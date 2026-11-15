// The THUG2 engine behind app/thug2_engine.h: the decomp's app/main.cpp and sys::win_main with headless Linux backends
// in place of Windows' (POSIX files, an in-memory GPU under the real PC renderer, no sound, no movies, no network, the
// host's pad), then the game's own main (sys::game_main) - on a thread of its own, which takes turns with the host one
// frame at a time through the loop's pacing hook (Lockstep below).
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "app/command_table.h"
#include "app/pc_setup.h"
#include "app/startup.h"
#include "app/probe.h"
#include "app/thug2_engine.h"
#include "backends/backends.h"
#include "engine/audio/audio_device.h"
#include "engine/audio/music_player.h"
#include "engine/ext/extension.h"
#include "engine/file/disk.h"
#include "engine/file/file_sources.h"
#include "engine/file/file_system.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/ui_backend.h"
#include "engine/media/movie.h"
#include "engine/net/transport.h"
#include "engine/pad_input/input_source.h"
#include "engine/script/host.h"
#include "engine/sys/startup_init.h"
#include "engine/sys/editor_mode.h"
#include "game/commands/node_hooks.h"
#include "game/front_end/pad_hooks.h"
#include "game/front_end/start_level.h"
#include "game/online/transport_hooks.h"
#include "game/skate/render_hooks.h"
#include "game/skate/sys_hooks.h"
#include "game/world/object_hooks.h"
#include "platform/output_sinks.h"
#include "platform/process_flags.h"
#include "platform/run_log.h"
#include "render_pc/d3d9_device.h"
#include "render_pc/loading_console_panel.h"
#include "script_qb/qb_host.h"

namespace {

// The turns: the host sets `go`, the game's loop takes it at its top and runs a frame, and coming back to the top
// it sets `at_top` and waits for the next. Only one of the two runs at any time.
struct Turns {
    std::mutex mutex;
    std::condition_variable changed;
    bool go = false;
    bool at_top = false;
    bool ended = false;
    Thug2Pad pad{0x80, 0x80, 0x80, 0x80, 0};
    uint32_t frames = 0;
};

// (never destroyed: the game's thread may still be waiting on it when its host exits)
Turns& turns()
{
    static Turns* t = new Turns();
    return *t;
}

// The loop's pacing hook (engine/ext/extension.h): at the top of every loop the game waits here for its turn, then
// runs that loop flat out (no 60 Hz wait: the host keeps the time) and draws nothing (the host draws).
class Lockstep final : public ext::Extension {
public:
    bool loop_pace(ext::LoopPace* pace) override
    {
        Turns& t = turns();
        std::unique_lock<std::mutex> lock(t.mutex);
        t.at_top = true;
        t.changed.notify_all();
        t.changed.wait(lock, [&] { return t.go; });
        t.go = false;
        t.at_top = false;
        ++t.frames;
        pace->flat_out = true;
        pace->skip_draw = true;
        return true;
    }
};

Lockstep& lockstep()
{
    static Lockstep extension;
    return extension;
}

int run_game(int argc, char** argv)
{
    set_command_line(argc, argv);

    platform::install_output_sinks();
    log_open();
    get_log("thug2-headless");
    fs::install_disk(headless::posix_disk());
    fs::file_system().add(file::overlay_source());
    fs::file_system().add(file::archive_source());
    fs::file_system().add(file::data_folder_source());
    net::install_transport(headless::null_transport());
    script::install_host(script::qb::host());
    app::install_command_tables();
    sys::set_startup_command_line(GetCommandLineA());
    // (of the exe's extensions, only starting in a level: no front end, no dev tools, no editor)
    ext::add(lockstep());
    ext::add(frontend::begin_level::extension());
    online::install_transport_hooks();
    commands::install_node_hooks();
    objects::install_game_hooks();
    render::install_game_hooks();
    sio::install_game_hooks();
    sys::install_game_hooks();
    render::install_device(render::d3d9::device());
    render::install_gpu(headless::headless_gpu());
    render::install_gpu_context(headless::headless_gpu_context());
    render::install_ui_backend(headless::headless_ui_backend());
    audio::install_device(headless::null_audio_device());
    media::install_movie(headless::null_movie());
    input::add_source(headless::scripted_input());
    // (no loading console panel: it is an ImGui overlay, and headless draws nothing)
    sys::run_startup_initialisers();

    // sys::win_main's part that matters without a window: the PC set-up (settings, script patches, the event pump),
    // the sound output (none), then the game's main with the command line's words.
    // (no window to give it the focus: the game is active from the start, or its main loop idles)
    sys::set_app_active(true);
    sys::init_pc_setup();
    audio::set_sound_device(audio::device().open(nullptr));
    std::array<char*, 21> words{};
    int count = 0;
    for (int i = 0; i < argc && count < 20; ++i) {
        words[static_cast<size_t>(count++)] = argv[i];
    }
    return sys::game_main(count, words.data());
}

} // namespace

namespace headless {
Thug2Pad current_pad()
{
    // (read by the game's thread during its turn, while the host waits)
    return turns().pad;
}
} // namespace headless

extern "C" int thug2_boot(const char* game_dir, int argc, const char* const* argv)
{
    if (game_dir && chdir(game_dir) != 0) {
        std::fprintf(stderr, "thug2: can't go to %s\n", game_dir);
        return 1;
    }
    // (the game keeps pointers into its command line for its whole life)
    static std::vector<std::string> words;
    static std::vector<char*> pointers;
    words.assign(argv, argv + argc);
    pointers.clear();
    for (std::string& w : words) pointers.push_back(w.data());
    std::thread([] {
        run_game(static_cast<int>(pointers.size()), pointers.data());
        Turns& t = turns();
        std::lock_guard<std::mutex> lock(t.mutex);
        t.ended = true;
        t.changed.notify_all();
    }).detach();
    Turns& t = turns();
    std::unique_lock<std::mutex> lock(t.mutex);
    t.changed.wait(lock, [&] { return t.at_top || t.ended; });
    return t.ended ? 1 : 0;
}

extern "C" int thug2_step(const Thug2Pad* pad, Thug2Skater* skater)
{
    Turns& t = turns();
    {
        std::unique_lock<std::mutex> lock(t.mutex);
        if (t.ended) return 1;
        t.pad = pad ? *pad : Thug2Pad{0x80, 0x80, 0x80, 0x80, 0};
        t.go = true;
        t.changed.notify_all();
        t.changed.wait(lock, [&] { return (!t.go && t.at_top) || t.ended; });
        if (t.ended) return 1;
    }
    // (the game waits at the top of its loop: its state is still)
    if (skater) {
        headless::read_skater(skater);
        skater->frame = t.frames;
    }
    return 0;
}

extern "C" void thug2_quit(void)
{
    // (the game's own end calls ExitProcess, which would end its host too: it is left waiting for a turn instead)
    Turns& t = turns();
    std::lock_guard<std::mutex> lock(t.mutex);
    t.ended = true;
    t.changed.notify_all();
}
