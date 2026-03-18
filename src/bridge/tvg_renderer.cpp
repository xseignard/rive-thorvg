#include "tvg_renderer.hpp"
#include "tvg_factory.hpp"
#include "tvg_mesh_rasterizer.hpp"

#include <cstring>
#include <cstdio>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace rive_thorvg {

static inline void* meshAlloc(size_t bytes) {
#ifdef ESP_PLATFORM
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
#else
    return std::malloc(bytes);
#endif
}

static inline void meshFree(void* ptr) {
    if (!ptr) return;
#ifdef ESP_PLATFORM
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
}

TvgRenderer::TvgRenderer(tvg::Canvas* canvas)
    : m_canvas(canvas) {}

void TvgRenderer::save() {
    m_savedStates.push({m_transform, m_opacity, m_clipShape});
}

void TvgRenderer::restore() {
    if (!m_savedStates.empty()) {
        auto& state = m_savedStates.top();
        m_transform = state.transform;
        m_opacity = state.opacity;

        // If we had a clip shape that was created at this save level
        // (different from the parent's), free it.
        if (m_clipShape && m_clipShape != state.clipShape) {
            m_clipShape->unref();
        }
        m_clipShape = state.clipShape;

        m_savedStates.pop();
    }
}

void TvgRenderer::transform(const rive::Mat2D& transform) {
    m_transform = m_transform * transform;
}

void TvgRenderer::applyClip(tvg::Shape* shape) {
    if (!m_clipShape) return;

    // Duplicate the clip shape so each paint gets its own clipper.
    // ThorVG requires each paint to have its own clip instance.
    auto* clipClone = static_cast<tvg::Shape*>(m_clipShape->duplicate());
    if (clipClone) {
        shape->clip(clipClone);
    }
}

void TvgRenderer::drawPath(rive::RenderPath* path, rive::RenderPaint* paint) {
    auto* tvgPath = static_cast<TvgRenderPath*>(path);
    auto* tvgPaint = static_cast<TvgRenderPaint*>(paint);

    if (!tvgPath || !tvgPaint) return;

    auto* shape = tvgPath->createTransformedShape(m_transform);
    if (!shape) return;

    tvgPaint->apply(shape);

    if (m_opacity < 1.0f) {
        shape->opacity(static_cast<uint8_t>(m_opacity * 255.0f));
    }

    // Apply current clip
    applyClip(shape);

    m_canvas->add(shape);  // Canvas takes ownership
}

void TvgRenderer::clipPath(rive::RenderPath* path) {
    auto* tvgPath = static_cast<TvgRenderPath*>(path);
    if (!tvgPath) return;

    // Create a transformed clip shape from the clip path.
    auto* clipShape = tvgPath->createTransformedShape(m_transform);
    if (!clipShape) return;

    // Fill the clip shape solid white — ThorVG clips based on the shape outline.
    clipShape->fill(255, 255, 255, 255);

    // If there's already a clip active, apply it to this new clip shape
    // to create the intersection. ThorVG supports clipping a clipper,
    // which effectively intersects the two clip regions.
    if (m_clipShape) {
        auto* parentClip = static_cast<tvg::Shape*>(m_clipShape->duplicate());
        if (parentClip) {
            clipShape->clip(parentClip);
        }
    }

    // Free old clip if it was created at this save level
    if (m_clipShape && !m_savedStates.empty() &&
        m_clipShape != m_savedStates.top().clipShape) {
        m_clipShape->unref();
    }

    // Ref so it survives being duplicated for each drawPath
    clipShape->ref();
    m_clipShape = clipShape;
}

/// Convert RGBA8888 pixels to ARGB8888 for ThorVG.
static void rgbaToArgb(const uint8_t* src, uint32_t* dst, int w, int h) {
    int count = w * h;
    for (int i = 0; i < count; ++i) {
        uint8_t r = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        dst[i] = (static_cast<uint32_t>(a) << 24) |
                 (static_cast<uint32_t>(r) << 16) |
                 (static_cast<uint32_t>(g) << 8) |
                 (static_cast<uint32_t>(b));
    }
}

void TvgRenderer::drawImage(const rive::RenderImage* image,
                            rive::ImageSampler sampler,
                            rive::BlendMode blendMode,
                            float opacity) {
    (void)sampler;
    (void)blendMode;

    auto* tvgImage = static_cast<const TvgRenderImage*>(image);
    if (!tvgImage || !tvgImage->pixels()) return;

    int w = image->width();
    int h = image->height();

    // Direct blit path: rasterize image into staging buffer using current
    // transform, bypassing ThorVG Picture. This avoids per-frame 3MB+ mallocs
    // and ThorVG compositing issues with large images on ESP32.
    if (m_stagingBuffer && m_stagingStride > 0) {
        const uint8_t* srcPixels = tvgImage->pixels();
        int pixelsPerRow = m_stagingStride;

        // For each output pixel in the canvas, compute source UV via inverse transform
        float det = m_transform[0] * m_transform[3] - m_transform[1] * m_transform[2];
        if (std::fabs(det) < 1e-6f) return;
        float invDet = 1.0f / det;
        float inv00 = m_transform[3] * invDet;
        float inv01 = -m_transform[2] * invDet;
        float inv10 = -m_transform[1] * invDet;
        float inv11 = m_transform[0] * invDet;
        float inv02 = -(inv00 * m_transform[4] + inv01 * m_transform[5]);
        float inv12 = -(inv10 * m_transform[4] + inv11 * m_transform[5]);

        // Compute output bounding box from transformed image corners
        float corners[4][2];
        for (int c = 0; c < 4; ++c) {
            float ix = (c & 1) ? (float)w : 0.0f;
            float iy = (c & 2) ? (float)h : 0.0f;
            corners[c][0] = m_transform[0] * ix + m_transform[2] * iy + m_transform[4];
            corners[c][1] = m_transform[1] * ix + m_transform[3] * iy + m_transform[5];
        }
        float minX = corners[0][0], maxX = corners[0][0];
        float minY = corners[0][1], maxY = corners[0][1];
        for (int c = 1; c < 4; ++c) {
            if (corners[c][0] < minX) minX = corners[c][0];
            if (corners[c][0] > maxX) maxX = corners[c][0];
            if (corners[c][1] < minY) minY = corners[c][1];
            if (corners[c][1] > maxY) maxY = corners[c][1];
        }

        int yStart = std::max(0, (int)std::floor(minY));
        int yEnd = std::min(m_height - 1, (int)std::ceil(maxY));
        int xStart = std::max(0, (int)std::floor(minX));
        int xEnd = std::min(m_width - 1, (int)std::ceil(maxX));

        float effectiveOpacity = m_opacity * opacity;

        DeferredMesh dm;
        dm.bx = xStart;
        dm.by = yStart;
        dm.bw = xEnd - xStart + 1;
        dm.bh = yEnd - yStart + 1;
        if (dm.bw <= 0 || dm.bh <= 0) return;

        dm.pixels = static_cast<uint32_t*>(meshAlloc(
            static_cast<size_t>(dm.bw) * dm.bh * sizeof(uint32_t)));
        if (!dm.pixels) return;
        std::memset(dm.pixels, 0, static_cast<size_t>(dm.bw) * dm.bh * sizeof(uint32_t));

        for (int dy = 0; dy < dm.bh; ++dy) {
            float py = (float)(dm.by + dy) + 0.5f;
            for (int dx = 0; dx < dm.bw; ++dx) {
                float px = (float)(dm.bx + dx) + 0.5f;
                float sx = inv00 * px + inv01 * py + inv02;
                float sy = inv10 * px + inv11 * py + inv12;
                int ix = (int)sx;
                int iy = (int)sy;
                if (ix < 0 || ix >= w || iy < 0 || iy >= h) continue;

                const uint8_t* p = srcPixels + (iy * w + ix) * 4;
                uint32_t r = p[0], g = p[1], b = p[2], a = p[3];
                a = (a * (uint32_t)(effectiveOpacity * 255.0f + 0.5f)) / 255;
                if (a == 0) continue;

                dm.pixels[dy * dm.bw + dx] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }

        m_deferredMeshes.push_back(dm);
        return;
    }

    // Fallback: ThorVG Picture path (desktop)
    auto* argbBuf = static_cast<uint32_t*>(meshAlloc(static_cast<size_t>(w) * h * 4));
    if (!argbBuf) return;
    rgbaToArgb(tvgImage->pixels(), argbBuf, w, h);

    auto* picture = tvg::Picture::gen();
    if (picture->load(argbBuf, w, h, tvg::ColorSpace::ARGB8888S, true) != tvg::Result::Success) {
        picture->unref();
        meshFree(argbBuf);
        return;
    }
    meshFree(argbBuf);

    tvg::Matrix mat;
    mat.e11 = m_transform[0]; mat.e12 = m_transform[2]; mat.e13 = m_transform[4];
    mat.e21 = m_transform[1]; mat.e22 = m_transform[3]; mat.e23 = m_transform[5];
    mat.e31 = 0.0f;           mat.e32 = 0.0f;           mat.e33 = 1.0f;
    picture->transform(mat);

    float effectiveOpacity = m_opacity * opacity;
    if (effectiveOpacity < 1.0f) {
        picture->opacity(static_cast<uint8_t>(effectiveOpacity * 255.0f));
    }

    if (m_clipShape) {
        auto* clipClone = static_cast<tvg::Shape*>(m_clipShape->duplicate());
        if (clipClone) picture->clip(clipClone);
    }

    m_canvas->add(picture);
}

void TvgRenderer::drawImageMesh(const rive::RenderImage* image,
                                rive::ImageSampler sampler,
                                rive::rcp<rive::RenderBuffer> vertices,
                                rive::rcp<rive::RenderBuffer> uvCoords,
                                rive::rcp<rive::RenderBuffer> indices,
                                uint32_t vertexCount,
                                uint32_t indexCount,
                                rive::BlendMode blendMode,
                                float opacity) {
    (void)blendMode;

    auto* tvgImage = static_cast<const TvgRenderImage*>(image);
    if (!tvgImage || !tvgImage->pixels()) return;
    if (!vertices || !uvCoords || !indices) return;
    if (vertexCount == 0 || indexCount == 0) return;

    int imgW = image->width();
    int imgH = image->height();
    if (m_width <= 0 || m_height <= 0) return;

    // Get vertex/UV/index data from render buffers
    auto* vtxBuf = static_cast<const TvgRenderBuffer*>(vertices.get());
    auto* uvBuf = static_cast<const TvgRenderBuffer*>(uvCoords.get());
    auto* idxBuf = static_cast<const TvgRenderBuffer*>(indices.get());

    const float* vtxData = static_cast<const float*>(vtxBuf->data());
    const float* uvData = static_cast<const float*>(uvBuf->data());
    const uint16_t* idxData = static_cast<const uint16_t*>(idxBuf->data());

    // Transform vertices to screen space and compute tight bounding box
    auto* transformedVerts = static_cast<float*>(meshAlloc(vertexCount * 2 * sizeof(float)));
    if (!transformedVerts) return;

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (uint32_t i = 0; i < vertexCount; ++i) {
        float x = vtxData[i * 2 + 0];
        float y = vtxData[i * 2 + 1];
        float tx = m_transform[0] * x + m_transform[2] * y + m_transform[4];
        float ty = m_transform[1] * x + m_transform[3] * y + m_transform[5];
        transformedVerts[i * 2 + 0] = tx;
        transformedVerts[i * 2 + 1] = ty;
        if (tx < minX) minX = tx;
        if (ty < minY) minY = ty;
        if (tx > maxX) maxX = tx;
        if (ty > maxY) maxY = ty;
    }

    // Tight bounding box clamped to canvas
    int bx = std::max(0, static_cast<int>(std::floor(minX)));
    int by = std::max(0, static_cast<int>(std::floor(minY)));
    int bx2 = std::min(m_width - 1, static_cast<int>(std::ceil(maxX)));
    int by2 = std::min(m_height - 1, static_cast<int>(std::ceil(maxY)));
    int bw = bx2 - bx + 1;
    int bh = by2 - by + 1;

    if (bw <= 0 || bh <= 0) {
        meshFree(transformedVerts);
        return;
    }

    // Offset vertices to bbox-local coordinates
    for (uint32_t i = 0; i < vertexCount; ++i) {
        transformedVerts[i * 2 + 0] -= static_cast<float>(bx);
        transformedVerts[i * 2 + 1] -= static_cast<float>(by);
    }

    // Rasterize mesh into tight bbox-sized buffer
    auto* meshBuffer = static_cast<uint32_t*>(meshAlloc(static_cast<size_t>(bw) * static_cast<size_t>(bh) * sizeof(uint32_t)));
    if (!meshBuffer) {
        meshFree(transformedVerts);
        return;
    }
    std::memset(meshBuffer, 0, static_cast<size_t>(bw) * static_cast<size_t>(bh) * sizeof(uint32_t));

    MeshRasterizer::rasterize(
        meshBuffer, bw, bh, bw * sizeof(uint32_t),
        tvgImage->pixels(), imgW, imgH,
        sampler,
        transformedVerts, uvData, idxData,
        vertexCount, indexCount,
        m_opacity * opacity);

    meshFree(transformedVerts);

    // Deferred path: store mesh buffer for blit after canvas->draw()/sync().
    if (m_stagingBuffer && m_stagingStride > 0) {
        m_deferredMeshes.push_back({meshBuffer, bx, by, bw, bh});
        return;
    }

    // Fallback: ThorVG Picture path (may not composite correctly in all cases)
    auto* picture = tvg::Picture::gen();
    if (picture->load(meshBuffer, bw, bh, tvg::ColorSpace::ARGB8888S, true) != tvg::Result::Success) {
        picture->unref();
        meshFree(meshBuffer);
        return;
    }
    meshFree(meshBuffer);

    tvg::Matrix mat;
    mat.e11 = 1.0f; mat.e12 = 0.0f; mat.e13 = static_cast<float>(bx);
    mat.e21 = 0.0f; mat.e22 = 1.0f; mat.e23 = static_cast<float>(by);
    mat.e31 = 0.0f; mat.e32 = 0.0f; mat.e33 = 1.0f;
    picture->transform(mat);

    if (m_clipShape) {
        auto* clipClone = static_cast<tvg::Shape*>(m_clipShape->duplicate());
        if (clipClone) picture->clip(clipClone);
    }

    m_canvas->add(picture);
}

void TvgRenderer::flushDeferredMeshes() {
    if (!m_stagingBuffer || m_stagingStride <= 0) return;

    int pixelsPerRow = m_stagingStride;
    for (auto& mesh : m_deferredMeshes) {
        for (int y = 0; y < mesh.bh; ++y) {
            int dy = mesh.by + y;
            if (dy < 0 || dy >= m_height) continue;
            for (int x = 0; x < mesh.bw; ++x) {
                int dx = mesh.bx + x;
                if (dx < 0 || dx >= m_width) continue;

                uint32_t src = mesh.pixels[y * mesh.bw + x];
                if (src == 0) continue;

                uint32_t sa = (src >> 24) & 0xFF;
                uint32_t sr = (src >> 16) & 0xFF;
                uint32_t sg = (src >> 8) & 0xFF;
                uint32_t sb = src & 0xFF;

                uint32_t& dst = m_stagingBuffer[dy * pixelsPerRow + dx];
                uint32_t da = (dst >> 24) & 0xFF;
                uint32_t dr = (dst >> 16) & 0xFF;
                uint32_t dg = (dst >> 8) & 0xFF;
                uint32_t db = dst & 0xFF;

                // Src-over blend (straight alpha)
                uint32_t inv_sa = 255 - sa;
                uint32_t oa = sa + (da * inv_sa + 127) / 255;
                uint32_t or_ = (sr * sa + dr * inv_sa + 127) / 255;
                uint32_t og = (sg * sa + dg * inv_sa + 127) / 255;
                uint32_t ob = (sb * sa + db * inv_sa + 127) / 255;

                dst = (oa << 24) | (or_ << 16) | (og << 8) | ob;
            }
        }
        meshFree(mesh.pixels);
    }
    m_deferredMeshes.clear();
}

void TvgRenderer::clearDeferredMeshes() {
    for (auto& mesh : m_deferredMeshes) {
        meshFree(mesh.pixels);
    }
    m_deferredMeshes.clear();
}

void TvgRenderer::modulateOpacity(float opacity) {
    m_opacity *= opacity;
}

} // namespace rive_thorvg
