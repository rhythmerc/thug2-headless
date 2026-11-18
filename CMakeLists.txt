# The THUG2 decomp's core (base, formats, wire, engine, script, game, render_pc, the app's start-up) built headless on
# Linux, with the harness's own backends: what the go/no-go spike for a THUG2 engine in buildAR runs.
cmake_minimum_required(VERSION 3.27)
project(thug2_headless LANGUAGES C CXX)
# (optimised by default: the decomp's core leaves calls into parts not built here - the profiler, the script debugger,
# SDL's window - in code that only an optimised build drops as dead)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type" FORCE)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Third-party code, at the versions (and checksums) the decomp's own CMake pins: downloaded at configure time, or
# taken from deps/ when a copy is there (an offline build).
include(FetchContent)
set(DEPS ${CMAKE_SOURCE_DIR}/deps)
# (sources only: nothing of theirs is configured; FetchContent_MakeAvailable would add a CMakeLists - SOURCE_SUBDIR
# pointed at a folder without one keeps it from that)
set(FETCHCONTENT_QUIET ON)
macro(thug2_dep_sources name folder url hash)
    if(EXISTS ${DEPS}/${folder})
        set(${name} ${DEPS}/${folder})
    else()
        FetchContent_Declare(${name} URL ${url} URL_HASH SHA256=${hash} DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR no-cmake-here)
        FetchContent_MakeAvailable(${name})
        string(TOLOWER ${name} lower)
        set(${name} ${${lower}_SOURCE_DIR})
    endif()
endmacro()
thug2_dep_sources(IMGUI imgui-1.91.9b-docking
    https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
    466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5)
thug2_dep_sources(GSL GSL-4.2.0 https://github.com/microsoft/GSL/archive/refs/tags/v4.2.0.tar.gz
    2c717545a073649126cb99ebd493fa2ae23120077968795d2c69cbab821e4ac6)
thug2_dep_sources(LZHAM lzham_codec-d379b1f9121e2197881c61cfc4713c78848bdfe7
    https://github.com/richgel999/lzham_codec/archive/d379b1f9121e2197881c61cfc4713c78848bdfe7.tar.gz
    34931869697f2afb6d5880438ca313f61ddb6cb4c8fcea947cad1aaaf88dd8c2)
thug2_dep_sources(STB stb https://github.com/nothings/stb/archive/1ee679ca2ef753a528db5ba6801e1067b40481b8.tar.gz
    be9842d207353c89c0b970d618030fe98a944ab3fe1374534cba1b8e934bfdd7)
# (SDL3's headers only: its calls are stubbed, backends/sdl_stubs.cpp)
thug2_dep_sources(SDL3 SDL3-3.2.30 https://github.com/libsdl-org/SDL/releases/download/release-3.2.30/SDL3-3.2.30.tar.gz
    4c3b09330d866dc52eb65b66259a6684ad387252ca8c57901b3a2b534eb42e3d)

# The decomp's checkout (github.com/CanuckAussieKev/thug2-decomp, with the linux-headless patches).
# (a checkout beside this one, else the `decomp` submodule: the revision this one is pinned to)
if(EXISTS "${CMAKE_SOURCE_DIR}/../thug2-decomp/src")
    set(THUG2_DECOMP_DEFAULT "${CMAKE_SOURCE_DIR}/../thug2-decomp")
else()
    set(THUG2_DECOMP_DEFAULT "${CMAKE_SOURCE_DIR}/decomp")
endif()
set(THUG2_DECOMP_DIR "${THUG2_DECOMP_DEFAULT}" CACHE PATH "the thug2-decomp checkout")
# The relit mode's per-level table, made a header from the decomp's JSON (as its src/engine/CMakeLists.txt does).
set(GENERATED ${CMAKE_BINARY_DIR}/generated)
file(READ ${THUG2_DECOMP_DIR}/src/engine/gfx/rt_relit_levels.json RELIT_LEVELS_JSON)
file(WRITE ${GENERATED}/rt_relit_levels.h.new "#pragma once
// Generated from src/engine/gfx/rt_relit_levels.json (as the decomp's src/engine/CMakeLists.txt does).
#include <string_view>
namespace render::rt {
inline constexpr std::string_view kRelitLevelsJson = R\"json(${RELIT_LEVELS_JSON})json\";
} // namespace render::rt
")
file(COPY_FILE ${GENERATED}/rt_relit_levels.h.new ${GENERATED}/rt_relit_levels.h ONLY_IF_DIFFERENT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${THUG2_DECOMP_DIR}/src/engine/gfx/rt_relit_levels.json)
file(STRINGS ${CMAKE_SOURCE_DIR}/build_sources.txt CORE_SOURCES_RELATIVE)
list(TRANSFORM CORE_SOURCES_RELATIVE PREPEND "${THUG2_DECOMP_DIR}/" OUTPUT_VARIABLE CORE_SOURCES)
# (with what the core itself calls that Windows gave it - Win32, SDL, GameSpy, the platform layer - so it links in
# any order)
add_library(thug2_core STATIC ${CORE_SOURCES}
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    compat/win_shim.cpp backends/platform_linux.cpp backends/gamespy_stubs.cpp backends/sdl_stubs.cpp)
target_include_directories(thug2_core PUBLIC
    ${THUG2_DECOMP_DIR}/src ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/compat ${CMAKE_SOURCE_DIR}/compat/generated
    ${GENERATED} ${SDL3}/include ${IMGUI} ${GSL}/include ${LZHAM}/include)
target_compile_definitions(thug2_core PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEFAULT_FONT)
target_compile_options(thug2_core PUBLIC -include ${CMAKE_SOURCE_DIR}/compat/msvc_crt.h -w -fno-strict-aliasing)

target_include_directories(thug2_core PUBLIC ${STB})

# LZHAM (THUGPRO's compression), as the decomp builds it - with POSIX threads in place of Win32's.
file(GLOB LZHAM_DECOMP_SOURCES ${LZHAM}/lzhamdecomp/*.cpp)
add_library(lzham STATIC ${LZHAM_DECOMP_SOURCES}
    ${LZHAM}/lzhamcomp/lzham_lzbase.cpp ${LZHAM}/lzhamcomp/lzham_lzcomp.cpp ${LZHAM}/lzhamcomp/lzham_lzcomp_internal.cpp
    ${LZHAM}/lzhamcomp/lzham_lzcomp_state.cpp ${LZHAM}/lzhamcomp/lzham_match_accel.cpp
    ${LZHAM}/lzhamcomp/lzham_pthreads_threading.cpp ${LZHAM}/lzhamlib/lzham_lib.cpp)
target_include_directories(lzham PUBLIC ${LZHAM}/include PRIVATE ${LZHAM}/lzhamdecomp ${LZHAM}/lzhamcomp)
target_compile_options(lzham PRIVATE -w "SHELL:-include stdint.h" "SHELL:-include limits.h")

# Only the engine (and what it needs) when a host builds it: THUG2_DRIVE=OFF.
option(THUG2_DRIVE "build thug2-drive" ON)

# The engine a host links (app/thug2_engine.h): the core, the headless backends and the lockstep turns.
add_library(thug2_engine STATIC app/engine.cpp app/probe.cpp app/skater_model.cpp world/col_writer.cpp
    backends/posix_disk.cpp backends/headless_gpu.cpp backends/capture_audio.cpp backends/capture_hud.cpp backends/scripted_input.cpp)
if(ANDROID)
    target_link_libraries(thug2_engine PUBLIC thug2_core lzham log m)
else()
    target_link_libraries(thug2_engine PUBLIC thug2_core lzham pthread dl m)
endif()

# A host on the command line: boots, steps with a scripted pad, logs the skater (app/drive_main.cpp).
if(THUG2_DRIVE)
    add_executable(thug2-drive app/drive_main.cpp)
    target_include_directories(thug2-drive PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(thug2-drive PRIVATE thug2_engine)
endif()
