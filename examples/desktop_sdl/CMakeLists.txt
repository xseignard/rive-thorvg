cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Desktop SDL example — primary development tool
# ---------------------------------------------------------------------------

find_package(SDL2 QUIET)

if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
endif()

if(NOT SDL2_FOUND)
    message(WARNING "SDL2 not found — desktop example will not be built. Install SDL2 via brew/apt.")
    return()
endif()

add_executable(rive_sdl_example main.c)

target_link_libraries(rive_sdl_example PRIVATE
    rive_thorvg
    ${SDL2_LIBRARIES}
)

target_include_directories(rive_sdl_example PRIVATE
    ${SDL2_INCLUDE_DIRS}
)

# On macOS, SDL2 needs Cocoa framework
if(APPLE)
    target_link_libraries(rive_sdl_example PRIVATE "-framework Cocoa")
endif()
