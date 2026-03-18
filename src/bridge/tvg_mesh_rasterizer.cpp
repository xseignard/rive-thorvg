#include "tvg_mesh_rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rive_thorvg {

static inline float wrapCoord(float t, rive::ImageWrap wrap) {
    switch (wrap) {
        case rive::ImageWrap::repeat:
            return t - std::floor(t);
        case rive::ImageWrap::mirror: {
            float f = t - std::floor(t * 0.5f) * 2.0f;
            return (f <= 1.0f) ? f : (2.0f - f);
        }
        case rive::ImageWrap::clamp:
        default:
            return std::max(0.0f, std::min(1.0f, t));
    }
}

static inline uint32_t sampleTextureNearest(const uint8_t* texturePixels,
                                            int textureWidth,
                                            int textureHeight,
                                            float u,
                                            float v) {
    int tx = static_cast<int>(u * (textureWidth - 1));
    int ty = static_cast<int>(v * (textureHeight - 1));

    const uint8_t* pixel = texturePixels + (ty * textureWidth + tx) * 4;
    uint8_t r = pixel[0];
    uint8_t g = pixel[1];
    uint8_t b = pixel[2];
    uint8_t a = pixel[3];

    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b));
}

uint32_t MeshRasterizer::sampleTexture(const uint8_t* texturePixels,
                                       int textureWidth, int textureHeight,
                                       float u, float v,
                                       rive::ImageSampler sampler) {
    u = wrapCoord(u, sampler.wrapX);
    v = wrapCoord(v, sampler.wrapY);

    if (sampler.filter == rive::ImageFilter::nearest) {
        return sampleTextureNearest(texturePixels, textureWidth, textureHeight, u, v);
    }

    float tx = u * (textureWidth - 1);
    float ty = v * (textureHeight - 1);
    int x0 = static_cast<int>(std::floor(tx));
    int y0 = static_cast<int>(std::floor(ty));
    int x1 = std::min(x0 + 1, textureWidth - 1);
    int y1 = std::min(y0 + 1, textureHeight - 1);
    float fx = tx - static_cast<float>(x0);
    float fy = ty - static_cast<float>(y0);

    uint32_t c00 = sampleTextureNearest(texturePixels,
                                        textureWidth,
                                        textureHeight,
                                        static_cast<float>(x0) / (textureWidth - 1),
                                        static_cast<float>(y0) / (textureHeight - 1));
    uint32_t c10 = sampleTextureNearest(texturePixels,
                                        textureWidth,
                                        textureHeight,
                                        static_cast<float>(x1) / (textureWidth - 1),
                                        static_cast<float>(y0) / (textureHeight - 1));
    uint32_t c01 = sampleTextureNearest(texturePixels,
                                        textureWidth,
                                        textureHeight,
                                        static_cast<float>(x0) / (textureWidth - 1),
                                        static_cast<float>(y1) / (textureHeight - 1));
    uint32_t c11 = sampleTextureNearest(texturePixels,
                                        textureWidth,
                                        textureHeight,
                                        static_cast<float>(x1) / (textureWidth - 1),
                                        static_cast<float>(y1) / (textureHeight - 1));

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    auto ch = [&](uint32_t c, int shift) -> float { return static_cast<float>((c >> shift) & 0xFF); };

    float a0 = lerp(ch(c00, 24), ch(c10, 24), fx);
    float a1 = lerp(ch(c01, 24), ch(c11, 24), fx);
    float r0 = lerp(ch(c00, 16), ch(c10, 16), fx);
    float r1 = lerp(ch(c01, 16), ch(c11, 16), fx);
    float g0 = lerp(ch(c00, 8), ch(c10, 8), fx);
    float g1 = lerp(ch(c01, 8), ch(c11, 8), fx);
    float b0 = lerp(ch(c00, 0), ch(c10, 0), fx);
    float b1 = lerp(ch(c01, 0), ch(c11, 0), fx);

    uint32_t a = static_cast<uint32_t>(lerp(a0, a1, fy) + 0.5f);
    uint32_t r = static_cast<uint32_t>(lerp(r0, r1, fy) + 0.5f);
    uint32_t g = static_cast<uint32_t>(lerp(g0, g1, fy) + 0.5f);
    uint32_t b = static_cast<uint32_t>(lerp(b0, b1, fy) + 0.5f);

    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t MeshRasterizer::blendPixel(uint32_t src, uint32_t dst, float opacity) {
    // Output straight (un-premultiplied) alpha — matches ARGB8888S colorspace
    // used by ThorVG picture->load in drawImageMesh.
    uint32_t sa = (src >> 24) & 0xFF;
    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8) & 0xFF;
    uint32_t sb = (src >> 0) & 0xFF;

    // Apply opacity to alpha
    sa = (sa * static_cast<uint32_t>(opacity * 255.0f + 0.5f)) / 255;

    // Straight-alpha src-over blend against destination
    uint32_t da = (dst >> 24) & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = (dst >> 0) & 0xFF;

    uint32_t inv_sa = 255 - sa;
    uint32_t oa = sa + (da * inv_sa + 127) / 255;
    uint32_t div = (oa > 0) ? oa : 1;
    uint32_t or_ = (sr * sa + dr * da * inv_sa / 255 + 127) / div;
    uint32_t og = (sg * sa + dg * da * inv_sa / 255 + 127) / div;
    uint32_t ob = (sb * sa + db * da * inv_sa / 255 + 127) / div;

    if (or_ > 255) or_ = 255;
    if (og > 255) og = 255;
    if (ob > 255) ob = 255;

    return (oa << 24) | (or_ << 16) | (og << 8) | ob;
}

void MeshRasterizer::rasterizeTriangle(
    uint32_t* targetBuffer,
    int targetWidth, int targetHeight, int targetStride,
    const uint8_t* texturePixels,
    int textureWidth, int textureHeight,
    rive::ImageSampler sampler,
    float x0, float y0, float u0, float v0,
    float x1, float y1, float u1, float v1,
    float x2, float y2, float u2, float v2,
    float opacity)
{
    auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    };

    float area = edge(x0, y0, x1, y1, x2, y2);
    if (std::fabs(area) < 1e-6f) return;

    float minXf = std::min(x0, std::min(x1, x2));
    float minYf = std::min(y0, std::min(y1, y2));
    float maxXf = std::max(x0, std::max(x1, x2));
    float maxYf = std::max(y0, std::max(y1, y2));

    int xStart = std::max(0, static_cast<int>(std::floor(minXf)));
    int yStart = std::max(0, static_cast<int>(std::floor(minYf)));
    int xEnd = std::min(targetWidth - 1, static_cast<int>(std::ceil(maxXf)));
    int yEnd = std::min(targetHeight - 1, static_cast<int>(std::ceil(maxYf)));

    if (xStart > xEnd || yStart > yEnd) return;

    int pixelsPerRow = targetStride / static_cast<int>(sizeof(uint32_t));
    float invArea = 1.0f / area;
    bool positiveArea = area > 0.0f;

    for (int y = yStart; y <= yEnd; ++y) {
        float py = static_cast<float>(y) + 0.5f;
        for (int x = xStart; x <= xEnd; ++x) {
            float px = static_cast<float>(x) + 0.5f;

            float w0 = edge(x1, y1, x2, y2, px, py);
            float w1 = edge(x2, y2, x0, y0, px, py);
            float w2 = edge(x0, y0, x1, y1, px, py);

            bool inside = positiveArea ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                       : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (!inside) continue;

            float b0 = w0 * invArea;
            float b1 = w1 * invArea;
            float b2 = w2 * invArea;

            float u = b0 * u0 + b1 * u1 + b2 * u2;
            float v = b0 * v0 + b1 * v1 + b2 * v2;

            uint32_t texColor = sampleTexture(texturePixels, textureWidth, textureHeight, u, v, sampler);
            uint32_t& dst = targetBuffer[y * pixelsPerRow + x];
            dst = blendPixel(texColor, dst, opacity);
        }
    }
}

void MeshRasterizer::rasterize(
    uint32_t* targetBuffer,
    int targetWidth, int targetHeight, int targetStride,
    const uint8_t* texturePixels,
    int textureWidth, int textureHeight,
    rive::ImageSampler sampler,
    const float* vertices,
    const float* uvCoords,
    const uint16_t* indices,
    uint32_t vertexCount,
    uint32_t indexCount,
    float opacity)
{
    // Process triangles: 3 indices per triangle
    for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
        uint16_t i0 = indices[i + 0];
        uint16_t i1 = indices[i + 1];
        uint16_t i2 = indices[i + 2];

        // Bounds check
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue;
        }

        // Vertices are [x, y] pairs
        float x0 = vertices[i0 * 2 + 0], y0 = vertices[i0 * 2 + 1];
        float x1 = vertices[i1 * 2 + 0], y1 = vertices[i1 * 2 + 1];
        float x2 = vertices[i2 * 2 + 0], y2 = vertices[i2 * 2 + 1];

        // UVs are [u, v] pairs
        float u0 = uvCoords[i0 * 2 + 0], v0 = uvCoords[i0 * 2 + 1];
        float u1 = uvCoords[i1 * 2 + 0], v1 = uvCoords[i1 * 2 + 1];
        float u2 = uvCoords[i2 * 2 + 0], v2 = uvCoords[i2 * 2 + 1];

        rasterizeTriangle(
            targetBuffer, targetWidth, targetHeight, targetStride,
            texturePixels, textureWidth, textureHeight,
            sampler,
            x0, y0, u0, v0,
            x1, y1, u1, v1,
            x2, y2, u2, v2,
            opacity);
    }
}

} // namespace rive_thorvg
