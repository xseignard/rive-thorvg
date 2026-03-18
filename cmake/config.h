// ThorVG config.h for rive-thorvg
// Minimal SW-only rasterizer build — no loaders, no GL/WebGPU

#ifndef THORVG_CONFIG_H
#define THORVG_CONFIG_H

#define THORVG_SW_RASTER_SUPPORT 1
#define THORVG_VERSION_STRING "1.0.2"

// Partial rendering — skip unchanged regions (good for embedded)
#define THORVG_PARTIAL_RENDER_SUPPORT 1

// Threading — enable for dual-core targets, disable for single-core
// Controlled by CMake option
#ifdef RIVE_THORVG_THREADS
#define THORVG_THREAD_SUPPORT 1
#endif

// No file I/O — we feed data from memory buffers
// #define THORVG_FILE_IO_SUPPORT

// Image format loaders — needed for decoding embedded images in .riv files
// Rive uses WebP for most embedded images, PNG for some
#define THORVG_PNG_LOADER_SUPPORT 1
#define THORVG_WEBP_LOADER_SUPPORT 1
#define THORVG_JPG_LOADER_SUPPORT 1

// No other loaders needed
// #define THORVG_SVG_LOADER_SUPPORT
// #define THORVG_TTF_LOADER_SUPPORT
// #define THORVG_LOTTIE_LOADER_SUPPORT

// No GL/WebGPU engines — SW only
// #define THORVG_GL_RASTER_SUPPORT
// #define THORVG_WG_RASTER_SUPPORT

// No savers
// #define THORVG_GIF_SAVER_SUPPORT

// No logging by default (enable for debugging)
// #define THORVG_LOG_ENABLED

// SIMD — auto-detected by platform
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define THORVG_NEON_VECTOR_SUPPORT 1
#elif defined(__AVX__)
#define THORVG_AVX_VECTOR_SUPPORT 1
#endif

#endif // THORVG_CONFIG_H
