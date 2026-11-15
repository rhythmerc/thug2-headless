# The THUG2 decomp's core (base, formats, wire, engine, script, game, render_pc, the app's start-up) built headless on
# Linux, with the harness's own backends: what the go/no-go spike for a THUG2 engine in skate3-quest-ar runs.
cmake_minimum_required(VERSION 3.27)
project(thug2_headless LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DEPS ${CMAKE_SOURCE_DIR}/deps)
set(IMGUI ${DEPS}/imgui-1.91.9b-docking)

file(STRINGS ${CMAKE_SOURCE_DIR}/build_sources.txt CORE_SOURCES)
add_library(thug2_core STATIC ${CORE_SOURCES}
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp)
target_include_directories(thug2_core PUBLIC
    $ENV{HOME}/projects/thug2-decomp/src ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/compat ${CMAKE_SOURCE_DIR}/compat/generated
    ${CMAKE_SOURCE_DIR}/out/generated ${CMAKE_SOURCE_DIR}/out/sdl3-install/include ${IMGUI}
    ${DEPS}/GSL-4.2.0/include ${DEPS}/lzham_codec-d379b1f9121e2197881c61cfc4713c78848bdfe7/include)
target_compile_definitions(thug2_core PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEFAULT_FONT)
target_compile_options(thug2_core PUBLIC -include ${CMAKE_SOURCE_DIR}/compat/msvc_crt.h -w -fno-strict-aliasing)

target_include_directories(thug2_core PUBLIC ${DEPS}/stb)

# LZHAM (THUGPRO's compression), as the decomp builds it - with POSIX threads in place of Win32's.
set(LZHAM ${DEPS}/lzham_codec-d379b1f9121e2197881c61cfc4713c78848bdfe7)
file(GLOB LZHAM_DECOMP_SOURCES ${LZHAM}/lzhamdecomp/*.cpp)
add_library(lzham STATIC ${LZHAM_DECOMP_SOURCES}
    ${LZHAM}/lzhamcomp/lzham_lzbase.cpp ${LZHAM}/lzhamcomp/lzham_lzcomp.cpp ${LZHAM}/lzhamcomp/lzham_lzcomp_internal.cpp
    ${LZHAM}/lzhamcomp/lzham_lzcomp_state.cpp ${LZHAM}/lzhamcomp/lzham_match_accel.cpp
    ${LZHAM}/lzhamcomp/lzham_pthreads_threading.cpp ${LZHAM}/lzhamlib/lzham_lib.cpp)
target_include_directories(lzham PUBLIC ${LZHAM}/include PRIVATE ${LZHAM}/lzhamdecomp ${LZHAM}/lzhamcomp)
target_compile_options(lzham PRIVATE -w "SHELL:-include stdint.h" "SHELL:-include limits.h")

add_executable(thug2-headless app/headless_main.cpp compat/win_shim.cpp
    backends/posix_disk.cpp backends/headless_gpu.cpp backends/scripted_input.cpp
    backends/platform_linux.cpp backends/gamespy_stubs.cpp app/probe.cpp)
target_link_libraries(thug2-headless PRIVATE thug2_core lzham ${CMAKE_SOURCE_DIR}/out/sdl3-install/lib/libSDL3.a pthread dl m)
