// What MSVC's compiler and C runtime give every file, for the decomp's core on Linux: the calling-convention
// keywords (meaningless on x86-64 / arm64) and the runtime's extra string functions. Forced into every file
// (-include) the way MSVC's own headers are always there.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stddef.h>
#include <strings.h>

#define __stdcall
#define __cdecl
#define __fastcall
#define __forceinline inline __attribute__((always_inline))
#define _TRUNCATE (static_cast<std::size_t>(-1))

// The MSVC C runtime's extras.
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, std::size_t n) { return strncasecmp(a, b, n); }
inline int _stricoll(const char* a, const char* b) { return strcasecmp(a, b); }
inline char* strtok_s(char* text, const char* delimiters, char** context) { return strtok_r(text, delimiters, context); }
inline char* _strupr(char* text)
{
    for (char* c = text; *c; ++c) *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    return text;
}
inline char* _strlwr(char* text)
{
    for (char* c = text; *c; ++c) *c = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    return text;
}
inline int strcpy_s(char* dest, std::size_t size, const char* src)
{
    if (!dest || size == 0) return 22;
    std::snprintf(dest, size, "%s", src ? src : "");
    return 0;
}
inline int strcat_s(char* dest, std::size_t size, const char* src)
{
    if (!dest || size == 0) return 22;
    std::size_t used = strnlen(dest, size);
    if (used < size) std::snprintf(dest + used, size - used, "%s", src ? src : "");
    return 0;
}
inline int strncpy_s(char* dest, std::size_t size, const char* src, std::size_t count)
{
    if (!dest || size == 0) return 22;
    std::size_t n = (count == _TRUNCATE) ? size - 1 : (count < size - 1 ? count : size - 1);
    std::size_t i = 0;
    for (; i < n && src && src[i]; ++i) dest[i] = src[i];
    dest[i] = '\0';
    return 0;
}
template <std::size_t N> int strcpy_s(char (&dest)[N], const char* src) { return strcpy_s(dest, N, src); }
template <std::size_t N> int strcat_s(char (&dest)[N], const char* src) { return strcat_s(dest, N, src); }
template <std::size_t N> int strncpy_s(char (&dest)[N], const char* src, std::size_t count) { return strncpy_s(dest, N, src, count); }
#define sprintf_s snprintf
#define _snprintf snprintf
#define _vsnprintf vsnprintf
