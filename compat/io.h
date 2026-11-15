#pragma once
#include <cstdint>
#include <cstdio>
#include <unistd.h>
// The MSVC runtime's low-level I/O, as POSIX has it.
#define _fileno fileno
inline std::intptr_t _get_osfhandle(int fd) { return static_cast<std::intptr_t>(fd); }
#define _isatty isatty
#define _access access
#define _dup dup
#define _dup2 dup2
