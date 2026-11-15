// The harness's main: the decomp's app/main.cpp and sys::win_main with headless Linux backends in place of Windows'
// (POSIX files, an in-memory GPU under the real PC renderer, no sound, no movies, no network, the harness's pad),
// then the game's own main (sys::game_main). Run from the game folder (its Data folder), with the game's command line:
//     thug2-headless -level <name>
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

#include "app/command_table.h"
#include "app/pc_setup.h"
#include "app/startup.h"
#include "app/probe.h"
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

int main(int argc, char** argv)
{
    // The game folder: THUG2_GAME_DIR, else where it is run from.
    if (const char* game = std::getenv("THUG2_GAME_DIR")) {
        if (chdir(game) != 0) {
            std::fprintf(stderr, "thug2-headless: can't go to %s\n", game);
            return 1;
        }
    }
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
