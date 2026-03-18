set(RIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

if(NOT EXISTS "${RIVE_ROOT}/rive-runtime/include/rive/artboard.hpp")
    message(FATAL_ERROR "rive-runtime submodule not found at ${RIVE_ROOT}/rive-runtime")
endif()

if(NOT EXISTS "${RIVE_ROOT}/thorvg/inc/thorvg.h")
    message(FATAL_ERROR "thorvg submodule not found at ${RIVE_ROOT}/thorvg")
endif()

set(RIVE_BRIDGE_SRCS
    "${RIVE_ROOT}/src/rive_thorvg.cpp"
    "${RIVE_ROOT}/src/bridge/tvg_factory.cpp"
    "${RIVE_ROOT}/src/bridge/tvg_renderer.cpp"
    "${RIVE_ROOT}/src/bridge/tvg_mesh_rasterizer.cpp"
    "${RIVE_ROOT}/src/text/stub_text_engine.cpp"
    "${RIVE_ROOT}/third_party/stb_image_impl.c"
)

file(GLOB_RECURSE RIVE_RUNTIME_SRCS
    "${RIVE_ROOT}/rive-runtime/src/*.cpp"
)

set(RIVE_EXCLUDE_PATTERNS
    ".*/audio/.*"
    ".*/lua/.*"
    ".*/command_queue.*"
    ".*/command_server.*"
)

foreach(pattern ${RIVE_EXCLUDE_PATTERNS})
    list(FILTER RIVE_RUNTIME_SRCS EXCLUDE REGEX "${pattern}")
endforeach()

# Xtensa toolchain maps uint32_t to unsigned long, while upstream color.cpp
# returns unsigned int and triggers an invalid redeclaration on ESP-IDF.
list(FILTER RIVE_RUNTIME_SRCS EXCLUDE REGEX ".*/shapes/paint/color\\.cpp$")

if(EXISTS "${RIVE_ROOT}/rive-runtime/src/audio_event.cpp")
    list(APPEND RIVE_RUNTIME_SRCS "${RIVE_ROOT}/rive-runtime/src/audio_event.cpp")
endif()

file(GLOB THORVG_COMMON_SRCS "${RIVE_ROOT}/thorvg/src/common/*.cpp")
file(GLOB THORVG_RENDERER_SRCS "${RIVE_ROOT}/thorvg/src/renderer/*.cpp")
file(GLOB THORVG_SW_SRCS "${RIVE_ROOT}/thorvg/src/renderer/sw_engine/*.cpp")
file(GLOB THORVG_RAW_SRCS "${RIVE_ROOT}/thorvg/src/loaders/raw/*.cpp")
file(GLOB_RECURSE THORVG_WEBP_SRCS "${RIVE_ROOT}/thorvg/src/loaders/webp/*.cpp")
file(GLOB_RECURSE THORVG_PNG_SRCS "${RIVE_ROOT}/thorvg/src/loaders/png/*.cpp")
file(GLOB_RECURSE THORVG_JPG_SRCS "${RIVE_ROOT}/thorvg/src/loaders/jpg/*.cpp")

set(THORVG_SRCS
    ${THORVG_COMMON_SRCS}
    ${THORVG_RENDERER_SRCS}
    ${THORVG_SW_SRCS}
    ${THORVG_RAW_SRCS}
    ${THORVG_WEBP_SRCS}
    ${THORVG_PNG_SRCS}
    ${THORVG_JPG_SRCS}
)

set(COMPONENT_SRCS
    ${RIVE_BRIDGE_SRCS}
    ${RIVE_RUNTIME_SRCS}
    ${THORVG_SRCS}
    "${CMAKE_CURRENT_LIST_DIR}/patches/color.cpp"
)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS
        "${RIVE_ROOT}/include"
    PRIV_INCLUDE_DIRS
        "${RIVE_ROOT}/src"
        "${RIVE_ROOT}/rive-runtime/include"
        "${RIVE_ROOT}/third_party"
        "${RIVE_ROOT}/cmake"
        "${RIVE_ROOT}/thorvg/inc"
        "${RIVE_ROOT}/thorvg/src/common"
        "${RIVE_ROOT}/thorvg/src/renderer"
        "${RIVE_ROOT}/thorvg/src/renderer/sw_engine"
        "${RIVE_ROOT}/thorvg/src/loaders/raw"
        "${RIVE_ROOT}/thorvg/src/loaders/webp"
        "${RIVE_ROOT}/thorvg/src/loaders/webp/webp"
        "${RIVE_ROOT}/thorvg/src/loaders/webp/dec"
        "${RIVE_ROOT}/thorvg/src/loaders/webp/dsp"
        "${RIVE_ROOT}/thorvg/src/loaders/webp/utils"
        "${RIVE_ROOT}/thorvg/src/loaders/png"
        "${RIVE_ROOT}/thorvg/src/loaders/jpg"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2
    -ffunction-sections
    -fdata-sections
    -w
    -Wno-error
    -Wno-error=format
    -Wno-error=unknown-pragmas
    -Wno-unknown-pragmas
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    -Wno-unused-parameter
    -Wno-sign-compare
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    RIVE_THORVG
    _RIVE_INTERNAL_
)
