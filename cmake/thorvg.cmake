# cmake/thorvg.cmake — Builds ThorVG SW rasterizer from submodule source
#
# Creates target: thorvg_sw
# Provides: thorvg.h public header, software canvas, raw image loader
#
# This replaces ThorVG's meson build with a minimal CMake wrapper that
# only compiles the SW engine + raw loader — no SVG/Lottie/PNG/GL/WebGPU.

set(THORVG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thorvg")

if(NOT EXISTS "${THORVG_DIR}/inc/thorvg.h")
    message(FATAL_ERROR
        "ThorVG submodule not found at ${THORVG_DIR}.\n"
        "Run: git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# Source files — SW engine only + raw loader
# ---------------------------------------------------------------------------

# Common utilities
file(GLOB THORVG_COMMON_SRCS "${THORVG_DIR}/src/common/*.cpp")

# Core renderer
file(GLOB THORVG_RENDERER_SRCS "${THORVG_DIR}/src/renderer/*.cpp")

# SW (software) rasterizer engine
file(GLOB THORVG_SW_SRCS "${THORVG_DIR}/src/renderer/sw_engine/*.cpp")

# Raw image loader (loads raw pixel data as tvg::Picture — always included)
file(GLOB THORVG_RAW_LOADER_SRCS "${THORVG_DIR}/src/loaders/raw/*.cpp")

# WebP image loader (Rive uses WebP for most embedded images)
file(GLOB_RECURSE THORVG_WEBP_LOADER_SRCS "${THORVG_DIR}/src/loaders/webp/*.cpp")

# PNG image loader (built-in, uses miniz)
file(GLOB_RECURSE THORVG_PNG_LOADER_SRCS "${THORVG_DIR}/src/loaders/png/*.cpp")

# JPG image loader (built-in)
file(GLOB_RECURSE THORVG_JPG_LOADER_SRCS "${THORVG_DIR}/src/loaders/jpg/*.cpp")

# ---------------------------------------------------------------------------
# Static library target
# ---------------------------------------------------------------------------

add_library(thorvg_sw STATIC
    ${THORVG_COMMON_SRCS}
    ${THORVG_RENDERER_SRCS}
    ${THORVG_SW_SRCS}
    ${THORVG_RAW_LOADER_SRCS}
    ${THORVG_WEBP_LOADER_SRCS}
    ${THORVG_PNG_LOADER_SRCS}
    ${THORVG_JPG_LOADER_SRCS}
)

# Public include: thorvg.h
target_include_directories(thorvg_sw
    PUBLIC
        ${THORVG_DIR}/inc
    PRIVATE
        # config.h lives in cmake/ directory
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake
        # Internal headers
        ${THORVG_DIR}/src/common
        ${THORVG_DIR}/src/renderer
        ${THORVG_DIR}/src/renderer/sw_engine
        ${THORVG_DIR}/src/loaders/raw
        ${THORVG_DIR}/src/loaders/webp
        ${THORVG_DIR}/src/loaders/webp/webp
        ${THORVG_DIR}/src/loaders/webp/dec
        ${THORVG_DIR}/src/loaders/webp/dsp
        ${THORVG_DIR}/src/loaders/webp/utils
        ${THORVG_DIR}/src/loaders/png
        ${THORVG_DIR}/src/loaders/jpg
)

# C++14 minimum (ThorVG requirement)
target_compile_features(thorvg_sw PRIVATE cxx_std_14)

# Compiler flags — match the main library's embedded-friendly settings
target_compile_options(thorvg_sw PRIVATE
    -Os
    -fno-exceptions
    -fno-rtti
    -ffunction-sections
    -fdata-sections
    -Wno-unused-parameter
    -Wno-sign-compare
)

# Threading support (optional)
option(THORVG_THREADS "Enable ThorVG threading support" ON)
if(THORVG_THREADS)
    target_compile_definitions(thorvg_sw PRIVATE RIVE_THORVG_THREADS)
    find_package(Threads QUIET)
    if(Threads_FOUND)
        target_link_libraries(thorvg_sw PRIVATE Threads::Threads)
    endif()
endif()

message(STATUS "ThorVG: SW engine built from submodule (${THORVG_DIR})")
