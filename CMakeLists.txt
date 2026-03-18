cmake_minimum_required(VERSION 3.16)
project(rive_thorvg VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(RIVE_THORVG_LVGL       "Build LVGL adapter"                          OFF)
option(RIVE_THORVG_HARFBUZZ   "Full text shaping via Harfbuzz + SheenBidi"  OFF)
option(RIVE_THORVG_STB_TEXT   "Lightweight LTR text via stb_truetype"       OFF)
option(RIVE_THORVG_BUILD_SDL  "Build desktop SDL example"                   OFF)

# ---------------------------------------------------------------------------
# Submodule checks
# ---------------------------------------------------------------------------

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rive-runtime/include/rive/artboard.hpp")
    message(FATAL_ERROR
        "rive-runtime submodule not found.\n"
        "Run: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thorvg/inc/thorvg.h")
    message(FATAL_ERROR
        "ThorVG submodule not found.\n"
        "Run: git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# Build ThorVG SW engine from submodule
# ---------------------------------------------------------------------------

include(cmake/thorvg.cmake)

# ---------------------------------------------------------------------------
# Collect rive-runtime sources (trimmed)
# ---------------------------------------------------------------------------

file(GLOB_RECURSE RIVE_SRCS "rive-runtime/src/*.cpp")

# Exclude subsystems we don't need (CMake regex — use .* not *)
# Note: text/, layout/, scripted/ are kept because their types are registered
# in the generated core registry. The heavy deps (harfbuzz, yoga, luau) are
# behind #ifdef guards and won't be compiled/linked.
set(RIVE_EXCLUDE_PATTERNS
    ".*/audio/.*"
    ".*/lua/.*"
    ".*/command_queue.*"
    ".*/command_server.*"
)

# Note: data_bind/ and viewmodel/ are intentionally KEPT — they are
# central to modern Rive content and have zero external dependencies.

foreach(pattern ${RIVE_EXCLUDE_PATTERNS})
    list(FILTER RIVE_SRCS EXCLUDE REGEX "${pattern}")
endforeach()

# Explicitly re-include audio_event.cpp (callback stub, no miniaudio dep)
# The glob above may have excluded it — add it back if it exists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rive-runtime/src/audio_event.cpp")
    list(APPEND RIVE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/rive-runtime/src/audio_event.cpp")
endif()

# ---------------------------------------------------------------------------
# Bridge sources
# ---------------------------------------------------------------------------

set(BRIDGE_SRCS
    src/bridge/tvg_factory.cpp
    src/bridge/tvg_renderer.cpp
    src/bridge/tvg_mesh_rasterizer.cpp
    third_party/stb_image_impl.c
)

# ---------------------------------------------------------------------------
# Text engine selection
# ---------------------------------------------------------------------------

if(RIVE_THORVG_HARFBUZZ)
    # Tier 3: Full international text
    # TODO: Add harfbuzz font backend source
    message(STATUS "rive-thorvg: Text Tier 3 (Harfbuzz) — not yet implemented")
elseif(RIVE_THORVG_STB_TEXT)
    # Tier 1/2: Lightweight LTR text via stb_truetype
    set(TEXT_SRCS src/text/stb_text_engine.cpp)
    message(STATUS "rive-thorvg: Text Tier 1 (stb_truetype)")
else()
    # Tier 0: No text (default) — stub engine
    set(TEXT_SRCS src/text/stub_text_engine.cpp)
    message(STATUS "rive-thorvg: Text Tier 0 (stub — text invisible)")
endif()

# ---------------------------------------------------------------------------
# Core library target
# ---------------------------------------------------------------------------

add_library(rive_thorvg STATIC
    src/rive_thorvg.cpp
    ${RIVE_SRCS}
    ${BRIDGE_SRCS}
    ${TEXT_SRCS}
)

target_include_directories(rive_thorvg
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/rive-runtime/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        # ThorVG internal headers for image decoders
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/thorvg/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}/thorvg/src/renderer
        ${CMAKE_CURRENT_SOURCE_DIR}/thorvg/src/loaders/webp
        ${CMAKE_CURRENT_SOURCE_DIR}/thorvg/src/loaders/webp/webp
        ${CMAKE_CURRENT_SOURCE_DIR}/thorvg/src/loaders/png
        ${CMAKE_CURRENT_SOURCE_DIR}/thorvg/src/loaders/jpg
)

# Link ThorVG SW engine (built from submodule via cmake/thorvg.cmake)
target_link_libraries(rive_thorvg PUBLIC thorvg_sw)

# Compiler flags — optimized for embedded
target_compile_options(rive_thorvg PRIVATE
    -Os
    -fno-exceptions
    -fno-rtti
    -ffunction-sections
    -fdata-sections
)

target_compile_definitions(rive_thorvg PRIVATE
    RIVE_THORVG
    _RIVE_INTERNAL_               # Required for rive-runtime internal headers
    # WITH_RIVE_AUDIO is intentionally NOT defined — disables miniaudio engine
    # WITH_RIVE_TEXT is intentionally NOT defined unless Tier 3 is enabled
)

if(RIVE_THORVG_HARFBUZZ)
    target_compile_definitions(rive_thorvg PRIVATE WITH_RIVE_TEXT)
    target_link_libraries(rive_thorvg PRIVATE harfbuzz sheenbidi)
endif()

# ---------------------------------------------------------------------------
# Optional LVGL adapter
# ---------------------------------------------------------------------------

if(RIVE_THORVG_LVGL)
    target_sources(rive_thorvg PRIVATE src/adapters/lv_rive.c)
    # LVGL must be available as a target or via include paths
    if(TARGET lvgl)
        target_link_libraries(rive_thorvg PUBLIC lvgl)
    else()
        message(WARNING "rive-thorvg: LVGL target not found. Set LVGL include paths manually.")
    endif()
    message(STATUS "rive-thorvg: LVGL adapter enabled")
endif()

# ---------------------------------------------------------------------------
# Desktop SDL example
# ---------------------------------------------------------------------------

if(RIVE_THORVG_BUILD_SDL)
    add_subdirectory(examples/desktop_sdl)
endif()

# ---------------------------------------------------------------------------
# Smoke tests
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
    add_executable(rive_render_smoke_test tests/render_smoke.c)
    target_link_libraries(rive_render_smoke_test PRIVATE rive_thorvg)
    add_test(
        NAME render_smoke_loader
        COMMAND rive_render_smoke_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/loader.riv"
    )
endif()
