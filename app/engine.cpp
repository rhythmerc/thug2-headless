// The THUG2 engine behind app/thug2_engine.h: the decomp's app/main.cpp and sys::win_main with headless Linux backends
// in place of Windows' (POSIX files, an in-memory GPU under the real PC renderer, no sound, no movies, no network, the
// host's pad), then the game's own main (sys::game_main) - on a thread of its own, which takes turns with the host one
// frame at a time through the loop's pacing hook (Lockstep below).
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <climits>
#include <pthread.h>
#include <cstdlib>
#include <unistd.h>

#include "app/command_table.h"
#include "app/pc_setup.h"
#include "app/startup.h"
#include "app/probe.h"
#include "app/thug2_engine.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/memory/mem.h"
#include "engine/objects/entity.h"
#include "formats/collision_format.h"
#include "game/rider/test_view.h"
#include "engine/objects/rail_manager.h"
#include "game/skate/skate_module.h"
#include "world/col_writer.h"
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
#include "engine/pad_input/sdl_input.h"
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
    // A world the host gave, waiting for the game's turn (and for the skater to be in the level).
    std::vector<headless::CollisionTriangle> world;
    float spawn[3] = {};
    float heading = 0.0F;
    bool has_spawn = false;
    bool world_pending = false;
    // A teleport the host asked for, waiting likewise.
    float teleport[3] = {};
    float teleport_heading = 0.0F;
    bool teleport_pending = false;
    // The host world's scene, once added (unloaded when another world replaces it).
    render::WorldScene* scene = nullptr;
    // The host's rails (each a run of points), waiting likewise.
    std::vector<std::vector<std::array<float, 3>>> rails;
    bool rails_pending = false;
};

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr uint32_t kWorldChecksum = 0xA5000000; // the host world's collision objects (any checksums not a level's)

// The host's world made a scene of collision beside the level's, and the skater put on it. On the game's thread,
// during its turn.
constexpr uint32_t kRailLabel = 0xA6000000; // the host rails' points' names (not a level node's: they have none)

// The host's rails in place of the level's: the skate module's rail manager emptied (its node array let go, so the
// grind code reads no level node for them) and given one rail point per point, each linked to the next with the box
// of their segment, as GrindRailHub::add_from_node_array makes them.
void apply_rails(Turns& t)
{
    if (objects::find_object(kSkater) == nullptr) return;
    t.rails_pending = false;
    objects::GrindRailHub* hub = modules::Skate::sole()->rail_manager().get();
    if (!hub) return;
    hub->tidy_up();
    size_t count = 0;
    for (auto& rail : t.rails) count += rail.size() >= 2 ? rail.size() : 0;
    if (count == 0) return;
    auto* points = static_cast<objects::GrindPoint*>(memory::alloc_block(static_cast<unsigned>(count * sizeof(objects::GrindPoint))));
    std::memset(static_cast<void*>(points), 0, count * sizeof(objects::GrindPoint));
    size_t at = 0;
    uint32_t label = kRailLabel;
    for (auto& rail : t.rails) {
        if (rail.size() < 2) continue;
        const size_t first = at;
        for (size_t i = 0; i < rail.size(); ++i) {
            objects::GrindPoint& p = points[at + i];
            p.at_point() = {rail[i][0], rail[i][1], rail[i][2], 1.0F};
            p.flag_bits() = objects::kRailFlagActive;
            p.set_label(++label);
            p.lower_limit() = p.at_point();
            p.upper_limit() = p.at_point();
        }
        for (size_t i = 0; i + 1 < rail.size(); ++i) {
            objects::GrindPoint& p = points[at + i];
            objects::GrindPoint& next = points[at + i + 1];
            const maths::Vec4& a = p.at_point();
            const maths::Vec4& b = next.at_point();
            p.set_succ(&next);
            next.last_value() = &p;
            p.lower_limit() = {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), 1.0F};
            p.upper_limit() = {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z), 1.0F};
        }
        at = first + rail.size();
    }
    hub->set_nodes(points);
    hub->set_num(static_cast<int>(count));
    get_log("thug2: host rails: %zu rails, %zu points", t.rails.size(), count);
}

void apply_world(Turns& t)
{
    if (objects::find_object(kSkater) == nullptr) return; // (not in the level yet: the next turn)
    t.world_pending = false;
    std::vector<uint8_t> file = headless::write_collision(t.world, kWorldChecksum);
    collision_format::CollisionHeader header;
    collision_format::CollisionLayout layout;
    if (!collision_format::lay_out_collision(file, &header, &layout) || !collision_format::check_collision_objects(file, header, layout)) {
        get_log("thug2: the host world's collision doesn't check out (%zu bytes) - not added", file.size());
        return;
    }
    // (the last world's scene let go of - its collision with it - before the new one is made)
    if (t.scene) {
        render::engine::unload_scene(t.scene, 1);
        t.scene = nullptr;
    }
    // (the game keeps reading the collision from these bytes: the scene's for its life)
    auto* data = static_cast<uint8_t*>(memory::alloc(static_cast<unsigned>(file.size())));
    std::memcpy(data, file.data(), file.size());
    t.scene = render::build_world_scene("HostWorld", nullptr, false);
    t.scene->set_builds_collision_grid(true);
    t.scene->add_collision_data(data, file.size(), 0);
    get_log("thug2: host world added: %zu triangles, %zu bytes of collision", t.world.size(), file.size());
    // (always put back on the board: nothing the skater held onto in the last world's collision is kept)
    frontend::test_view::Vec3 at{};
    std::string failed;
    if (t.has_spawn) {
        failed = frontend::test_view::jump_to({.x = t.spawn[0], .y = t.spawn[1], .z = t.spawn[2]}, true, t.heading, &at, true);
    } else if (auto* skater = static_cast<objects::GameEntity*>(objects::find_object(kSkater))) {
        const maths::Vec4& p = skater->at_point();
        failed = frontend::test_view::jump_to({.x = p.x, .y = p.y + 12.0F, .z = p.z}, false, 0.0F, &at, true);
    }
    get_log("thug2: skater %s", failed.empty() ? "put on the host world" : failed.c_str());
}

void apply_teleport(Turns& t)
{
    if (objects::find_object(kSkater) == nullptr) return;
    t.teleport_pending = false;
    frontend::test_view::Vec3 at{};
    const std::string failed = frontend::test_view::jump_to({.x = t.teleport[0], .y = t.teleport[1], .z = t.teleport[2]}, true, t.teleport_heading, &at, true);
    if (!failed.empty()) get_log("thug2: teleport: %s", failed.c_str());
}

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
    // (no intro movies, and no start-up pause on a loading screen nobody sees)
    bool skips_intro_movies() override { return true; }
    bool loop_pace(ext::LoopPace* pace) override
    {
        // (the frame just run's 2D, for a host that draws the HUD: before the host's turn)
        headless::hud_frame_done();
        Turns& t = turns();
        std::unique_lock<std::mutex> lock(t.mutex);
        t.at_top = true;
        t.changed.notify_all();
        t.changed.wait(lock, [&] { return t.go; });
        t.go = false;
        t.at_top = false;
        ++t.frames;
        if (t.world_pending) apply_world(t);
        if (t.rails_pending && !t.world_pending) apply_rails(t);
        if (t.teleport_pending && !t.world_pending) apply_teleport(t);
        pace->flat_out = true;
        pace->skip_draw = true;
        pace->pose_without_draw = true; // (the host draws the skater: thug2_skater_pose)
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
    audio::install_device(headless::capture_audio_device());
    media::install_movie(headless::null_movie());
    input::add_source(headless::scripted_input());
    // (no loading console panel: it is an ImGui overlay, and headless draws nothing)
    sys::run_startup_initialisers();

    // sys::win_main's part that matters without a window: the PC set-up (settings, script patches, the event pump),
    // the sound output (the host's, capture_audio.cpp), then the game's main with the command line's words.
    // (no window to give it the focus: the game is active from the start, or its main loop idles)
    sys::set_app_active(true);
    // (PS2 controls - as the game's own settings default them, read only by the SDL input this harness doesn't run:
    // either shoulder trigger alone does what both together do on the PC pad - spine transfers, acid drops, hips)
    sio::the_input_settings().ps2_controls_ = 1;
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
    if (game_dir) {
        // (the game's files are found from its folder; the host's working folder isn't changed)
        char real[PATH_MAX];
        if (!realpath(game_dir, real)) {
            std::fprintf(stderr, "thug2: no game folder %s\n", game_dir);
            return 1;
        }
        headless::set_game_folder(real);
    }
    // (the game keeps pointers into its command line for its whole life)
    static std::vector<std::string> words;
    static std::vector<char*> pointers;
    words.assign(argv, argv + argc);
    pointers.clear();
    for (std::string& w : words) pointers.push_back(w.data());
    // (a thread of its own with a desktop-sized stack: a host's - Android's 1 MB - may be too small for the game)
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 16U << 20U);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    const int started = pthread_create(&thread, &attributes, [](void*) -> void* {
        run_game(static_cast<int>(pointers.size()), pointers.data());
        Turns& t = turns();
        std::lock_guard<std::mutex> lock(t.mutex);
        t.ended = true;
        t.changed.notify_all();
        return nullptr;
    }, nullptr);
    pthread_attr_destroy(&attributes);
    if (started != 0) {
        std::fprintf(stderr, "thug2: the game's thread didn't start\n");
        return 1;
    }
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
    headless::sound_frame_done();
    if (skater) {
        headless::read_skater(skater);
        skater->frame = t.frames;
    }
    return 0;
}

extern "C" int thug2_set_world(const Thug2Triangle* triangles, uint32_t count, const float spawn[3], float heading_degrees)
{
    if (!triangles || count == 0) return 1;
    Turns& t = turns();
    std::lock_guard<std::mutex> lock(t.mutex);
    t.world.clear();
    for (uint32_t i = 0; i < count; ++i) {
        headless::CollisionTriangle tri{};
        std::memcpy(tri.v, triangles[i].v, sizeof(tri.v));
        tri.flags = triangles[i].flags;
        tri.terrain = triangles[i].terrain;
        t.world.push_back(tri);
    }
    t.has_spawn = spawn != nullptr;
    if (spawn) std::memcpy(t.spawn, spawn, sizeof(t.spawn));
    t.heading = heading_degrees;
    t.world_pending = true;
    return 0;
}

extern "C" int thug2_teleport(const float spawn[3], float heading_degrees)
{
    if (!spawn) return 1;
    Turns& t = turns();
    std::lock_guard<std::mutex> lock(t.mutex);
    std::memcpy(t.teleport, spawn, sizeof(t.teleport));
    t.teleport_heading = heading_degrees;
    t.teleport_pending = true;
    return 0;
}

extern "C" void thug2_set_cache_dir(const char* folder) { headless::set_cache_folder(folder); }

extern "C" int thug2_set_rails(const float (*points)[3], const uint32_t* counts, uint32_t rail_count)
{
    if (!points || !counts) return 1;
    Turns& t = turns();
    std::lock_guard<std::mutex> lock(t.mutex);
    t.rails.clear();
    size_t at = 0;
    for (uint32_t r = 0; r < rail_count; ++r) {
        std::vector<std::array<float, 3>> rail;
        for (uint32_t i = 0; i < counts[r]; ++i, ++at) rail.push_back({points[at][0], points[at][1], points[at][2]});
        t.rails.push_back(std::move(rail));
    }
    t.rails_pending = true;
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
