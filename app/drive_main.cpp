// thug2-drive: the engine API (app/thug2_engine.h) driven from the command line, as a host would drive it. Boots the
// game, steps THUG2_FRAMES frames with a scripted pad and logs the skater every 30 frames, then the time a step took.
//     THUG2_GAME_DIR=~/Games/THUG2-extract/Game THUG2_FRAMES=700 THUG2_INPUT="120-330:up,400-412:cross" \
//         thug2-drive -level BO
// THUG2_INPUT: comma-separated "<from>[-<to>]:<control>" items (frames from the first step); controls: up / down /
// left / right (the left stick, full over), rup / rdown / rleft / rright (the right stick), cross, circle, square,
// triangle, l1, r1, l2, r2.
#include "app/thug2_engine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <string>
#include <vector>

namespace {

struct Press {
    uint32_t from = 0;
    uint32_t to = 0;
    std::string control;
};

std::vector<Press> parse(const char* text)
{
    std::vector<Press> out;
    const std::string all = text ? text : "";
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find(',', at);
        if (end == std::string::npos) end = all.size();
        const std::string item = all.substr(at, end - at);
        at = end + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        Press p;
        const std::string frames = item.substr(0, colon);
        const size_t dash = frames.find('-');
        p.from = static_cast<uint32_t>(std::strtoul(frames.c_str(), nullptr, 10));
        p.to = dash == std::string::npos ? p.from + 2 : static_cast<uint32_t>(std::strtoul(frames.c_str() + dash + 1, nullptr, 10));
        p.control = item.substr(colon + 1);
        out.push_back(p);
    }
    return out;
}

Thug2Pad pad_at(const std::vector<Press>& script, uint32_t frame)
{
    Thug2Pad pad{0x80, 0x80, 0x80, 0x80, 0};
    for (const Press& p : script) {
        if (frame < p.from || frame > p.to) continue;
        const std::string& c = p.control;
        if (c == "up") pad.left_y = 0;
        else if (c == "down") pad.left_y = 0xFF;
        else if (c == "left") pad.left_x = 0;
        else if (c == "right") pad.left_x = 0xFF;
        else if (c == "rup") pad.right_y = 0;
        else if (c == "rdown") pad.right_y = 0xFF;
        else if (c == "rleft") pad.right_x = 0;
        else if (c == "rright") pad.right_x = 0xFF;
        else if (c == "cross") pad.buttons |= THUG2_CROSS;
        else if (c == "circle") pad.buttons |= THUG2_CIRCLE;
        else if (c == "square") pad.buttons |= THUG2_SQUARE;
        else if (c == "triangle") pad.buttons |= THUG2_TRIANGLE;
        else if (c == "l1") pad.buttons |= THUG2_L1;
        else if (c == "r1") pad.buttons |= THUG2_R1;
        else if (c == "l2") pad.buttons |= THUG2_L2;
        else if (c == "r2") pad.buttons |= THUG2_R2;
    }
    return pad;
}

// THUG2_WORLD=test: a small park far from the level (around x = z = 50000, the floor at y = 0), in game units
// (inches): an 800-inch floor, a 10-foot quarter pipe at +z (its top third vert) with a deck behind it, and a
// wallride wall along -x. THUG2_WINDING=flip turns every triangle round.
std::vector<Thug2Triangle> test_world()
{
    std::vector<Thug2Triangle> out;
    const bool flip = std::getenv("THUG2_WINDING") && std::string(std::getenv("THUG2_WINDING")) == "flip";
    const float cx = 50000.0F, cz = 50000.0F;
    auto tri = [&](const float a[3], const float b[3], const float c[3], uint16_t flags) {
        Thug2Triangle t{};
        const float* v[3] = {a, b, c};
        if (flip) std::swap(v[1], v[2]);
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 3; ++k) t.v[i][k] = v[i][k];
        t.flags = flags;
        out.push_back(t);
    };
    // A quad a b c d (counter-clockwise seen from its front) as two triangles.
    auto quad = [&](std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> c, std::array<float, 3> d, uint16_t flags) {
        tri(a.data(), b.data(), c.data(), flags);
        tri(a.data(), c.data(), d.data(), flags);
    };
    const float half = 400.0F;
    const float qp_start = cz + 100.0F; // where the quarter pipe's curve starts (along +z)
    const float radius = 120.0F;
    // The floor, up to the quarter pipe.
    quad({cx - half, 0, cz - half}, {cx - half, 0, qp_start}, {cx + half, 0, qp_start}, {cx + half, 0, cz - half}, THUG2_FACE_SKATABLE);
    // The quarter pipe: 12 steps of a quarter circle from flat to vertical, along +z; the steep top third vert.
    const int steps = 12;
    for (int i = 0; i < steps; ++i) {
        const double a0 = (M_PI / 2) * i / steps, a1 = (M_PI / 2) * (i + 1) / steps;
        const float z0 = qp_start + radius * static_cast<float>(std::sin(a0)), y0 = radius * static_cast<float>(1 - std::cos(a0));
        const float z1 = qp_start + radius * static_cast<float>(std::sin(a1)), y1 = radius * static_cast<float>(1 - std::cos(a1));
        const uint16_t flags = THUG2_FACE_SKATABLE | (i >= steps * 2 / 3 ? THUG2_FACE_VERT : 0);
        quad({cx - half, y0, z0}, {cx - half, y1, z1}, {cx + half, y1, z1}, {cx + half, y0, z0}, flags);
    }
    // The deck behind it.
    const float top_z = qp_start + radius;
    quad({cx - half, radius, top_z}, {cx - half, radius, top_z + 60}, {cx + half, radius, top_z + 60}, {cx + half, radius, top_z}, THUG2_FACE_SKATABLE);
    // A wallride wall along -x, facing +x.
    quad({cx - half, 0, cz - half}, {cx - half, 120, cz - half}, {cx - half, 120, qp_start}, {cx - half, 0, qp_start}, THUG2_FACE_WALL | THUG2_FACE_SKATABLE);
    return out;
}

const char* state_name(int state)
{
    static const char* names[] = {"ground", "air", "wall", "lip", "rail", "wallplant"};
    return state >= 0 && state <= 5 ? names[state] : "?";
}

} // namespace

int main(int argc, char** argv)
{
    using clock = std::chrono::steady_clock;
    const auto boot_start = clock::now();
    if (thug2_boot(std::getenv("THUG2_GAME_DIR"), argc, argv) != 0) {
        std::fprintf(stderr, "thug2-drive: the game ended before its first frame\n");
        return 1;
    }
    const double boot_ms = std::chrono::duration<double, std::milli>(clock::now() - boot_start).count();
    std::printf("DRIVE booted in %.0f ms\n", boot_ms);
    const char* frames_text = std::getenv("THUG2_FRAMES");
    const uint32_t frames = frames_text ? static_cast<uint32_t>(std::strtoul(frames_text, nullptr, 10)) : 600;
    const std::vector<Press> script = parse(std::getenv("THUG2_INPUT"));
    double total_ms = 0;
    double worst_ms = 0;
    uint32_t stepped = 0;
    const bool test = std::getenv("THUG2_WORLD") && std::string(std::getenv("THUG2_WORLD")) == "test";
    const std::vector<Thug2Triangle> world = test ? test_world() : std::vector<Thug2Triangle>{};
    for (uint32_t frame = 1; frame <= frames; ++frame) {
        // (the world once the level has put the skater at its own start, else that start overrides ours)
        if (test && frame == 120) {
            const float spawn[3] = {50000.0F, 20.0F, 49700.0F};
            thug2_set_world(world.data(), static_cast<uint32_t>(world.size()), spawn, 0.0F);
            // A ledge-height rail down the middle of the run, 30 units up, before the quarter pipe.
            const float rail[2][3] = {{50000.0F, 30.0F, 49900.0F}, {50000.0F, 30.0F, 50080.0F}};
            const uint32_t counts[1] = {2};
            thug2_set_rails(rail, counts, 1);
            std::printf("DRIVE frame %u: the test world (%zu triangles) given\n", frame, world.size());
        }
        const Thug2Pad pad = pad_at(script, frame);
        Thug2Skater skater{};
        const auto start = clock::now();
        if (thug2_step(&pad, &skater) != 0) {
            std::printf("DRIVE the game ended at frame %u\n", frame);
            break;
        }
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
        ++stepped;
        total_ms += ms;
        if (ms > worst_ms) worst_ms = ms;
        if (ms > 20.0) std::printf("DRIVE frame %u took %.0f ms\n", frame, ms);
        static const uint32_t every = std::getenv("THUG2_EVERY") ? static_cast<uint32_t>(std::strtoul(std::getenv("THUG2_EVERY"), nullptr, 10)) : 30;
        if (frame % every == 0) {
            if (skater.present) {
                std::printf("DRIVE frame %u: at (%.1f, %.1f, %.1f) vel (%.1f, %.1f, %.1f) %s  [%.2f ms]\n", frame,
                            skater.position[0], skater.position[1], skater.position[2], skater.velocity[0],
                            skater.velocity[1], skater.velocity[2], state_name(skater.state), ms);
            } else {
                std::printf("DRIVE frame %u: no skater yet  [%.2f ms]\n", frame, ms);
            }
            std::fflush(stdout);
        }
    }
    if (stepped) std::printf("DRIVE %u steps: %.2f ms average, %.2f ms worst\n", stepped, total_ms / stepped, worst_ms);
    thug2_quit();
    return 0;
}
