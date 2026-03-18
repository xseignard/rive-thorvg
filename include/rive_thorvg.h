#ifndef RIVE_THORVG_H
#define RIVE_THORVG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rive_instance rive_instance_t;

/* -- Pixel Format --------------------------------------------------------- */

typedef enum {
    RIVE_PIXEL_FORMAT_ARGB8888,  // 4 bytes/pixel -- best quality, more RAM
    RIVE_PIXEL_FORMAT_RGB565,    // 2 bytes/pixel -- native for most SPI LCDs
} rive_pixel_format_t;

/* -- Fit & Alignment ------------------------------------------------------ */

typedef enum {
    RIVE_FIT_CONTAIN,    // Scale to fit, preserve aspect ratio (default)
    RIVE_FIT_COVER,      // Scale to cover, preserve aspect ratio, may clip
    RIVE_FIT_FILL,       // Stretch to fill, may distort
    RIVE_FIT_FIT_WIDTH,  // Scale to fit width
    RIVE_FIT_FIT_HEIGHT, // Scale to fit height
    RIVE_FIT_NONE,       // No scaling -- render at artboard size
    RIVE_FIT_SCALE_DOWN, // Like contain, but never scales up
} rive_fit_t;

typedef enum {
    RIVE_ALIGN_CENTER,        // Default
    RIVE_ALIGN_TOP_LEFT,      RIVE_ALIGN_TOP_CENTER,      RIVE_ALIGN_TOP_RIGHT,
    RIVE_ALIGN_CENTER_LEFT,                                RIVE_ALIGN_CENTER_RIGHT,
    RIVE_ALIGN_BOTTOM_LEFT,   RIVE_ALIGN_BOTTOM_CENTER,   RIVE_ALIGN_BOTTOM_RIGHT,
} rive_align_t;

/* -- Lifecycle ------------------------------------------------------------ */

// Initialize the ThorVG engine. Call once at startup.
// thread_count: number of ThorVG worker threads (1 for single-core, 2 for dual-core)
void rive_thorvg_init(int thread_count);

// Shut down the ThorVG engine. Call once at exit.
void rive_thorvg_term(void);

// Create a Rive instance that renders into the given pixel buffer.
// Buffer size must be width * height * bytes_per_pixel (4 for ARGB8888, 2 for RGB565).
// The caller owns the buffer and must keep it alive.
rive_instance_t* rive_create(void* buffer, int width, int height,
                             rive_pixel_format_t format);

// Load a .riv file. Selects default artboard and first state machine.
// Automatically binds the default ViewModel if one exists.
// Returns true on success, false on parse/version error.
bool rive_load(rive_instance_t* ctx, const uint8_t* riv_data, size_t riv_len);

// Load with explicit artboard and state machine selection.
// Pass NULL for artboard_name to use default, NULL for sm_name to use first.
bool rive_load_ex(rive_instance_t* ctx, const uint8_t* riv_data, size_t riv_len,
                  const char* artboard_name, const char* state_machine_name);

// Destroy the instance and free internal resources.
// Does NOT free the pixel buffer (caller owns it).
void rive_destroy(rive_instance_t* ctx);

/* -- Fit & Alignment ------------------------------------------------------ */

// Set how the artboard maps to the pixel buffer. Default: contain + center.
void rive_set_fit(rive_instance_t* ctx, rive_fit_t fit, rive_align_t align);

/* -- Render --------------------------------------------------------------- */

// Advance the animation/state machine by elapsed_seconds,
// then render the current frame into the pixel buffer.
// Returns true if the frame changed (buffer was updated).
// Convenience function -- equivalent to rive_advance_state() + rive_render().
bool rive_advance(rive_instance_t* ctx, float elapsed_seconds);

// Advance state machine only (no rendering). Useful for processing events
// or checking state without the cost of rasterization.
void rive_advance_state(rive_instance_t* ctx, float elapsed_seconds);

// Render the current frame into the pixel buffer without advancing.
// Useful for re-rendering after a buffer swap or resize.
// Returns true if the buffer was updated.
bool rive_render(rive_instance_t* ctx);

/* -- State Machine Input -------------------------------------------------- */

// Set inputs by name. Silently ignored if the input doesn't exist.
void rive_set_bool(rive_instance_t* ctx, const char* name, bool value);
void rive_set_number(rive_instance_t* ctx, const char* name, float value);
void rive_fire_trigger(rive_instance_t* ctx, const char* name);

/* -- ViewModel / Data Binding --------------------------------------------- */

// Bind the default ViewModel to the active artboard/state machine.
// Called automatically by rive_load() if a default ViewModel exists.
// Returns true if a ViewModel was bound.
bool rive_bind_default_viewmodel(rive_instance_t* ctx);

// Set ViewModel properties by path. Supports nested paths with '/'.
// Silently ignored if the path doesn't resolve to a property of the expected type.
void rive_vm_set_bool(rive_instance_t* ctx, const char* path, bool value);
void rive_vm_set_number(rive_instance_t* ctx, const char* path, float value);
void rive_vm_set_string(rive_instance_t* ctx, const char* path, const char* value);
void rive_vm_set_color(rive_instance_t* ctx, const char* path, uint32_t argb);
void rive_vm_set_enum(rive_instance_t* ctx, const char* path, const char* value);
void rive_vm_fire_trigger(rive_instance_t* ctx, const char* path);

// Read ViewModel properties. Returns default value (false/0.0/NULL/0) if path
// doesn't resolve. Returned string pointer is valid until next rive_advance().
bool        rive_vm_get_bool(rive_instance_t* ctx, const char* path);
float       rive_vm_get_number(rive_instance_t* ctx, const char* path);
const char* rive_vm_get_string(rive_instance_t* ctx, const char* path);
uint32_t    rive_vm_get_color(rive_instance_t* ctx, const char* path);

/* -- Pointer / Touch ------------------------------------------------------ */

// Coordinates are in artboard space.
// Use rive_screen_to_artboard() to convert from screen pixels.
void rive_pointer_down(rive_instance_t* ctx, float x, float y);
void rive_pointer_move(rive_instance_t* ctx, float x, float y);
void rive_pointer_up(rive_instance_t* ctx, float x, float y);

// Convert screen pixel coordinates to artboard coordinates,
// accounting for current fit/alignment settings.
void rive_screen_to_artboard(rive_instance_t* ctx,
                             float screen_x, float screen_y,
                             float* art_x, float* art_y);

/* -- Events --------------------------------------------------------------- */

// Callback for Rive events (audio triggers, general events).
typedef void (*rive_event_cb)(const char* event_name, void* user_data);
void rive_set_event_callback(rive_instance_t* ctx, rive_event_cb cb, void* user_data);

/* -- Asset Loading -------------------------------------------------------- */

// Callback for loading referenced/external assets at runtime.
// Called during rive_load() for each asset not embedded in the .riv.
// Return true if you handled the asset, false to skip it.
// For images: write decoded RGBA pixel data to *out_data, set *out_len.
// The library takes ownership of *out_data and will free() it.
typedef bool (*rive_asset_loader_cb)(const char* asset_name,
                                     const char* file_extension,
                                     bool is_image, bool is_font,
                                     uint8_t** out_data, size_t* out_len,
                                     void* user_data);
void rive_set_asset_loader(rive_instance_t* ctx,
                           rive_asset_loader_cb loader, void* user_data);

/* -- Artboard Info -------------------------------------------------------- */

float rive_artboard_width(rive_instance_t* ctx);
float rive_artboard_height(rive_instance_t* ctx);

/* -- State Machine Query -------------------------------------------------- */

// Get the number of state machine inputs (for introspection/debugging).
int rive_input_count(rive_instance_t* ctx);

// Get the name of input at index. Returns NULL if out of range.
const char* rive_input_name(rive_instance_t* ctx, int index);

#ifdef __cplusplus
}
#endif

#endif // RIVE_THORVG_H
