// The platform layer's parts the decomp keeps in Windows-only files, headless on Linux: the frame profiler (off), the
// game window (none: 640x480 windowed, as the original starts), the editor's window placement (nothing to keep), the
// TypeScript scripts (off: only the game's QB scripts run) and the exe's added script commands (none).
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "app/extensions.h"
#include "platform/app_window.h"
#include "platform/frame_profile.h"
#include "platform/sdl_window.h"
#include "script_qb/ts_bridge.h"

namespace frame_profile {
int level() { return 0; }
int slot(const char*) { return 0; }
void add_ms(int, double) {}
Scope::Scope(const char*) {}
Scope::~Scope() = default;
void loop_begin() {}
void loop_end(bool, bool) {}
} // namespace frame_profile

namespace sys {
bool& get_windowed()
{
    static bool windowed = true;
    return windowed;
}
int& get_window_width()
{
    static int width = 640;
    return width;
}
int& get_window_height()
{
    static int height = 480;
    return height;
}
SDL_Window* sdl_window() { return nullptr; }
void keep_text_input() {}
} // namespace sys

namespace sys::app_window {
void save_placement() {}
} // namespace sys::app_window

namespace script::qb::ts {
void install() {}
bool enabled() { return false; }
void set_interrupt_budget(uint32_t) {}
void forget_file(uint32_t, bool) {}
void read_version_next(const std::string&, const std::string&) {}
void read_check_data_next(const std::string&) {}
std::optional<LoadedFile> loaded_file(uint32_t) { return std::nullopt; }
uint32_t begin(uint32_t, uint32_t, Struct*, uint32_t, std::string& error)
{
    error = "TypeScript scripts are off in the headless build";
    return 0;
}
Answer resume(uint32_t, Struct*, bool, Struct*) { return {.done = true}; }
void drop(uint32_t) {}
} // namespace script::qb::ts

namespace app {
void register_added_script_commands() {}
} // namespace app

// The crash handling (platform/crash.cpp: Windows' structured exceptions and minidumps): a headless run's crash is
// the debugger's, or the core file's.
void install_crash_handling(bool) {}

// stb_image_write's code, for the screenshot files (platform/screenshot_files.cpp).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
