#include "tvg_factory.hpp"

#include <cstring>
#include <cstdlib>

#include "stb_image.h"

// ThorVG bundled WebP decoder
#include "webp/decode.h"

namespace rive_thorvg {

// ---------------------------------------------------------------------------
// TvgRenderBuffer
// ---------------------------------------------------------------------------

TvgRenderBuffer::TvgRenderBuffer(rive::RenderBufferType type,
                                 rive::RenderBufferFlags flags,
                                 size_t sizeInBytes)
    : rive::RenderBuffer(type, flags, sizeInBytes),
      m_sizeInBytes(sizeInBytes)
{
    m_data = std::malloc(sizeInBytes);
}

TvgRenderBuffer::~TvgRenderBuffer() {
    std::free(m_data);
}

void* TvgRenderBuffer::onMap() { return m_data; }
void TvgRenderBuffer::onUnmap() {}

// ---------------------------------------------------------------------------
// Gradient helpers
// ---------------------------------------------------------------------------

static tvg::Fill::ColorStop makeColorStop(float offset, rive::ColorInt color) {
    tvg::Fill::ColorStop cs;
    cs.offset = offset;
    cs.r = (color >> 16) & 0xFF;
    cs.g = (color >> 8) & 0xFF;
    cs.b = (color >> 0) & 0xFF;
    cs.a = (color >> 24) & 0xFF;
    return cs;
}

// ---------------------------------------------------------------------------
// TvgLinearGradientShader
// ---------------------------------------------------------------------------

TvgLinearGradientShader::TvgLinearGradientShader(
    float sx, float sy, float ex, float ey,
    const rive::ColorInt colors[], const float stops[], size_t count)
    : m_sx(sx), m_sy(sy), m_ex(ex), m_ey(ey)
{
    m_stops.reserve(count);
    for (size_t i = 0; i < count; ++i)
        m_stops.push_back(makeColorStop(stops[i], colors[i]));
}

void TvgLinearGradientShader::apply(tvg::Shape* shape) const {
    auto* grad = tvg::LinearGradient::gen();
    grad->linear(m_sx, m_sy, m_ex, m_ey);
    grad->colorStops(m_stops.data(), static_cast<uint32_t>(m_stops.size()));
    shape->fill(grad);  // takes ownership
}

// ---------------------------------------------------------------------------
// TvgRadialGradientShader
// ---------------------------------------------------------------------------

TvgRadialGradientShader::TvgRadialGradientShader(
    float cx, float cy, float radius,
    const rive::ColorInt colors[], const float stops[], size_t count)
    : m_cx(cx), m_cy(cy), m_radius(radius)
{
    m_stops.reserve(count);
    for (size_t i = 0; i < count; ++i)
        m_stops.push_back(makeColorStop(stops[i], colors[i]));
}

void TvgRadialGradientShader::apply(tvg::Shape* shape) const {
    auto* grad = tvg::RadialGradient::gen();
    // ThorVG v1.0: radial(cx, cy, r, fx, fy, fr) — focal = center, focal radius = 0
    grad->radial(m_cx, m_cy, m_radius, m_cx, m_cy, 0.0f);
    grad->colorStops(m_stops.data(), static_cast<uint32_t>(m_stops.size()));
    shape->fill(grad);  // takes ownership
}

// ---------------------------------------------------------------------------
// TvgRenderPath
// ---------------------------------------------------------------------------

TvgRenderPath::TvgRenderPath() {
    m_shape = tvg::Shape::gen();
}

TvgRenderPath::TvgRenderPath(rive::RawPath& rawPath, rive::FillRule fillRule) {
    m_shape = tvg::Shape::gen();

    m_shape->fillRule(fillRule == rive::FillRule::evenOdd
                          ? tvg::FillRule::EvenOdd
                          : tvg::FillRule::NonZero);

    const rive::Vec2D* pts = rawPath.points().data();
    for (auto verb : rawPath.verbs()) {
        switch (verb) {
            case rive::PathVerb::move:
                m_shape->moveTo(pts->x, pts->y);
                pts += 1;
                break;
            case rive::PathVerb::line:
                m_shape->lineTo(pts->x, pts->y);
                pts += 1;
                break;
            case rive::PathVerb::cubic:
                m_shape->cubicTo(pts[0].x, pts[0].y,
                                 pts[1].x, pts[1].y,
                                 pts[2].x, pts[2].y);
                pts += 3;
                break;
            case rive::PathVerb::close:
                m_shape->close();
                break;
            default:
                break;
        }
    }
}

void TvgRenderPath::rewind() {
    m_shape->reset();
}

void TvgRenderPath::addRenderPath(rive::RenderPath* path,
                                  const rive::Mat2D& transform) {
    auto* srcPath = static_cast<TvgRenderPath*>(path);
    if (!srcPath || !srcPath->m_shape) return;

    // Read the source shape's path data
    const tvg::PathCommand* cmds = nullptr;
    uint32_t cmdCnt = 0;
    const tvg::Point* pts = nullptr;
    uint32_t ptsCnt = 0;

    if (srcPath->m_shape->path(&cmds, &cmdCnt, &pts, &ptsCnt) != tvg::Result::Success) {
        return;
    }

    // Transform helper
    auto xform = [&](const tvg::Point& p) -> std::pair<float, float> {
        float x = transform[0] * p.x + transform[2] * p.y + transform[4];
        float y = transform[1] * p.x + transform[3] * p.y + transform[5];
        return {x, y};
    };

    // Replay transformed commands into this shape
    uint32_t ptIdx = 0;
    for (uint32_t i = 0; i < cmdCnt; ++i) {
        switch (cmds[i]) {
            case tvg::PathCommand::MoveTo: {
                auto [x, y] = xform(pts[ptIdx++]);
                m_shape->moveTo(x, y);
                break;
            }
            case tvg::PathCommand::LineTo: {
                auto [x, y] = xform(pts[ptIdx++]);
                m_shape->lineTo(x, y);
                break;
            }
            case tvg::PathCommand::CubicTo: {
                auto [cx1, cy1] = xform(pts[ptIdx++]);
                auto [cx2, cy2] = xform(pts[ptIdx++]);
                auto [x, y] = xform(pts[ptIdx++]);
                m_shape->cubicTo(cx1, cy1, cx2, cy2, x, y);
                break;
            }
            case tvg::PathCommand::Close:
                m_shape->close();
                break;
        }
    }
}

void TvgRenderPath::addRawPath(const rive::RawPath& path) {
    const rive::Vec2D* pts = path.points().data();
    for (auto verb : path.verbs()) {
        switch (verb) {
            case rive::PathVerb::move:
                m_shape->moveTo(pts->x, pts->y); pts += 1; break;
            case rive::PathVerb::line:
                m_shape->lineTo(pts->x, pts->y); pts += 1; break;
            case rive::PathVerb::cubic:
                m_shape->cubicTo(pts[0].x, pts[0].y, pts[1].x, pts[1].y,
                                 pts[2].x, pts[2].y);
                pts += 3; break;
            case rive::PathVerb::close:
                m_shape->close(); break;
            default: break;
        }
    }
}

void TvgRenderPath::fillRule(rive::FillRule value) {
    m_shape->fillRule(value == rive::FillRule::evenOdd
                          ? tvg::FillRule::EvenOdd
                          : tvg::FillRule::NonZero);
}

void TvgRenderPath::moveTo(float x, float y) { m_shape->moveTo(x, y); }
void TvgRenderPath::lineTo(float x, float y) { m_shape->lineTo(x, y); }
void TvgRenderPath::cubicTo(float ox, float oy, float ix, float iy,
                            float x, float y) {
    m_shape->cubicTo(ox, oy, ix, iy, x, y);
}
void TvgRenderPath::close() { m_shape->close(); }

tvg::Shape* TvgRenderPath::createTransformedShape(
    const rive::Mat2D& transform) const {
    auto* clone = static_cast<tvg::Shape*>(m_shape->duplicate());
    if (clone) {
        tvg::Matrix mat;
        mat.e11 = transform[0]; mat.e12 = transform[2]; mat.e13 = transform[4];
        mat.e21 = transform[1]; mat.e22 = transform[3]; mat.e23 = transform[5];
        mat.e31 = 0.0f;         mat.e32 = 0.0f;         mat.e33 = 1.0f;
        clone->transform(mat);
    }
    return clone;  // caller takes ownership
}

// ---------------------------------------------------------------------------
// TvgRenderPaint
// ---------------------------------------------------------------------------

TvgRenderPaint::TvgRenderPaint() {}

void TvgRenderPaint::style(rive::RenderPaintStyle style) {
    m_isStroke = (style == rive::RenderPaintStyle::stroke);
}
void TvgRenderPaint::color(rive::ColorInt value) { m_color = value; }
void TvgRenderPaint::thickness(float value) { m_thickness = value; }

void TvgRenderPaint::join(rive::StrokeJoin value) {
    switch (value) {
        case rive::StrokeJoin::miter: m_join = tvg::StrokeJoin::Miter; break;
        case rive::StrokeJoin::round: m_join = tvg::StrokeJoin::Round; break;
        case rive::StrokeJoin::bevel: m_join = tvg::StrokeJoin::Bevel; break;
    }
}

void TvgRenderPaint::cap(rive::StrokeCap value) {
    switch (value) {
        case rive::StrokeCap::butt:   m_cap = tvg::StrokeCap::Butt; break;
        case rive::StrokeCap::round:  m_cap = tvg::StrokeCap::Round; break;
        case rive::StrokeCap::square: m_cap = tvg::StrokeCap::Square; break;
    }
}

void TvgRenderPaint::blendMode(rive::BlendMode value) { m_blendMode = value; }
void TvgRenderPaint::shader(rive::rcp<rive::RenderShader> sh) { m_shader = std::move(sh); }

void TvgRenderPaint::apply(tvg::Shape* shape) const {
    uint8_t a = (m_color >> 24) & 0xFF;
    uint8_t r = (m_color >> 16) & 0xFF;
    uint8_t g = (m_color >> 8) & 0xFF;
    uint8_t b = (m_color >> 0) & 0xFF;

    if (m_shader) {
        auto* tvgShader = static_cast<const TvgRenderShader*>(m_shader.get());
        tvgShader->apply(shape);
    }

    if (m_isStroke) {
        shape->strokeFill(r, g, b, a);
        shape->strokeWidth(m_thickness);
        shape->strokeJoin(m_join);
        shape->strokeCap(m_cap);
    } else if (!m_shader) {
        shape->fill(r, g, b, a);
    }

    // Map Rive BlendMode to ThorVG BlendMethod
    switch (m_blendMode) {
        case rive::BlendMode::srcOver:    shape->blend(tvg::BlendMethod::Normal); break;
        case rive::BlendMode::screen:     shape->blend(tvg::BlendMethod::Screen); break;
        case rive::BlendMode::overlay:    shape->blend(tvg::BlendMethod::Overlay); break;
        case rive::BlendMode::darken:     shape->blend(tvg::BlendMethod::Darken); break;
        case rive::BlendMode::lighten:    shape->blend(tvg::BlendMethod::Lighten); break;
        case rive::BlendMode::colorDodge: shape->blend(tvg::BlendMethod::ColorDodge); break;
        case rive::BlendMode::colorBurn:  shape->blend(tvg::BlendMethod::ColorBurn); break;
        case rive::BlendMode::hardLight:  shape->blend(tvg::BlendMethod::HardLight); break;
        case rive::BlendMode::softLight:  shape->blend(tvg::BlendMethod::SoftLight); break;
        case rive::BlendMode::difference: shape->blend(tvg::BlendMethod::Difference); break;
        case rive::BlendMode::exclusion:  shape->blend(tvg::BlendMethod::Exclusion); break;
        case rive::BlendMode::multiply:   shape->blend(tvg::BlendMethod::Multiply); break;
        default:                          shape->blend(tvg::BlendMethod::Normal); break;
    }
}

// ---------------------------------------------------------------------------
// TvgRenderImage
// ---------------------------------------------------------------------------

TvgRenderImage::TvgRenderImage(int width, int height, uint8_t* pixels)
    : m_pixels(pixels)
{
    m_Width = width;
    m_Height = height;
}

TvgRenderImage::~TvgRenderImage() {
    std::free(m_pixels);
}

// ---------------------------------------------------------------------------
// TvgFactory
// ---------------------------------------------------------------------------

TvgFactory::TvgFactory(tvg::SwCanvas* canvas) : m_canvas(canvas) {}

rive::rcp<rive::RenderBuffer> TvgFactory::makeRenderBuffer(
    rive::RenderBufferType type, rive::RenderBufferFlags flags, size_t sizeInBytes) {
    return rive::rcp<rive::RenderBuffer>(new TvgRenderBuffer(type, flags, sizeInBytes));
}

rive::rcp<rive::RenderShader> TvgFactory::makeLinearGradient(
    float sx, float sy, float ex, float ey,
    const rive::ColorInt colors[], const float stops[], size_t count) {
    return rive::rcp<rive::RenderShader>(
        new TvgLinearGradientShader(sx, sy, ex, ey, colors, stops, count));
}

rive::rcp<rive::RenderShader> TvgFactory::makeRadialGradient(
    float cx, float cy, float radius,
    const rive::ColorInt colors[], const float stops[], size_t count) {
    return rive::rcp<rive::RenderShader>(
        new TvgRadialGradientShader(cx, cy, radius, colors, stops, count));
}

rive::rcp<rive::RenderPath> TvgFactory::makeRenderPath(
    rive::RawPath& rawPath, rive::FillRule fillRule) {
    return rive::rcp<rive::RenderPath>(new TvgRenderPath(rawPath, fillRule));
}

rive::rcp<rive::RenderPath> TvgFactory::makeEmptyRenderPath() {
    return rive::rcp<rive::RenderPath>(new TvgRenderPath());
}

rive::rcp<rive::RenderPaint> TvgFactory::makeRenderPaint() {
    return rive::rcp<rive::RenderPaint>(new TvgRenderPaint());
}

rive::rcp<rive::RenderImage> TvgFactory::decodeImage(rive::Span<const uint8_t> data) {
    if (data.size() < 4) return nullptr;

    int w = 0, h = 0;
    uint8_t* pixels = nullptr;

    // Detect format
    bool isWebP = data.size() >= 12 && data[0] == 'R' && data[1] == 'I' &&
                  data[2] == 'F' && data[3] == 'F' && data[8] == 'W' &&
                  data[9] == 'E' && data[10] == 'B' && data[11] == 'P';

    if (isWebP) {
        pixels = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
        if (pixels && w > 0 && h > 0) {
            return rive::rcp<rive::RenderImage>(new TvgRenderImage(w, h, pixels));
        }
        if (pixels) std::free(pixels);
    }

    // PNG/JPEG/other: use stb_image
    int channels;
    pixels = stbi_load_from_memory(
        data.data(), static_cast<int>(data.size()),
        &w, &h, &channels, 4);

    if (!pixels) return nullptr;

    return rive::rcp<rive::RenderImage>(new TvgRenderImage(w, h, pixels));
}

} // namespace rive_thorvg
