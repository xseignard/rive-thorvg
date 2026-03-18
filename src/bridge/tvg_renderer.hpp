#ifndef TVG_RENDERER_HPP
#define TVG_RENDERER_HPP

#include <thorvg.h>
#include <rive/renderer.hpp>
#include <rive/math/mat2d.hpp>
#include <stack>
#include <vector>
#include <functional>

namespace rive_thorvg {

class TvgRenderPath;

/// TvgRenderer — implements rive::Renderer using ThorVG's software canvas.
class TvgRenderer : public rive::Renderer {
public:
    explicit TvgRenderer(tvg::Canvas* canvas);
    ~TvgRenderer() override = default;

    void save() override;
    void restore() override;
    void transform(const rive::Mat2D& transform) override;

    void drawPath(rive::RenderPath* path, rive::RenderPaint* paint) override;
    void clipPath(rive::RenderPath* path) override;

    void drawImage(const rive::RenderImage* image,
                   rive::ImageSampler sampler,
                   rive::BlendMode blendMode,
                   float opacity) override;

    void drawImageMesh(const rive::RenderImage* image,
                       rive::ImageSampler sampler,
                       rive::rcp<rive::RenderBuffer> vertices,
                       rive::rcp<rive::RenderBuffer> uvCoords,
                       rive::rcp<rive::RenderBuffer> indices,
                       uint32_t vertexCount,
                       uint32_t indexCount,
                       rive::BlendMode blendMode,
                       float opacity) override;

    void modulateOpacity(float opacity) override;

    void setBufferSize(int width, int height) {
        m_width = width;
        m_height = height;
    }

    /// Set the direct ARGB staging buffer for deferred mesh blit.
    /// Mesh draws are collected during the Rive draw phase, then
    /// applied after canvas->draw()/sync() via flushDeferredMeshes().
    void setStagingBuffer(uint32_t* buffer, int stride) {
        m_stagingBuffer = buffer;
        m_stagingStride = stride;
    }

    /// Apply all deferred mesh blits to the staging buffer.
    /// Must be called AFTER canvas->draw() + canvas->sync().
    void flushDeferredMeshes();

    /// Clear deferred mesh list (call before each frame's draw phase).
    void clearDeferredMeshes();

    const rive::Mat2D& currentTransform() const { return m_transform; }

private:
    /// Apply current clip to a ThorVG shape (creates a clone of the clip shape).
    void applyClip(tvg::Shape* shape);

    tvg::Canvas* m_canvas;
    rive::Mat2D m_transform;
    float m_opacity = 1.0f;
    int m_width = 0;
    int m_height = 0;
    uint32_t* m_stagingBuffer = nullptr;
    int m_stagingStride = 0;

    struct DeferredMesh {
        uint32_t* pixels;  // owned, meshAlloc'd
        int bx, by, bw, bh;
    };
    std::vector<DeferredMesh> m_deferredMeshes;

    /// Clip state: transformed clip shape for the current save level.
    /// nullptr means no clip active.
    tvg::Shape* m_clipShape = nullptr;

    struct SavedState {
        rive::Mat2D transform;
        float opacity;
        tvg::Shape* clipShape;  // not owned — just a pointer to track
    };
    std::stack<SavedState> m_savedStates;
};

} // namespace rive_thorvg

#endif // TVG_RENDERER_HPP
