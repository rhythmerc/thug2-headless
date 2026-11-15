// fs::Disk on POSIX: the game's files by path, as fs_win32/win32_disk.cpp gives them on Windows. Windows paths are
// case-insensitive and use backslashes; the game's data is not consistent in either, so a path is resolved here part
// by part against what is on disk, ignoring case (each folder's listing kept once read; dropped when this disk
// changes that folder). An overlapped read completes at once at its offset (Win32 allows a read to complete at the
// call): the result is there when read() returns.
#include "backends/backends.h"

#include "engine/file/disk.h"

#include <windows.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs {
struct DiskFile {
    int fd = -1;
};
struct DiskStream {
    std::FILE* file = nullptr;
};
} // namespace fs

namespace headless {
namespace {

using fs::Access;
using fs::DiskFile;
using fs::DiskStream;
using fs::Disposition;
using fs::Origin;

std::mutex listing_mutex;
std::unordered_map<std::string, std::vector<std::string>> listings;

const std::vector<std::string>& listing(const std::string& folder)
{
    auto at = listings.find(folder);
    if (at != listings.end()) {
        return at->second;
    }
    std::vector<std::string> names;
    if (DIR* dir = opendir(folder.empty() ? "." : folder.c_str())) {
        while (dirent* entry = readdir(dir)) {
            names.emplace_back(entry->d_name);
        }
        closedir(dir);
    }
    return listings.emplace(folder, std::move(names)).first->second;
}

void forget_folder_of(const std::string& path)
{
    std::lock_guard<std::mutex> guard(listing_mutex);
    const auto slash = path.find_last_of('/');
    listings.erase(slash == std::string::npos ? std::string() : path.substr(0, slash));
}

// The game's folder (thug2_boot's), which relative paths are in. Empty: the working folder.
std::string& game_root()
{
    static std::string root;
    return root;
}

// `path` (Windows' or POSIX's spelling) as the file on disk: each part matched to an entry of its folder whatever
// its case; a part with no match is kept as given (a file about to be made).
std::string resolve(const char* path)
{
    std::string in = path ? path : "";
    for (char& c : in) {
        if (c == '\\') c = '/';
    }
    // (relative to the game folder - not the process's working folder, which belongs to the host)
    if (!in.empty() && in[0] != '/' && !game_root().empty()) {
        in = game_root() + "/" + in;
    }
    struct stat st {};
    if (::stat(in.c_str(), &st) == 0) {
        return in;
    }
    std::string out;
    std::size_t at = 0;
    if (!in.empty() && in[0] == '/') {
        out = "/";
        at = 1;
    }
    std::lock_guard<std::mutex> guard(listing_mutex);
    bool matching = true;
    while (at <= in.size()) {
        std::size_t end = in.find('/', at);
        if (end == std::string::npos) end = in.size();
        std::string part = in.substr(at, end - at);
        at = end + 1;
        if (part.empty() || part == ".") {
            continue;
        }
        const std::string folder = out.empty() || out == "/" ? out.substr(0, out.size() - (out == "/" ? 0 : 0)) : out;
        std::string chosen = part;
        if (matching && part != "..") {
            bool found = false;
            for (const std::string& name : listing(folder == "/" ? "/" : folder)) {
                if (name == part) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (const std::string& name : listing(folder == "/" ? "/" : folder)) {
                    if (strcasecmp(name.c_str(), part.c_str()) == 0) {
                        chosen = name;
                        found = true;
                        break;
                    }
                }
            }
            matching = found;
        }
        if (!out.empty() && out != "/") out += '/';
        out += chosen;
    }
    return out;
}

// The folder the harness keeps its own files in (thug2_set_cache_dir; empty: $XDG_CACHE_HOME or ~/.cache).
const std::string& cache_folder() { return cache_folder_path(); }

// The developer-only Default level ships without a scene or nodes (only its scripts and sky): its scene, texture
// dictionary and node file (levels\Default\Default.qb: the NodeArray) are answered with the sky's own - a scene of a
// few kilobytes and its one geometry node - so the level loads as a near-empty one the host's world is put into (in
// place of a stock level's megabytes). Only where the game folder has no file of its own.
// The sky's node file as Default's: its NodeArray (the sky's one geometry node) renamed Default_NodeArray - the name
// the game looks for in levels\\Default\\Default.qb - in a copy kept in the harness's cache folder.
std::string default_nodes_from(const std::string& sky_qb)
{
    constexpr uint32_t kNodeArray = 0xC472ECC5;        // NodeArray
    constexpr uint32_t kDefaultNodeArray = 0x5510C462; // Default_NodeArray
    const char* home = std::getenv("HOME");
    const std::string folder = !cache_folder().empty() ? cache_folder()
        : std::string(std::getenv("XDG_CACHE_HOME") ? std::getenv("XDG_CACHE_HOME") : (std::string(home ? home : ".") + "/.cache")) + "/thug2-headless";
    const std::string out = folder + "/Default.qb";
    struct stat st {};
    if (::stat(out.c_str(), &st) == 0) return out;
    std::FILE* in = std::fopen(sky_qb.c_str(), "rb");
    if (!in) return sky_qb;
    std::vector<uint8_t> bytes;
    for (int c; (c = std::fgetc(in)) != EOF;) bytes.push_back(static_cast<uint8_t>(c));
    std::fclose(in);
    // (the file starts with the global's name: a name token, 0x16, then its checksum)
    uint32_t first = 0;
    if (bytes.size() < 5 || bytes[0] != 0x16 || (std::memcpy(&first, &bytes[1], 4), first != kNodeArray)) return sky_qb;
    std::memcpy(&bytes[1], &kDefaultNodeArray, 4);
    std::filesystem::create_directories(folder);
    std::FILE* write = std::fopen(out.c_str(), "wb");
    if (!write) return sky_qb;
    std::fwrite(bytes.data(), 1, bytes.size(), write);
    std::fclose(write);
    return out;
}

// The developer-only Default level ships without a scene or nodes (only its scripts and sky): its scene, texture
// dictionary and collision are answered with the sky's own (a few kilobytes), and its node file with the sky's renamed
// (default_nodes_from), so the level loads as a near-empty one the host's world is put into - in place of a stock
// level's megabytes. Only where the game folder has no file of its own.
std::string with_stand_ins(std::string path)
{
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0) return path;
    std::string lower = path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* kind : {".scn.xbx", ".tex.xbx", ".col.xbx", ".qb"}) {
        const std::string want = std::string("levels/default/default") + kind;
        if (lower.size() >= want.size() && lower.compare(lower.size() - want.size(), want.size(), want) == 0) {
            const std::string folder = path.substr(0, path.size() - want.size());
            const std::string sky = resolve((folder + "levels/default_sky/default_Sky" + kind).c_str());
            return std::string(kind) == ".qb" ? default_nodes_from(sky) : sky;
        }
    }
    return path;
}

// Seconds since 1970 as the system's file time (100 ns ticks since 1601).
std::uint64_t ticks(std::time_t seconds) { return (static_cast<std::uint64_t>(seconds) + 11644473600ULL) * 10000000ULL; }

thread_local std::uint32_t error_code = 0;

// THUG2_TRACE_FILES=1: every file opened, as found on disk, to stderr (what a run needs of the game's data).
void trace(const std::string& real)
{
    static const bool on = std::getenv("THUG2_TRACE_FILES") != nullptr;
    if (on) std::fprintf(stderr, "OPEN %s\n", real.c_str());
}

// A handle that is no open file (a failed open's, or null): Win32's calls fail on those, they don't crash.
bool bad(DiskFile* file) { return file == nullptr || file == fs::no_file(); }

class PosixDisk final : public fs::Disk {
public:
    DiskFile* open(const char* path, fs::OpenSpec spec) override
    {
        int flags = spec.access == Access::kRead ? O_RDONLY : spec.access == Access::kWrite ? O_WRONLY : O_RDWR;
        switch (spec.disposition) {
        case Disposition::kOpenExisting:
            break;
        case Disposition::kCreateNew:
            flags |= O_CREAT | O_EXCL;
            break;
        case Disposition::kCreateAlways:
            flags |= O_CREAT | O_TRUNC;
            break;
        case Disposition::kOpenAlways:
            flags |= O_CREAT;
            break;
        }
        const std::string real = with_stand_ins(resolve(path));
        const int fd = ::open(real.c_str(), flags, 0644);
        if (fd < 0) {
            error_code = errno == ENOENT ? ERROR_FILE_NOT_FOUND : 5;
            return fs::no_file();
        }
        if ((flags & O_CREAT) != 0) forget_folder_of(real);
        trace(real);
        return new DiskFile{fd};
    }
    bool close(DiskFile* file) override
    {
        if (!file || file == fs::no_file()) return false;
        const bool done = ::close(file->fd) == 0;
        delete file;
        return done;
    }
    bool read(DiskFile* file, void* buffer, uint32_t length, uint32_t* bytes, void* overlapped) override
    {
        if (bad(file)) return false;
        auto* block = static_cast<OVERLAPPED*>(overlapped);
        const ssize_t n = block ? ::pread(file->fd, buffer, length, block->Offset) : ::read(file->fd, buffer, length);
        if (n < 0) {
            error_code = 5;
            return false;
        }
        if (bytes) *bytes = static_cast<uint32_t>(n);
        if (block) {
            block->Internal = 0;
            block->InternalHigh = static_cast<ULONG_PTR>(n);
            if (block->hEvent) SetEvent(block->hEvent);
        }
        return true;
    }
    bool write(DiskFile* file, const void* data, uint32_t length, uint32_t* written) override
    {
        if (bad(file)) return false;
        const ssize_t n = ::write(file->fd, data, length);
        if (written) *written = n < 0 ? 0 : static_cast<uint32_t>(n);
        return n >= 0;
    }
    uint32_t seek(DiskFile* file, int32_t offset, Origin origin) override
    {
        if (bad(file)) return 0xFFFFFFFFu;
        const int whence = origin == Origin::kBegin ? SEEK_SET : origin == Origin::kCurrent ? SEEK_CUR : SEEK_END;
        const off_t at = ::lseek(file->fd, offset, whence);
        return at < 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(at);
    }
    bool seek_to(DiskFile* file, uint64_t offset) override { return !bad(file) && ::lseek(file->fd, static_cast<off_t>(offset), SEEK_SET) >= 0; }
    uint32_t size(DiskFile* file) override
    {
        if (bad(file)) return 0xFFFFFFFFu;
        struct stat st {};
        return ::fstat(file->fd, &st) == 0 ? static_cast<uint32_t>(st.st_size) : 0xFFFFFFFFu;
    }
    bool size64(DiskFile* file, uint64_t* size) override
    {
        if (bad(file)) return false;
        struct stat st {};
        const bool known = ::fstat(file->fd, &st) == 0;
        *size = known ? static_cast<uint64_t>(st.st_size) : 0;
        return known;
    }
    bool flush(DiskFile* file) override { return !bad(file) && ::fsync(file->fd) == 0; }
    bool overlapped_result(DiskFile*, void* overlapped, uint32_t* bytes, bool) override
    {
        *bytes = static_cast<uint32_t>(static_cast<OVERLAPPED*>(overlapped)->InternalHigh);
        return true;
    }
    uint32_t last_error() override { return error_code; }

    DiskStream* open_stream(const char* path, const char* mode, fs::StreamSharing) override
    {
        const std::string real = with_stand_ins(resolve(path));
        std::FILE* file = std::fopen(real.c_str(), mode);
        if (!file) return nullptr;
        if (mode && (mode[0] == 'w' || mode[0] == 'a')) forget_folder_of(real);
        trace(real);
        return new DiskStream{file};
    }
    size_t stream_read(DiskStream* stream, void* buffer, size_t size, size_t count) override { return std::fread(buffer, size, count, stream->file); }
    size_t stream_write(DiskStream* stream, const void* data, size_t size, size_t count) override { return std::fwrite(data, size, count, stream->file); }
    int stream_seek(DiskStream* stream, long offset, int origin) override { return std::fseek(stream->file, offset, origin); }
    long stream_tell(DiskStream* stream) override { return std::ftell(stream->file); }
    int stream_flush(DiskStream* stream) override { return std::fflush(stream->file); }
    int stream_close(DiskStream* stream) override
    {
        const int result = std::fclose(stream->file);
        delete stream;
        return result;
    }

    fs::PathKind kind(const char* path) override
    {
        struct stat st {};
        if (::stat(with_stand_ins(resolve(path)).c_str(), &st) != 0) return fs::PathKind::kNone;
        return S_ISDIR(st.st_mode) ? fs::PathKind::kFolder : fs::PathKind::kFile;
    }
    bool stat(const char* path, fs::FileStat* out) override
    {
        struct stat st {};
        if (::stat(with_stand_ins(resolve(path)).c_str(), &st) != 0) return false;
        out->folder = S_ISDIR(st.st_mode);
        out->size = static_cast<uint64_t>(st.st_size);
        out->write_time = ticks(st.st_mtime);
        return true;
    }
    bool local_time(uint64_t write_time, fs::LocalTime* local) override
    {
        const std::time_t seconds = static_cast<std::time_t>(write_time / 10000000ULL - 11644473600ULL);
        std::tm tm {};
        if (!localtime_r(&seconds, &tm)) return false;
        *local = {.year = static_cast<uint16_t>(tm.tm_year + 1900), .month = static_cast<uint16_t>(tm.tm_mon + 1),
                  .day = static_cast<uint16_t>(tm.tm_mday), .hour = static_cast<uint16_t>(tm.tm_hour),
                  .minute = static_cast<uint16_t>(tm.tm_min), .second = static_cast<uint16_t>(tm.tm_sec)};
        return true;
    }
    bool make_folder(const char* path) override
    {
        const std::string real = resolve(path);
        forget_folder_of(real);
        return ::mkdir(real.c_str(), 0755) == 0;
    }
    bool make_folders(const char* path) override
    {
        const std::string real = resolve(path);
        std::error_code error;
        const bool made = std::filesystem::create_directories(real, error);
        std::lock_guard<std::mutex> guard(listing_mutex);
        listings.clear();
        return made;
    }
    bool remove(const char* path) override
    {
        const std::string real = resolve(path);
        forget_folder_of(real);
        return ::unlink(real.c_str()) == 0;
    }
    bool replace(const char* from, const char* to, bool) override
    {
        const std::string a = resolve(from);
        const std::string b = resolve(to);
        forget_folder_of(a);
        forget_folder_of(b);
        return ::rename(a.c_str(), b.c_str()) == 0;
    }
    bool list(const char* pattern, fs::FoundVisitor& visitor) override
    {
        // The folder resolved; the last part a wildcard ("*.pre"), matched ignoring case as Windows does.
        std::string in = pattern ? pattern : "";
        for (char& c : in) {
            if (c == '\\') c = '/';
        }
        const auto slash = in.find_last_of('/');
        const std::string folder = slash == std::string::npos ? std::string() : resolve(in.substr(0, slash).c_str());
        const std::string wild = slash == std::string::npos ? in : in.substr(slash + 1);
        DIR* dir = opendir(folder.empty() ? (game_root().empty() ? "." : game_root().c_str()) : folder.c_str());
        if (!dir) return false;
        bool any = false;
        while (dirent* entry = readdir(dir)) {
            if (fnmatch(wild.c_str(), entry->d_name, FNM_CASEFOLD) != 0) continue;
            const std::string full = folder.empty() ? entry->d_name : folder + "/" + entry->d_name;
            struct stat st {};
            ::stat(full.c_str(), &st);
            any = true;
            const fs::Found found{.name = entry->d_name, .folder = S_ISDIR(st.st_mode), .size_low = static_cast<uint32_t>(st.st_size), .write_time = ticks(st.st_mtime)};
            if (!visitor.found(found)) break;
        }
        closedir(dir);
        return any;
    }
    bool free_space(uint64_t* available) override
    {
        struct statvfs vfs {};
        if (::statvfs(game_root().empty() ? "." : game_root().c_str(), &vfs) != 0) return false;
        *available = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
        return true;
    }
};

} // namespace

void set_game_folder(const char* folder) { game_root() = folder ? folder : ""; }

fs::Disk& posix_disk()
{
    static PosixDisk disk;
    return disk;
}

} // namespace headless
