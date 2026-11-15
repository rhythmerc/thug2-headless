# The THUG2 decomp's core (base, formats, wire, engine, script, game, render_pc) built headless on Linux, with the
# harness's own backends: what the go/no-go spike for a THUG2 engine in skate3-quest-ar runs.
cmake_minimum_required(VERSION 3.27)
project(thug2_headless LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DECOMP "$ENV{HOME}/projects/thug2-decomp" CACHE PATH "the decomp's checkout")
file(STRINGS ${CMAKE_SOURCE_DIR}/build_sources.txt CORE_SOURCES)
add_library(thug2_core STATIC ${CORE_SOURCES})
target_include_directories(thug2_core PUBLIC
    ${DECOMP}/src ${CMAKE_SOURCE_DIR}/compat
    ${CMAKE_SOURCE_DIR}/deps/GSL-4.2.0/include
    ${CMAKE_SOURCE_DIR}/deps/lzham_codec-d379b1f9121e2197881c61cfc4713c78848bdfe7/include)
target_compile_options(thug2_core PUBLIC -include ${CMAKE_SOURCE_DIR}/compat/msvc_crt.h -w -fno-strict-aliasing)
