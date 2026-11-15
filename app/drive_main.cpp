// thug2-drive: the engine API (app/thug2_engine.h) driven from the command line, as a host would drive it. Boots the
// game, steps THUG2_FRAMES frames with a scripted pad and logs the skater every 30 frames, then the time a step took.
//     THUG2_GAME_DIR=~/Games/THUG2-extract/Game THUG2_FRAMES=700 THUG2_INPUT="120-330:up,400-412:cross" \
//         thug2-drive -level BO
// THUG2_INPUT: comma-separated "<from>[-<to>]:<control>" items (frames from the first step); controls: up / down /
// left / right (the left stick, full over), rup / rdown / rleft / rright (the right stick), cross, circle, square,
// triangle, l1, r1, l2, r2.
#include "app/thug2_engine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    for (uint32_t frame = 1; frame <= frames; ++frame) {
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
        if (frame % 30 == 0) {
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
