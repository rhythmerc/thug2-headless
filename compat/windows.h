// Linux stand-in for the Win32 API the decomp's core layers use. Threads, locks, semaphores and timers are real
// (POSIX underneath, in win_shim.cpp); the registry, INI files, message boxes and window calls answer "nothing
// there", which is what a headless run wants. Only what the core includes is here; it grows as the harness does.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "msvc_crt.h"

using BYTE = std::uint8_t;
using WORD = std::uint16_t;
using DWORD = std::uint32_t;
using UINT = unsigned int;
using LONG = std::int32_t;
using ULONG = std::uint32_t;
using BOOL = int;
using LONG_PTR = std::intptr_t;
using ULONG_PTR = std::uintptr_t;
using DWORD_PTR = std::uintptr_t;
using SIZE_T = std::size_t;
using LPVOID = void*;
using PVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPDWORD = DWORD*;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using HRESULT = std::int32_t;
using LONGLONG = std::int64_t;
using ULONGLONG = std::uint64_t;
using HANDLE = void*;
struct HWND__;
using HWND = HWND__*;
struct HINSTANCE__;
using HINSTANCE = HINSTANCE__*;
using HMODULE = HINSTANCE;
struct HKEY__;
using HKEY = HKEY__*;
using PHKEY = HKEY*;
using REGSAM = DWORD;
using LSTATUS = LONG;
using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
// (the decomp's thread entry points are declared `unsigned long __stdcall f(void*)`, as Win32 has them)
using LPTHREAD_START_ROUTINE = unsigned long (*)(LPVOID);

#define WINAPI
#define CALLBACK
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define INFINITE 0xFFFFFFFFu
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)))
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_IO_PENDING 997L
#define ERROR_HANDLE_EOF 38L
#define CREATE_SUSPENDED 0x4u
#define VER_PLATFORM_WIN32_NT 2u
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define TIMER_ALL_ACCESS 0x1F0003u
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2u
#define CP_ACP 0u
#define CP_UTF8 65001u
#define _TRUNCATE (static_cast<std::size_t>(-1))
#define MB_OK 0u
#define MB_ICONERROR 0x10u
#define MB_ICONWARNING 0x30u
#define MB_YESNO 0x4u
#define IDYES 6
#define IDNO 7

#define HKEY_CURRENT_USER (reinterpret_cast<HKEY>(static_cast<std::uintptr_t>(0x80000001u)))
#define HKEY_LOCAL_MACHINE (reinterpret_cast<HKEY>(static_cast<std::uintptr_t>(0x80000002u)))
#define KEY_READ 0x20019u
#define KEY_WRITE 0x20006u
#define KEY_QUERY_VALUE 0x1u
#define KEY_SET_VALUE 0x2u
#define REG_SZ 1u
#define REG_BINARY 3u
#define REG_DWORD 4u
#define REG_OPTION_NON_VOLATILE 0u

#define SPI_GETSCREENSAVETIMEOUT 0x000Eu
#define SPI_SETSCREENSAVETIMEOUT 0x000Fu
#define MAPVK_VK_TO_VSC 0u
#define MAPVK_VSC_TO_VK 1u
#define MAPVK_VK_TO_CHAR 2u
#define VK_ESCAPE 0x1B
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74

struct GUID {
    std::uint32_t Data1;
    std::uint16_t Data2;
    std::uint16_t Data3;
    std::uint8_t Data4[8];
};
inline bool operator==(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
};

struct OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
};
using LPOVERLAPPED = OVERLAPPED*;

struct SRWLOCK {
    void* Ptr;
};
#define SRWLOCK_INIT {nullptr}

struct OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    char szCSDVersion[128];
};

struct MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
};

#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_FREE 0x10000u
#define MEM_IMAGE 0x1000000u
#define MEM_MAPPED 0x40000u
#define MEM_PRIVATE 0x20000u
struct MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
};

struct MSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
};

// Threads, waits and time.
HANDLE create_thread_with(unsigned long (*call)(void* start, LPVOID parameter), void* start, LPVOID parameter, DWORD flags, DWORD* id);
// (any entry point: the decomp's are DWORD- or unsigned long-returning)
template <class R>
HANDLE CreateThread(void* /*attributes*/, SIZE_T /*stack*/, R (*start)(LPVOID), LPVOID parameter, DWORD flags, DWORD* id)
{
    return create_thread_with([](void* f, LPVOID p) -> unsigned long { return static_cast<unsigned long>(reinterpret_cast<R (*)(LPVOID)>(f)(p)); },
                              reinterpret_cast<void*>(start), parameter, flags, id);
}
DWORD ResumeThread(HANDLE thread);
BOOL SetThreadPriority(HANDLE thread, int priority);
HANDLE GetCurrentThread();
HANDLE GetCurrentProcess();
DWORD GetCurrentThreadId();
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
BOOL CloseHandle(HANDLE handle);
void Sleep(DWORD milliseconds);
DWORD GetTickCount();
BOOL QueryPerformanceCounter(LARGE_INTEGER* count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
HANDLE CreateSemaphoreA(void* attributes, LONG initial, LONG maximum, LPCSTR name);
BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG* previous);
HANDLE CreateMutexA(void* attributes, BOOL owned, LPCSTR name);
BOOL ReleaseMutex(HANDLE mutex);
HANDLE CreateEventA(void* attributes, BOOL manual_reset, BOOL initial, LPCSTR name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
HANDLE CreateWaitableTimerExW(void* attributes, const wchar_t* name, DWORD flags, DWORD access);
BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, LONG period, void* routine, LPVOID argument, BOOL resume);

using BOOLEAN = BYTE;
void InitializeSRWLock(SRWLOCK* lock);
void AcquireSRWLockExclusive(SRWLOCK* lock);
void ReleaseSRWLockExclusive(SRWLOCK* lock);
BOOLEAN TryAcquireSRWLockExclusive(SRWLOCK* lock);
void AcquireSRWLockShared(SRWLOCK* lock);
void ReleaseSRWLockShared(SRWLOCK* lock);

// The process and the system.
DWORD GetLastError();
void SetLastError(DWORD error);
void OutputDebugStringA(LPCSTR text);
int MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
[[noreturn]] void ExitProcess(UINT code);
BOOL GetVersionExA(OSVERSIONINFOA* info);
void GlobalMemoryStatus(MEMORYSTATUS* status);
SIZE_T VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* info, SIZE_T length);
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);
inline BOOL AllocConsole() { return TRUE; }
BOOL SystemParametersInfoA(UINT action, UINT param, PVOID value, UINT update);
UINT MapVirtualKeyA(UINT code, UINT type);
DWORD GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR buffer, DWORD size, LPCSTR file);
UINT GetPrivateProfileIntA(LPCSTR section, LPCSTR key, int fallback, LPCSTR file);
BOOL WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file);
int MultiByteToWideChar(UINT page, DWORD flags, LPCSTR text, int length, wchar_t* wide, int wide_length);
int WideCharToMultiByte(UINT page, DWORD flags, const wchar_t* wide, int wide_length, LPSTR text, int length, LPCSTR fallback, BOOL* used);
BOOL PeekMessageA(MSG* msg, HWND window, UINT first, UINT last, UINT remove);
BOOL TranslateMessage(const MSG* msg);
LRESULT DispatchMessageA(const MSG* msg);
#define PeekMessage PeekMessageA
#define DispatchMessage DispatchMessageA
#define PM_REMOVE 1u


// COM results.
#define S_OK 0L
#define S_FALSE 1L
#define E_FAIL (static_cast<HRESULT>(0x80004005L))
#define E_OUTOFMEMORY (static_cast<HRESULT>(0x8007000EL))
#define FAILED(hr) (static_cast<HRESULT>(hr) < 0)
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)

#define LOWORD(l) (static_cast<WORD>(static_cast<DWORD_PTR>(l) & 0xFFFF))
#define HIWORD(l) (static_cast<WORD>((static_cast<DWORD_PTR>(l) >> 16) & 0xFFFF))
#define MAKEINTRESOURCEA(i) (reinterpret_cast<LPSTR>(static_cast<ULONG_PTR>(static_cast<WORD>(i))))
#define ES_SYSTEM_REQUIRED 0x1u
#define ES_DISPLAY_REQUIRED 0x2u
#define ES_CONTINUOUS 0x80000000u
DWORD SetThreadExecutionState(DWORD flags);

// The command line (main's arguments, kept by the harness), libraries (none load) and INI sections.
LPSTR GetCommandLineA();
void set_command_line(int argc, char** argv);
HMODULE LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags);
HMODULE LoadLibraryA(LPCSTR name);
inline HMODULE GetModuleHandleA(LPCSTR) { return nullptr; }
void* GetProcAddress(HMODULE module, LPCSTR name);
BOOL FreeLibrary(HMODULE module);
UINT GetSystemDirectoryA(LPSTR buffer, UINT size);
DWORD GetPrivateProfileSectionA(LPCSTR section, LPSTR buffer, DWORD size, LPCSTR file);
DWORD GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);

// Files (POSIX underneath).
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1u
#define FILE_SHARE_WRITE 0x2u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_FLAG_OVERLAPPED 0x40000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, void* security, DWORD disposition, DWORD flags, HANDLE templ);
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD read, LPOVERLAPPED overlapped);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD bytes, LPDWORD written, LPOVERLAPPED overlapped);
DWORD GetFileSize(HANDLE file, LPDWORD high);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG* high, DWORD method);
BOOL GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD bytes, BOOL wait);
DWORD GetFileAttributesA(LPCSTR name);
BOOL CreateDirectoryA(LPCSTR name, void* security);
BOOL DeleteFileA(LPCSTR name);

// The registry (nothing is ever there).
LSTATUS RegOpenKeyExA(HKEY key, LPCSTR sub, DWORD options, REGSAM access, PHKEY result);
LSTATUS RegCreateKeyA(HKEY key, LPCSTR sub, PHKEY result);
LSTATUS RegCreateKeyExA(HKEY key, LPCSTR sub, DWORD reserved, LPSTR cls, DWORD options, REGSAM access, void* security, PHKEY result, LPDWORD disposition);
LSTATUS RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, BYTE* data, LPDWORD size);
LSTATUS RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size);
LSTATUS RegCloseKey(HKEY key);

