#ifndef TVG_FACTORY_HPP
#define TVG_FACTORY_HPP

#include <thorvg.h>
#include <rive/factory.hpp>
#include <rive/refcnt.hpp>
#include <rive/math/raw_path.hpp>
#include <rive/renderer.hpp>
#include <rive/command_path.hpp>

namespace rive_thorvg {

/// TvgRenderBuffer — wraps raw vertex/index data for mesh rendering.
class TvgRenderBuffer : public rive::RenderBuffer {
public:
    TvgRenderBuffer(rive::RenderBufferType type,
                    rive::RenderBufferFlags flags,
                    size_t sizeInBytes);
    ~TvgRenderBuffer() override;

    void* onMap() override;
    void onUnmap() override;

    const void* data() const { return m_data; }
    size_t size() const { return m_sizeInBytes; }

private:
    void* m_data = nullptr;
    size_t m_sizeInBytes = 0;
};

/// TvgRenderShader — base class for gradient shaders.
class TvgRenderShader : public rive::RenderShader {
public:
    virtual ~TvgRenderShader() = default;

    /// Apply this shader (gradient) to a ThorVG shape.
    virtual void apply(tvg::Shape* shape) const = 0;
};

class TvgLinearGradientShader : public TvgRenderShader {
public:
    TvgLinearGradientShader(float sx, float sy, float ex, float ey,
                            const rive::ColorInt colors[],
                            const float stops[],
                            size_t count);
    void apply(tvg::Shape* shape) const override;

private:
    float m_sx, m_sy, m_ex, m_ey;
    std::vector<tvg::Fill::ColorStop> m_stops;
};

class TvgRadialGradientShader : public TvgRenderShader {
public:
    TvgRadialGradientShader(float cx, float cy, float radius,
                            const rive::ColorInt colors[],
                            const float stops[],
                            size_t count);
    void apply(tvg::Shape* shape) const override;

private:
    float m_cx, m_cy, m_radius;
    std::vector<tvg::Fill::ColorStop> m_stops;
};

/// TvgRenderPath — wraps a tvg::Shape with path commands.
/// Implements both CommandPath (for building) and RenderPath (for rendering).
class TvgRenderPath : public rive::RenderPath {
public:
    TvgRenderPath();
    TvgRenderPath(rive::RawPath& rawPath, rive::FillRule fillRule);
    ~TvgRenderPath() override = default;

    // RenderPath interface
    void rewind() override;
    void addRenderPath(rive::RenderPath* path, const rive::Mat2D& transform) override;
    void addRawPath(const rive::RawPath& path) override;
    void fillRule(rive::FillRule value) override;
    void moveTo(float x, float y) override;
    void lineTo(float x, float y) override;
    void cubicTo(float ox, float oy, float ix, float iy, float x, float y) override;
    void close() override;

    /// Get the internal ThorVG shape.
    tvg::Shape* tvgShape() const { return m_shape; }

    /// Create a transformed copy for drawing. Caller takes ownership via unref().
    tvg::Shape* createTransformedShape(const rive::Mat2D& transform) const;

private:
    tvg::Shape* m_shape;  // ref-counted by ThorVG
};

/// TvgRenderPaint — wraps Rive paint (fill/stroke + shader) for ThorVG.
class TvgRenderPaint : public rive::RenderPaint {
public:
    TvgRenderPaint();
    ~TvgRenderPaint() override = default;

    void style(rive::RenderPaintStyle style) override;
    void color(rive::ColorInt value) override;
    void thickness(float value) override;
    void join(rive::StrokeJoin value) override;
    void cap(rive::StrokeCap value) override;
    void blendMode(rive::BlendMode value) override;
    void shader(rive::rcp<rive::RenderShader> shader) override;
    void invalidateStroke() override {}

    /// Apply this paint to a ThorVG shape for rendering.
    void apply(tvg::Shape* shape) const;

    bool isStroke() const { return m_isStroke; }
    rive::BlendMode riveBlendMode() const { return m_blendMode; }

private:
    bool m_isStroke = false;
    rive::ColorInt m_color = 0xFF000000;
    float m_thickness = 1.0f;
    tvg::StrokeJoin m_join = tvg::StrokeJoin::Miter;
    tvg::StrokeCap m_cap = tvg::StrokeCap::Butt;
    rive::BlendMode m_blendMode = rive::BlendMode::srcOver;
    rive::rcp<rive::RenderShader> m_shader;
};

/// TvgRenderImage — holds decoded pixel data for image rendering.
class TvgRenderImage : public rive::RenderImage {
public:
    TvgRenderImage(int width, int height, uint8_t* pixels);
    ~TvgRenderImage() override;

    const uint8_t* pixels() const { return m_pixels; }

private:
    uint8_t* m_pixels;  // RGBA8888, owned
};

/// TvgFactory — creates Rive rendering objects backed by ThorVG.
class TvgFactory : public rive::Factory {
public:
    TvgFactory(tvg::SwCanvas* canvas);
    ~TvgFactory() override = default;

    rive::rcp<rive::RenderBuffer> makeRenderBuffer(
        rive::RenderBufferType type,
        rive::RenderBufferFlags flags,
        size_t sizeInBytes) override;

    rive::rcp<rive::RenderShader> makeLinearGradient(
        float sx, float sy, float ex, float ey,
        const rive::ColorInt colors[],
        const float stops[],
        size_t count) override;

    rive::rcp<rive::RenderShader> makeRadialGradient(
        float cx, float cy, float radius,
        const rive::ColorInt colors[],
        const float stops[],
        size_t count) override;

    rive::rcp<rive::RenderPath> makeRenderPath(
        rive::RawPath& rawPath,
        rive::FillRule fillRule) override;

    rive::rcp<rive::RenderPath> makeEmptyRenderPath() override;

    rive::rcp<rive::RenderPaint> makeRenderPaint() override;

    rive::rcp<rive::RenderImage> decodeImage(
        rive::Span<const uint8_t> data) override;

private:
    tvg::SwCanvas* m_canvas;
};

} // namespace rive_thorvg

#endif // TVG_FACTORY_HPP
