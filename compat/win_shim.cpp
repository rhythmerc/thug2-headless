// The Win32 calls of compat/windows.h on POSIX: real threads, locks, semaphores, events, timers and files; the
// registry, INI files, libraries and windows answer "nothing there".
#include <windows.h>
#include "backends/backends.h"
#include <mmsystem.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

thread_local DWORD last_error = 0;

struct Handle {
    virtual ~Handle() = default;
    // WAIT_OBJECT_0 when signalled within `ms` (INFINITE: no limit), else WAIT_TIMEOUT.
    virtual DWORD wait(DWORD ms) = 0;
};

template <class Predicate>
DWORD wait_on(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, DWORD ms, Predicate ready)
{
    if (ms == INFINITE) {
        cv.wait(lock, ready);
        return WAIT_OBJECT_0;
    }
    return cv.wait_for(lock, std::chrono::milliseconds(ms), ready) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}

struct Thread final : Handle {
    unsigned long (*call)(void*, LPVOID) = nullptr;
    void* start = nullptr;
    LPVOID parameter = nullptr;
    std::mutex m;
    std::condition_variable cv;
    bool started = false;
    bool done = false;
    std::thread thread;
    void run()
    {
        std::lock_guard<std::mutex> guard(m);
        if (started) return;
        started = true;
        thread = std::thread([this] {
            call(start, parameter);
            std::lock_guard<std::mutex> finished(m);
            done = true;
            cv.notify_all();
        });
        thread.detach();
    }
    DWORD wait(DWORD ms) override
    {
        std::unique_lock<std::mutex> lock(m);
        return wait_on(cv, lock, ms, [this] { return done; });
    }
};

struct Semaphore final : Handle {
    std::mutex m;
    std::condition_variable cv;
    LONG count = 0;
    LONG maximum = 1;
    DWORD wait(DWORD ms) override
    {
        std::unique_lock<std::mutex> lock(m);
        DWORD r = wait_on(cv, lock, ms, [this] { return count > 0; });
        if (r == WAIT_OBJECT_0) --count;
        return r;
    }
};

struct Event final : Handle {
    std::mutex m;
    std::condition_variable cv;
    bool manual = false;
    bool set = false;
    DWORD wait(DWORD ms) override
    {
        std::unique_lock<std::mutex> lock(m);
        DWORD r = wait_on(cv, lock, ms, [this] { return set; });
        if (r == WAIT_OBJECT_0 && !manual) set = false;
        return r;
    }
};

struct Mutex final : Handle {
    std::recursive_timed_mutex m;
    DWORD wait(DWORD ms) override
    {
        if (ms == INFINITE) {
            m.lock();
            return WAIT_OBJECT_0;
        }
        return m.try_lock_for(std::chrono::milliseconds(ms)) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
};

struct Timer final : Handle {
    std::chrono::steady_clock::time_point due{};
    DWORD wait(DWORD ms) override
    {
        auto now = std::chrono::steady_clock::now();
        if (due > now) {
            auto left = due - now;
            if (ms != INFINITE && left > std::chrono::milliseconds(ms)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                return WAIT_TIMEOUT;
            }
            std::this_thread::sleep_for(left);
        }
        return WAIT_OBJECT_0;
    }
};

struct File final : Handle {
    int fd = -1;
    ~File() override
    {
        if (fd >= 0) close(fd);
    }
    DWORD wait(DWORD) override { return WAIT_OBJECT_0; }
};

Handle* as_handle(HANDLE h) { return static_cast<Handle*>(h); }

std::string command_line;

std::shared_mutex* srw(SRWLOCK* lock)
{
    auto* slot = reinterpret_cast<std::atomic<std::shared_mutex*>*>(&lock->Ptr);
    std::shared_mutex* m = slot->load(std::memory_order_acquire);
    if (!m) {
        auto* made = new std::shared_mutex();
        if (slot->compare_exchange_strong(m, made, std::memory_order_acq_rel)) {
            m = made;
        } else {
            delete made;
        }
    }
    return m;
}

// A Windows path ("levels\\x\\y.col") as a POSIX one.
std::string posix_path(LPCSTR name)
{
    std::string path = name ? name : "";
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return path;
}

} // namespace

HANDLE create_thread_with(unsigned long (*call)(void*, LPVOID), void* start, LPVOID parameter, DWORD flags, DWORD* id)
{
    auto* t = new Thread();
    t->call = call;
    t->start = start;
    t->parameter = parameter;
    static std::atomic<DWORD> next_id{1};
    if (id) *id = next_id++;
    if ((flags & CREATE_SUSPENDED) == 0) t->run();
    return t;
}

DWORD ResumeThread(HANDLE thread)
{
    if (auto* t = dynamic_cast<Thread*>(as_handle(thread))) t->run();
    return 1;
}
BOOL SetThreadPriority(HANDLE, int) { return TRUE; }
HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-2)); }
HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)); }
DWORD GetCurrentThreadId() { return static_cast<DWORD>(std::hash<std::thread::id>{}(std::this_thread::get_id())); }

DWORD WaitForSingleObject(HANDLE handle, DWORD ms)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) return WAIT_FAILED;
    return as_handle(handle)->wait(ms);
}

BOOL CloseHandle(HANDLE handle)
{
    // (a thread's object lives on: its detached thread may still be running)
    if (!handle || handle == INVALID_HANDLE_VALUE || dynamic_cast<Thread*>(as_handle(handle))) return TRUE;
    delete as_handle(handle);
    return TRUE;
}

void Sleep(DWORD ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

DWORD GetTickCount()
{
    using namespace std::chrono;
    return static_cast<DWORD>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
DWORD timeGetTime() { return GetTickCount(); }
UINT timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
UINT timeEndPeriod(UINT) { return TIMERR_NOERROR; }

BOOL QueryPerformanceCounter(LARGE_INTEGER* count)
{
    using namespace std::chrono;
    count->QuadPart = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return TRUE;
}
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency)
{
    frequency->QuadPart = 1000000000LL;
    return TRUE;
}

HANDLE CreateSemaphoreA(void*, LONG initial, LONG maximum, LPCSTR)
{
    auto* s = new Semaphore();
    s->count = initial;
    s->maximum = maximum;
    return s;
}
BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG* previous)
{
    auto* s = dynamic_cast<Semaphore*>(as_handle(semaphore));
    if (!s) return FALSE;
    std::lock_guard<std::mutex> guard(s->m);
    if (previous) *previous = s->count;
    s->count = std::min(s->count + count, s->maximum);
    s->cv.notify_all();
    return TRUE;
}

HANDLE CreateMutexA(void*, BOOL owned, LPCSTR)
{
    auto* m = new Mutex();
    if (owned) m->m.lock();
    return m;
}
BOOL ReleaseMutex(HANDLE mutex)
{
    auto* m = dynamic_cast<Mutex*>(as_handle(mutex));
    if (!m) return FALSE;
    m->m.unlock();
    return TRUE;
}

HANDLE CreateEventA(void*, BOOL manual_reset, BOOL initial, LPCSTR)
{
    auto* e = new Event();
    e->manual = manual_reset;
    e->set = initial;
    return e;
}
BOOL SetEvent(HANDLE event)
{
    auto* e = dynamic_cast<Event*>(as_handle(event));
    if (!e) return FALSE;
    std::lock_guard<std::mutex> guard(e->m);
    e->set = true;
    e->cv.notify_all();
    return TRUE;
}
BOOL ResetEvent(HANDLE event)
{
    auto* e = dynamic_cast<Event*>(as_handle(event));
    if (!e) return FALSE;
    std::lock_guard<std::mutex> guard(e->m);
    e->set = false;
    return TRUE;
}

HANDLE CreateWaitableTimerExW(void*, const wchar_t*, DWORD, DWORD) { return new Timer(); }
BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, LONG, void*, LPVOID, BOOL)
{
    auto* t = dynamic_cast<Timer*>(as_handle(timer));
    if (!t || !due) return FALSE;
    // (negative: relative, in 100 ns units - the only kind the game sets)
    const LONGLONG units = due->QuadPart < 0 ? -due->QuadPart : 0;
    t->due = std::chrono::steady_clock::now() + std::chrono::nanoseconds(units * 100);
    return TRUE;
}

void InitializeSRWLock(SRWLOCK* lock) { lock->Ptr = nullptr; }
void AcquireSRWLockExclusive(SRWLOCK* lock) { srw(lock)->lock(); }
void ReleaseSRWLockExclusive(SRWLOCK* lock) { srw(lock)->unlock(); }
BOOLEAN TryAcquireSRWLockExclusive(SRWLOCK* lock) { return srw(lock)->try_lock() ? 1 : 0; }
void AcquireSRWLockShared(SRWLOCK* lock) { srw(lock)->lock_shared(); }
void ReleaseSRWLockShared(SRWLOCK* lock) { srw(lock)->unlock_shared(); }

DWORD GetLastError() { return last_error; }
void SetLastError(DWORD error) { last_error = error; }
void OutputDebugStringA(LPCSTR text) { std::fputs(text ? text : "", stderr); }
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
    std::fprintf(stderr, "[message box] %s: %s\n", caption ? caption : "", text ? text : "");
    return IDNO;
}
void ExitProcess(UINT code) { std::_Exit(static_cast<int>(code)); }
BOOL GetVersionExA(OSVERSIONINFOA* info)
{
    info->dwMajorVersion = 10;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 19045;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    info->szCSDVersion[0] = '\0';
    return TRUE;
}
void GlobalMemoryStatus(MEMORYSTATUS* status)
{
    const SIZE_T total = static_cast<SIZE_T>(sysconf(_SC_PHYS_PAGES)) * static_cast<SIZE_T>(sysconf(_SC_PAGESIZE));
    const SIZE_T available = static_cast<SIZE_T>(sysconf(_SC_AVPHYS_PAGES)) * static_cast<SIZE_T>(sysconf(_SC_PAGESIZE));
    *status = MEMORYSTATUS{sizeof(MEMORYSTATUS), 50, total, available, total, available, total, available};
}
SIZE_T VirtualQuery(LPCVOID, MEMORY_BASIC_INFORMATION* info, SIZE_T)
{
    *info = {};
    return 0;
}
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size)
{
    const char* value = std::getenv(name);
    if (!value) return 0;
    const DWORD length = static_cast<DWORD>(std::strlen(value));
    if (buffer && size > length) std::memcpy(buffer, value, length + 1);
    return size > length ? length : length + 1;
}
BOOL SystemParametersInfoA(UINT, UINT, PVOID value, UINT)
{
    if (value) *static_cast<UINT*>(value) = 0;
    return TRUE;
}
UINT MapVirtualKeyA(UINT, UINT) { return 0; }

// INI files: the fallback (the harness writes no ini; settings come from defaults).
DWORD GetPrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR fallback, LPSTR buffer, DWORD size, LPCSTR)
{
    if (!buffer || size == 0) return 0;
    std::snprintf(buffer, size, "%s", fallback ? fallback : "");
    return static_cast<DWORD>(std::strlen(buffer));
}
UINT GetPrivateProfileIntA(LPCSTR, LPCSTR, int fallback, LPCSTR) { return static_cast<UINT>(fallback); }
BOOL WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR) { return TRUE; }
DWORD GetPrivateProfileSectionA(LPCSTR, LPSTR buffer, DWORD size, LPCSTR)
{
    if (buffer && size >= 2) buffer[0] = buffer[1] = '\0';
    return 0;
}

int MultiByteToWideChar(UINT, DWORD, LPCSTR text, int length, wchar_t* wide, int wide_length)
{
    const int n = length < 0 ? static_cast<int>(std::strlen(text)) + 1 : length;
    if (!wide || wide_length == 0) return n;
    int i = 0;
    for (; i < n && i < wide_length; ++i) wide[i] = static_cast<unsigned char>(text[i]);
    return i;
}
int WideCharToMultiByte(UINT, DWORD, const wchar_t* wide, int wide_length, LPSTR text, int length, LPCSTR, BOOL* used)
{
    if (used) *used = FALSE;
    const int n = wide_length < 0 ? static_cast<int>(std::wcslen(wide)) + 1 : wide_length;
    if (!text || length == 0) return n;
    int i = 0;
    for (; i < n && i < length; ++i) text[i] = wide[i] < 0x80 ? static_cast<char>(wide[i]) : '?';
    return i;
}

BOOL PeekMessageA(MSG*, HWND, UINT, UINT, UINT) { return FALSE; }
BOOL TranslateMessage(const MSG*) { return FALSE; }
LRESULT DispatchMessageA(const MSG*) { return 0; }
DWORD SetThreadExecutionState(DWORD) { return 0; }

void set_command_line(int argc, char** argv)
{
    command_line.clear();
    for (int i = 0; i < argc; ++i) {
        if (i) command_line += ' ';
        command_line += argv[i];
    }
}
LPSTR GetCommandLineA() { return command_line.data(); }
HMODULE LoadLibraryExA(LPCSTR, HANDLE, DWORD) { return nullptr; }
HMODULE LoadLibraryA(LPCSTR) { return nullptr; }
void* GetProcAddress(HMODULE, LPCSTR) { return nullptr; }
BOOL FreeLibrary(HMODULE) { return TRUE; }
UINT GetSystemDirectoryA(LPSTR buffer, UINT size)
{
    std::snprintf(buffer, size, "/usr/lib");
    return static_cast<UINT>(std::strlen(buffer));
}
DWORD GetModuleFileNameA(HMODULE, LPSTR buffer, DWORD size)
{
    // (the game keeps its log and settings beside its exe: in the harness's cache folder when a host set one)
    if (!headless::cache_folder_path().empty() && size > 0) {
        const int n = std::snprintf(buffer, size, "%s/thug2.exe", headless::cache_folder_path().c_str());
        return n > 0 && static_cast<DWORD>(n) < size ? static_cast<DWORD>(n) : 0;
    }
    const ssize_t n = readlink("/proc/self/exe", buffer, size ? size - 1 : 0);
    if (n <= 0) return 0;
    buffer[n] = '\0';
    return static_cast<DWORD>(n);
}

HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD, void*, DWORD disposition, DWORD, HANDLE)
{
    int flags = (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (disposition == CREATE_ALWAYS) flags |= O_CREAT | O_TRUNC;
    if (disposition == OPEN_ALWAYS) flags |= O_CREAT;
    const int fd = open(posix_path(name).c_str(), flags, 0644);
    if (fd < 0) {
        last_error = errno == ENOENT ? ERROR_FILE_NOT_FOUND : 5;
        return INVALID_HANDLE_VALUE;
    }
    auto* f = new File();
    f->fd = fd;
    return f;
}
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD read_bytes, LPOVERLAPPED overlapped)
{
    auto* f = dynamic_cast<File*>(as_handle(file));
    if (!f) return FALSE;
    // (an overlapped read is done at once, at its offset: the request is complete when this returns)
    const ssize_t n = overlapped ? pread(f->fd, buffer, bytes, overlapped->Offset) : read(f->fd, buffer, bytes);
    if (n < 0) return FALSE;
    if (read_bytes) *read_bytes = static_cast<DWORD>(n);
    if (overlapped) {
        overlapped->Internal = 0;
        overlapped->InternalHigh = static_cast<ULONG_PTR>(n);
        if (overlapped->hEvent) SetEvent(overlapped->hEvent);
    }
    return TRUE;
}
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD bytes, LPDWORD written, LPOVERLAPPED)
{
    auto* f = dynamic_cast<File*>(as_handle(file));
    if (!f) return FALSE;
    const ssize_t n = write(f->fd, buffer, bytes);
    if (n < 0) return FALSE;
    if (written) *written = static_cast<DWORD>(n);
    return TRUE;
}
DWORD GetFileSize(HANDLE file, LPDWORD high)
{
    auto* f = dynamic_cast<File*>(as_handle(file));
    struct stat st {};
    if (!f || fstat(f->fd, &st) != 0) return INVALID_FILE_SIZE;
    if (high) *high = static_cast<DWORD>(static_cast<std::uint64_t>(st.st_size) >> 32);
    return static_cast<DWORD>(st.st_size);
}
DWORD SetFilePointer(HANDLE file, LONG distance, LONG*, DWORD method)
{
    auto* f = dynamic_cast<File*>(as_handle(file));
    if (!f) return INVALID_SET_FILE_POINTER;
    const int whence = method == FILE_BEGIN ? SEEK_SET : method == FILE_CURRENT ? SEEK_CUR : SEEK_END;
    const off_t at = lseek(f->fd, distance, whence);
    return at < 0 ? INVALID_SET_FILE_POINTER : static_cast<DWORD>(at);
}
BOOL GetOverlappedResult(HANDLE, LPOVERLAPPED overlapped, LPDWORD bytes, BOOL)
{
    if (bytes) *bytes = static_cast<DWORD>(overlapped->InternalHigh);
    return TRUE;
}
DWORD GetFileAttributesA(LPCSTR name)
{
    struct stat st {};
    if (stat(posix_path(name).c_str(), &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
BOOL CreateDirectoryA(LPCSTR name, void*) { return mkdir(posix_path(name).c_str(), 0755) == 0 ? TRUE : FALSE; }
BOOL DeleteFileA(LPCSTR name) { return unlink(posix_path(name).c_str()) == 0 ? TRUE : FALSE; }

LSTATUS RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, PHKEY result)
{
    if (result) *result = nullptr;
    return ERROR_FILE_NOT_FOUND;
}
LSTATUS RegCreateKeyA(HKEY, LPCSTR, PHKEY result)
{
    if (result) *result = nullptr;
    return ERROR_FILE_NOT_FOUND;
}
LSTATUS RegCreateKeyExA(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, void*, PHKEY result, LPDWORD)
{
    if (result) *result = nullptr;
    return ERROR_FILE_NOT_FOUND;
}
LSTATUS RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, BYTE*, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LSTATUS RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD) { return ERROR_FILE_NOT_FOUND; }
LSTATUS RegCloseKey(HKEY) { return ERROR_SUCCESS; }
