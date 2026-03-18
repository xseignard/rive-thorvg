/// rive_thorvg.cpp — C API implementation for the rive-thorvg library.
///
/// This file implements the public C API defined in rive_thorvg.h.
/// It wraps the C++ Rive runtime + ThorVG bridge behind a clean C interface.

#include "rive_thorvg.h"
#include "bridge/tvg_factory.hpp"
#include "bridge/tvg_renderer.hpp"

#include <thorvg.h>

// Rive runtime headers
#include <rive/file.hpp>
#include <rive/artboard.hpp>
#include <rive/animation/state_machine_instance.hpp>
#include <rive/animation/state_machine_input_instance.hpp>
#include <rive/math/mat2d.hpp>
#include <rive/joystick.hpp>
#include <rive/file_asset_loader.hpp>
#include <rive/assets/file_asset.hpp>
#include <rive/assets/image_asset.hpp>
#include <rive/assets/font_asset.hpp>

#include <rive/nested_artboard.hpp>
#include <rive/viewmodel/viewmodel.hpp>
#include <rive/viewmodel/viewmodel_instance.hpp>
#include <rive/viewmodel/runtime/viewmodel_instance_runtime.hpp>
#include <rive/data_bind/data_bind.hpp>

#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>

#include <rive/simple_array.hpp>

// ---------------------------------------------------------------------------
// Bridge asset loader — wraps the C callback
// ---------------------------------------------------------------------------

class BridgeAssetLoader : public rive::FileAssetLoader {
public:
    BridgeAssetLoader(rive_asset_loader_cb cb, void* user_data)
        : m_cb(cb), m_userData(user_data) {}

    bool loadContents(rive::FileAsset& asset,
                      rive::Span<const uint8_t> inBandBytes,
                      rive::Factory* factory) override {
        if (!m_cb) return false;

        bool isImage = asset.is<rive::ImageAsset>();
        bool isFont = asset.is<rive::FontAsset>();
        std::string name = asset.uniqueFilename();
        std::string ext = asset.fileExtension();

        uint8_t* outData = nullptr;
        size_t outLen = 0;

        bool handled = m_cb(name.c_str(), ext.c_str(),
                            isImage, isFont,
                            &outData, &outLen,
                            m_userData);

        if (!handled || !outData || outLen == 0) return false;

        // Feed the loaded bytes back to the asset for decoding
        rive::SimpleArray<uint8_t> bytes(outLen);
        std::memcpy(bytes.data(), outData, outLen);
        std::free(outData);
        asset.decode(bytes, factory);
        return true;
    }

private:
    rive_asset_loader_cb m_cb;
    void* m_userData;
};

// ---------------------------------------------------------------------------
// Internal instance structure
// ---------------------------------------------------------------------------

struct rive_instance {
    // Pixel buffer (owned by caller)
    void* buffer = nullptr;
    int width = 0;
    int height = 0;
    rive_pixel_format_t format = RIVE_PIXEL_FORMAT_ARGB8888;

    // Fit & alignment
    rive_fit_t fit = RIVE_FIT_CONTAIN;
    rive_align_t align = RIVE_ALIGN_CENTER;

    // ThorVG canvas — renders into the pixel buffer (owned, must be deleted)
    tvg::SwCanvas* canvas = nullptr;

    // Internal ARGB staging buffer used when the output format is RGB565.
    uint32_t* argb_staging = nullptr;

    // Factory and renderer (bridge layer)
    std::unique_ptr<rive_thorvg::TvgFactory> factory;
    std::unique_ptr<rive_thorvg::TvgRenderer> renderer;

    // Rive runtime objects
    rive::rcp<rive::File> file;
    std::unique_ptr<rive::ArtboardInstance> artboard;
    std::unique_ptr<rive::StateMachineInstance> stateMachine;

    // ViewModel runtime (for data binding)
    rive::rcp<rive::ViewModelInstanceRuntime> vmRuntime;

    // Event callback
    rive_event_cb event_cb = nullptr;
    void* event_user_data = nullptr;

    // Asset loader callback
    rive_asset_loader_cb asset_loader = nullptr;
    void* asset_user_data = nullptr;

    // Cached string for rive_vm_get_string
    std::string cached_string;

    // Track whether the scene is dirty (needs re-render)
    bool dirty = true;
    bool first_render = true;
};

static void argb8888ToRgb565(const uint32_t* src, uint16_t* dst, size_t pixel_count) {
    if (!src || !dst || pixel_count == 0) return;

    size_t i = 0;
    for (; i + 3 < pixel_count; i += 4) {
        uint32_t a0 = src[i + 0];
        uint32_t a1 = src[i + 1];
        uint32_t a2 = src[i + 2];
        uint32_t a3 = src[i + 3];
        dst[i + 0] = static_cast<uint16_t>((((a0 >> 16) & 0xF8) << 8) |
                                           (((a0 >> 8) & 0xFC) << 3) |
                                           ((a0 >> 3) & 0x1F));
        dst[i + 1] = static_cast<uint16_t>((((a1 >> 16) & 0xF8) << 8) |
                                           (((a1 >> 8) & 0xFC) << 3) |
                                           ((a1 >> 3) & 0x1F));
        dst[i + 2] = static_cast<uint16_t>((((a2 >> 16) & 0xF8) << 8) |
                                           (((a2 >> 8) & 0xFC) << 3) |
                                           ((a2 >> 3) & 0x1F));
        dst[i + 3] = static_cast<uint16_t>((((a3 >> 16) & 0xF8) << 8) |
                                           (((a3 >> 8) & 0xFC) << 3) |
                                           ((a3 >> 3) & 0x1F));
    }

    for (; i < pixel_count; ++i) {
        uint32_t argb = src[i];
        dst[i] = static_cast<uint16_t>((((argb >> 16) & 0xF8) << 8) |
                                       (((argb >> 8) & 0xFC) << 3) |
                                       ((argb >> 3) & 0x1F));
    }
}

// ---------------------------------------------------------------------------
// Helper: convert rive_fit_t to rive::Fit
// ---------------------------------------------------------------------------

static rive::Fit toRiveFit(rive_fit_t fit) {
    switch (fit) {
        case RIVE_FIT_CONTAIN:    return rive::Fit::contain;
        case RIVE_FIT_COVER:      return rive::Fit::cover;
        case RIVE_FIT_FILL:       return rive::Fit::fill;
        case RIVE_FIT_FIT_WIDTH:  return rive::Fit::fitWidth;
        case RIVE_FIT_FIT_HEIGHT: return rive::Fit::fitHeight;
        case RIVE_FIT_NONE:       return rive::Fit::none;
        case RIVE_FIT_SCALE_DOWN: return rive::Fit::scaleDown;
        default:                  return rive::Fit::contain;
    }
}

// ---------------------------------------------------------------------------
// Helper: convert rive_align_t to rive::Alignment
// ---------------------------------------------------------------------------

static rive::Alignment toRiveAlignment(rive_align_t align) {
    switch (align) {
        case RIVE_ALIGN_CENTER:        return rive::Alignment::center;
        case RIVE_ALIGN_TOP_LEFT:      return rive::Alignment::topLeft;
        case RIVE_ALIGN_TOP_CENTER:    return rive::Alignment::topCenter;
        case RIVE_ALIGN_TOP_RIGHT:     return rive::Alignment::topRight;
        case RIVE_ALIGN_CENTER_LEFT:   return rive::Alignment::centerLeft;
        case RIVE_ALIGN_CENTER_RIGHT:  return rive::Alignment::centerRight;
        case RIVE_ALIGN_BOTTOM_LEFT:   return rive::Alignment::bottomLeft;
        case RIVE_ALIGN_BOTTOM_CENTER: return rive::Alignment::bottomCenter;
        case RIVE_ALIGN_BOTTOM_RIGHT:  return rive::Alignment::bottomRight;
        default:                       return rive::Alignment::center;
    }
}

// ---------------------------------------------------------------------------
// Helper: compute the alignment matrix (artboard → pixel buffer)
// ---------------------------------------------------------------------------

static rive::Mat2D computeAlignment(rive_instance_t* ctx) {
    if (!ctx || !ctx->artboard) {
        return rive::Mat2D();
    }

    rive::AABB frame(0, 0, static_cast<float>(ctx->width),
                     static_cast<float>(ctx->height));
    rive::AABB content(0, 0, ctx->artboard->width(), ctx->artboard->height());

    return rive::computeAlignment(toRiveFit(ctx->fit),
                                  toRiveAlignment(ctx->align),
                                  frame,
                                  content);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void rive_thorvg_init(int thread_count) {
    tvg::Initializer::init(thread_count);
}

void rive_thorvg_term(void) {
    tvg::Initializer::term();
}

rive_instance_t* rive_create(void* buffer, int width, int height,
                             rive_pixel_format_t format) {
    if (!buffer || width <= 0 || height <= 0) return nullptr;

    auto* ctx = new rive_instance();
    ctx->buffer = buffer;
    ctx->width = width;
    ctx->height = height;
    ctx->format = format;

    // Create ThorVG software canvas with smart partial redraw enabled.
    ctx->canvas = tvg::SwCanvas::gen(tvg::EngineOption::SmartRender);
    if (!ctx->canvas) {
        delete ctx;
        return nullptr;
    }

    // Set target buffer
    if (format == RIVE_PIXEL_FORMAT_ARGB8888) {
        ctx->canvas->target(static_cast<uint32_t*>(buffer),
                            width, width, height,
                            tvg::ColorSpace::ARGB8888);
    } else {
        ctx->argb_staging = static_cast<uint32_t*>(
            std::calloc(static_cast<size_t>(width) * static_cast<size_t>(height),
                        sizeof(uint32_t)));
        if (!ctx->argb_staging) {
            delete ctx->canvas;
            delete ctx;
            return nullptr;
        }

        ctx->canvas->target(ctx->argb_staging,
                            width, width, height,
                            tvg::ColorSpace::ARGB8888);
    }

    // Create factory and renderer
    ctx->factory = std::make_unique<rive_thorvg::TvgFactory>(ctx->canvas);
    ctx->renderer = std::make_unique<rive_thorvg::TvgRenderer>(ctx->canvas);
    ctx->renderer->setBufferSize(width, height);

    // Wire staging buffer for deferred mesh blit (bypasses ThorVG Picture path).
    // For ARGB8888, the buffer IS the target; for RGB565, it's the staging buffer.
    uint32_t* meshTarget = ctx->argb_staging
                           ? ctx->argb_staging
                           : static_cast<uint32_t*>(buffer);
    ctx->renderer->setStagingBuffer(meshTarget, width);

    return ctx;
}

bool rive_load(rive_instance_t* ctx, const uint8_t* riv_data, size_t riv_len) {
    return rive_load_ex(ctx, riv_data, riv_len, nullptr, nullptr);
}

bool rive_load_ex(rive_instance_t* ctx, const uint8_t* riv_data, size_t riv_len,
                  const char* artboard_name, const char* state_machine_name) {
    if (!ctx || !riv_data || riv_len == 0) return false;

    // Create asset loader if callback is set
    rive::FileAssetLoader* assetLoader = nullptr;
    rive::rcp<BridgeAssetLoader> bridgeLoader;
    if (ctx->asset_loader) {
        bridgeLoader = rive::rcp<BridgeAssetLoader>(
            new BridgeAssetLoader(ctx->asset_loader, ctx->asset_user_data));
        assetLoader = bridgeLoader.get();
    }

    // Parse the .riv file
    rive::ImportResult importResult;
    ctx->file = rive::File::import(
        rive::Span<const uint8_t>(riv_data, riv_len),
        ctx->factory.get(),
        &importResult,
        assetLoader);

    if (!ctx->file || importResult != rive::ImportResult::success) return false;

    // Get artboard
    if (artboard_name) {
        ctx->artboard = ctx->file->artboardNamed(artboard_name);
    } else {
        ctx->artboard = ctx->file->artboardDefault();
    }

    if (!ctx->artboard) return false;

    // Get state machine
    if (state_machine_name) {
        ctx->stateMachine = ctx->artboard->stateMachineNamed(state_machine_name);
    } else {
        ctx->stateMachine = ctx->artboard->defaultStateMachine();
        // Fall back to first state machine if no default is marked
        if (!ctx->stateMachine && ctx->artboard->stateMachineCount() > 0) {
            ctx->stateMachine = ctx->artboard->stateMachineAt(0);
        }
    }

    // Bind ViewModel AFTER creating state machine — binding to the SM
    // automatically binds to the artboard and sets up data bindings.
    rive_bind_default_viewmodel(ctx);

    // Initial advance to compute transforms, opacity, etc.
    if (ctx->stateMachine) {
        ctx->stateMachine->advanceAndApply(0.0f);
    } else {
        ctx->artboard->advance(0.0f);
    }

    ctx->dirty = true;

    return true;
}

void rive_destroy(rive_instance_t* ctx) {
    if (!ctx) return;

    // Release Rive objects in reverse order
    ctx->vmRuntime = nullptr;
    ctx->stateMachine.reset();
    ctx->artboard.reset();
    ctx->file.reset();

    // Release ThorVG objects
    ctx->renderer.reset();
    ctx->factory.reset();
    if (ctx->argb_staging) {
        std::free(ctx->argb_staging);
        ctx->argb_staging = nullptr;
    }
    if (ctx->canvas) {
        delete ctx->canvas;
        ctx->canvas = nullptr;
    }

    delete ctx;
}

// ---------------------------------------------------------------------------
// Fit & Alignment
// ---------------------------------------------------------------------------

void rive_set_fit(rive_instance_t* ctx, rive_fit_t fit, rive_align_t align) {
    if (!ctx) return;
    ctx->fit = fit;
    ctx->align = align;
    ctx->dirty = true;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

bool rive_advance(rive_instance_t* ctx, float elapsed_seconds) {
    if (!ctx) return false;

    rive_advance_state(ctx, elapsed_seconds);
    return rive_render(ctx);
}

void rive_advance_state(rive_instance_t* ctx, float elapsed_seconds) {
    if (!ctx) return;

    if (ctx->stateMachine) {
        bool changed = ctx->stateMachine->advanceAndApply(elapsed_seconds);
        ctx->dirty = ctx->dirty || changed;
    } else if (ctx->artboard) {
        bool changed = ctx->artboard->advance(elapsed_seconds);
        ctx->dirty = ctx->dirty || changed;
    }
}

bool rive_render(rive_instance_t* ctx) {
    if (!ctx || !ctx->artboard) return false;
    if (!ctx->dirty) return false;

    // Clear all paints from the previous frame
    ctx->canvas->remove();

    // Clear deferred mesh list from previous frame
    ctx->renderer->clearDeferredMeshes();

    // Compute alignment transform
    rive::Mat2D alignMat = computeAlignment(ctx);

    // Set up renderer with alignment transform
    ctx->renderer->save();
    ctx->renderer->transform(alignMat);

    // Draw the artboard — pushes shapes to the ThorVG canvas.
    // Mesh image draws are deferred (not added to ThorVG canvas).
    ctx->artboard->draw(ctx->renderer.get());

    ctx->renderer->restore();

    // Rasterize ThorVG scene (vector shapes + non-mesh images) into staging buffer.
    // This clears the buffer first, which is why mesh blits must happen AFTER.
    ctx->canvas->draw(true);
    ctx->canvas->sync();

    // Now apply deferred mesh image blits on top of the rasterized scene.
    ctx->renderer->flushDeferredMeshes();

    if (ctx->format == RIVE_PIXEL_FORMAT_RGB565 && ctx->argb_staging) {
        size_t pixelCount = static_cast<size_t>(ctx->width) * static_cast<size_t>(ctx->height);
        argb8888ToRgb565(ctx->argb_staging,
                         static_cast<uint16_t*>(ctx->buffer),
                         pixelCount);
    }

    ctx->dirty = false;
    ctx->first_render = false;
    return true;
}

// ---------------------------------------------------------------------------
// State Machine Input
// ---------------------------------------------------------------------------

void rive_set_bool(rive_instance_t* ctx, const char* name, bool value) {
    if (!ctx || !ctx->stateMachine || !name) return;
    auto* input = ctx->stateMachine->getBool(name);
    if (input) input->value(value);
}

void rive_set_number(rive_instance_t* ctx, const char* name, float value) {
    if (!ctx || !ctx->stateMachine || !name) return;
    auto* input = ctx->stateMachine->getNumber(name);
    if (input) input->value(value);
}

void rive_fire_trigger(rive_instance_t* ctx, const char* name) {
    if (!ctx || !ctx->stateMachine || !name) return;
    auto* input = ctx->stateMachine->getTrigger(name);
    if (input) input->fire();
}

// ---------------------------------------------------------------------------
// ViewModel / Data Binding
// ---------------------------------------------------------------------------

bool rive_bind_default_viewmodel(rive_instance_t* ctx) {
    if (!ctx || !ctx->file || !ctx->artboard) return false;

    // Create default ViewModel instance for this artboard
    auto vmInstance = ctx->file->createDefaultViewModelInstance(ctx->artboard.get());
    if (!vmInstance) return false;

    // Bind to state machine if available (preferred — auto-binds artboard too),
    // otherwise bind directly to artboard.
    if (ctx->stateMachine) {
        ctx->stateMachine->bindViewModelInstance(vmInstance);
    } else {
        ctx->artboard->bindViewModelInstance(vmInstance);
    }

    // Create the runtime wrapper for property access
    ctx->vmRuntime = rive::rcp<rive::ViewModelInstanceRuntime>(
        new rive::ViewModelInstanceRuntime(vmInstance));

    return true;
}

void rive_vm_set_bool(rive_instance_t* ctx, const char* path, bool value) {
    if (!ctx || !ctx->vmRuntime || !path) return;
    auto* prop = ctx->vmRuntime->propertyBoolean(path);
    if (prop) prop->value(value);
}

void rive_vm_set_number(rive_instance_t* ctx, const char* path, float value) {
    if (!ctx || !ctx->vmRuntime || !path) return;
    auto* prop = ctx->vmRuntime->propertyNumber(path);
    if (prop) prop->value(value);
}

void rive_vm_set_string(rive_instance_t* ctx, const char* path,
                        const char* value) {
    if (!ctx || !ctx->vmRuntime || !path) return;
    auto* prop = ctx->vmRuntime->propertyString(path);
    if (prop) prop->value(value);
}

void rive_vm_set_color(rive_instance_t* ctx, const char* path, uint32_t argb) {
    if (!ctx || !ctx->vmRuntime || !path) return;
    auto* prop = ctx->vmRuntime->propertyColor(path);
    if (prop) prop->value(static_cast<int>(argb));
}

void rive_vm_set_enum(rive_instance_t* ctx, const char* path,
                      const char* value) {
    if (!ctx || !ctx->vmRuntime || !path) return;
    auto* prop = ctx->vmRuntime->propertyEnum(path);
    if (prop) prop->value(value);
}

void rive_vm_fire_trigger(rive_instance_t* ctx, const char* path) {
    if (!ctx || !ctx->vmRuntime || !path) return;
    auto* prop = ctx->vmRuntime->propertyTrigger(path);
    if (prop) prop->trigger();
}

bool rive_vm_get_bool(rive_instance_t* ctx, const char* path) {
    if (!ctx || !ctx->vmRuntime || !path) return false;
    auto* prop = ctx->vmRuntime->propertyBoolean(path);
    return prop ? prop->value() : false;
}

float rive_vm_get_number(rive_instance_t* ctx, const char* path) {
    if (!ctx || !ctx->vmRuntime || !path) return 0.0f;
    auto* prop = ctx->vmRuntime->propertyNumber(path);
    return prop ? prop->value() : 0.0f;
}

const char* rive_vm_get_string(rive_instance_t* ctx, const char* path) {
    if (!ctx || !ctx->vmRuntime || !path) return nullptr;
    auto* prop = ctx->vmRuntime->propertyString(path);
    if (!prop) return nullptr;
    ctx->cached_string = prop->value();
    return ctx->cached_string.c_str();
}

uint32_t rive_vm_get_color(rive_instance_t* ctx, const char* path) {
    if (!ctx || !ctx->vmRuntime || !path) return 0;
    auto* prop = ctx->vmRuntime->propertyColor(path);
    return prop ? static_cast<uint32_t>(prop->value()) : 0;
}

// ---------------------------------------------------------------------------
// Pointer / Touch
// ---------------------------------------------------------------------------

// Helper: update joysticks with artboard-space pointer coordinates.
// Maps artboard bounds (0..width, 0..height) to the joystick range (-1..+1).
static void updateJoysticks(rive_instance_t* ctx, float art_x, float art_y) {
    if (!ctx || !ctx->artboard) return;

    float abW = ctx->artboard->width();
    float abH = ctx->artboard->height();
    if (abW <= 0 || abH <= 0) return;

    // Normalize: artboard (0,0)-(w,h) → (-1,-1)-(+1,+1)
    float nx = art_x / abW * 2.0f - 1.0f;
    float ny = art_y / abH * 2.0f - 1.0f;

    for (auto object : ctx->artboard->objects()) {
        if (object == nullptr || !object->is<rive::Joystick>()) continue;
        object->as<rive::Joystick>()->x(nx);
        object->as<rive::Joystick>()->y(ny);
    }
}

void rive_pointer_down(rive_instance_t* ctx, float x, float y) {
    if (!ctx) return;
    if (ctx->stateMachine)
        ctx->stateMachine->pointerDown(rive::Vec2D(x, y));
    ctx->dirty = true;
}

void rive_pointer_move(rive_instance_t* ctx, float x, float y) {
    if (!ctx) return;
    if (ctx->stateMachine)
        ctx->stateMachine->pointerMove(rive::Vec2D(x, y));
    updateJoysticks(ctx, x, y);
    ctx->dirty = true;
}

void rive_pointer_up(rive_instance_t* ctx, float x, float y) {
    if (!ctx) return;
    if (ctx->stateMachine)
        ctx->stateMachine->pointerUp(rive::Vec2D(x, y));
    ctx->dirty = true;
}

void rive_screen_to_artboard(rive_instance_t* ctx,
                             float screen_x, float screen_y,
                             float* art_x, float* art_y) {
    if (!ctx || !art_x || !art_y) return;

    // Compute the inverse of the alignment matrix
    rive::Mat2D alignMat = computeAlignment(ctx);
    rive::Mat2D inverse;
    if (!alignMat.invert(&inverse)) {
        // If inversion fails, pass through coordinates unchanged
        *art_x = screen_x;
        *art_y = screen_y;
        return;
    }

    rive::Vec2D screen(screen_x, screen_y);
    rive::Vec2D artboard = inverse * screen;
    *art_x = artboard.x;
    *art_y = artboard.y;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void rive_set_event_callback(rive_instance_t* ctx, rive_event_cb cb,
                             void* user_data) {
    if (!ctx) return;
    ctx->event_cb = cb;
    ctx->event_user_data = user_data;
}

// ---------------------------------------------------------------------------
// Asset Loading
// ---------------------------------------------------------------------------

void rive_set_asset_loader(rive_instance_t* ctx,
                           rive_asset_loader_cb loader, void* user_data) {
    if (!ctx) return;
    ctx->asset_loader = loader;
    ctx->asset_user_data = user_data;
}

// ---------------------------------------------------------------------------
// Artboard Info
// ---------------------------------------------------------------------------

float rive_artboard_width(rive_instance_t* ctx) {
    if (!ctx || !ctx->artboard) return 0.0f;
    return ctx->artboard->width();
}

float rive_artboard_height(rive_instance_t* ctx) {
    if (!ctx || !ctx->artboard) return 0.0f;
    return ctx->artboard->height();
}

// ---------------------------------------------------------------------------
// State Machine Query
// ---------------------------------------------------------------------------

int rive_input_count(rive_instance_t* ctx) {
    if (!ctx || !ctx->stateMachine) return 0;
    return static_cast<int>(ctx->stateMachine->inputCount());
}

const char* rive_input_name(rive_instance_t* ctx, int index) {
    if (!ctx || !ctx->stateMachine) return nullptr;
    if (index < 0 || index >= static_cast<int>(ctx->stateMachine->inputCount())) {
        return nullptr;
    }
    auto* input = ctx->stateMachine->input(index);
    if (!input) return nullptr;
    return input->name().c_str();
}
