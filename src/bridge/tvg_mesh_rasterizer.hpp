#ifndef TVG_MESH_RASTERIZER_HPP
#define TVG_MESH_RASTERIZER_HPP

#include <cstdint>
#include <cstddef>

#include <rive/shapes/paint/image_sampler.hpp>

namespace rive_thorvg {

/// Software triangle rasterizer for mesh deformation.
///
/// Renders textured triangles from mesh vertex/UV/index buffers directly
/// into a pixel buffer. Used by TvgRenderer::drawImageMesh().
///
/// This is a simple scanline rasterizer with affine texture mapping —
/// sufficient for the typical 50-200 triangle meshes found in Rive content.
class MeshRasterizer {
public:
    /// Rasterize a textured triangle mesh into the target buffer.
    ///
    /// @param targetBuffer   Destination pixel buffer (ARGB8888)
    /// @param targetWidth    Width of the target buffer in pixels
    /// @param targetHeight   Height of the target buffer in pixels
    /// @param targetStride   Stride of the target buffer in bytes per row
    /// @param texturePixels  Source texture (RGBA8888)
    /// @param textureWidth   Width of the source texture
    /// @param textureHeight  Height of the source texture
    /// @param vertices       Array of [x, y] vertex positions (screen space)
    /// @param uvCoords       Array of [u, v] texture coordinates (0-1 range)
    /// @param indices        Array of triangle indices (3 per triangle)
    /// @param vertexCount    Number of vertices
    /// @param indexCount     Number of indices (must be multiple of 3)
    /// @param opacity        Global opacity (0.0 - 1.0)
    static void rasterize(
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
        float opacity);

private:
    /// Rasterize a single textured triangle using scanline algorithm.
    static void rasterizeTriangle(
        uint32_t* targetBuffer,
        int targetWidth, int targetHeight, int targetStride,
        const uint8_t* texturePixels,
        int textureWidth, int textureHeight,
        rive::ImageSampler sampler,
        float x0, float y0, float u0, float v0,
        float x1, float y1, float u1, float v1,
        float x2, float y2, float u2, float v2,
        float opacity);

    /// Sample a texel from the texture at (u, v) coordinates.
    /// Performs nearest-neighbor sampling with clamping.
    static uint32_t sampleTexture(const uint8_t* texturePixels,
                                  int textureWidth, int textureHeight,
                                  float u, float v,
                                  rive::ImageSampler sampler);

    /// Blend a source pixel (ARGB) over a destination pixel (ARGB).
    static uint32_t blendPixel(uint32_t src, uint32_t dst, float opacity);
};

} // namespace rive_thorvg

#endif // TVG_MESH_RASTERIZER_HPP
