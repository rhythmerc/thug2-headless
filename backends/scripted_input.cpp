// The pad the harness drives: port 0 is a controller whose buttons and sticks follow a script by frame, from
// THUG2_INPUT - comma-separated "<from>[-<to>]:<control>" items, frames counted from the first read, e.g.
//     THUG2_INPUT="120-240:up,250:cross,400-460:left"
// Controls: up / down / left / right (the left stick, full over), rup / rdown / rleft / rright (the right stick),
// cross, circle, square, triangle, l1, r1, l2, r2 (pressed, full pressure). Anything not held rests: sticks at the
// centre (0x80), buttons up. The port's control bytes are filled as PartyMod's SDL source fills them
// (engine/pad_input/sdl_input.cpp poll_controller): a set bit is a button down.
#include "backends/backends.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/probe.h"
#include "base/core/checked_index.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_source.h"

namespace headless {
namespace {

constexpr uint8_t kRest = 0x80;
constexpr uint8_t kFull = 0xFF;

struct Press {
    uint32_t from = 0;
    uint32_t to = 0;
    std::string control;
};

std::vector<Press> parse(const char* text)
{
    std::vector<Press> out;
    std::string all = text ? text : "";
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

class ScriptedInput final : public input::Source {
public:
    ScriptedInput() : script_(parse(std::getenv("THUG2_INPUT"))) {}

    // (once a frame, before the pads are read)
    void read_frame() override
    {
        ++frame_;
        probe_frame();
    }
    bool has_pad(int port) override { return port == 0; }
    void poll_port(input::PortPoll& poll) override
    {
        if (poll.port != 0 || poll.blocked) return;
        poll.connected = true;
        auto data = poll.data;
        using namespace sio;
        core::at(data, kControlRightStickX) = kRest;
        core::at(data, kControlRightStickY) = kRest;
        core::at(data, kControlLeftStickX) = kRest;
        core::at(data, kControlLeftStickY) = kRest;
        for (const Press& p : script_) {
            if (frame_ < p.from || frame_ > p.to) continue;
            const std::string& c = p.control;
            auto button = [&](int bit, bool high, int pressure) {
                core::at(data, high ? kControlButtonsHigh : kControlButtonsLow) |= static_cast<uint8_t>(1 << bit);
                if (pressure >= 0) core::at(data, pressure) = kFull;
            };
            if (c == "up") core::at(data, kControlLeftStickY) = 0;
            else if (c == "down") core::at(data, kControlLeftStickY) = kFull;
            else if (c == "left") core::at(data, kControlLeftStickX) = 0;
            else if (c == "right") core::at(data, kControlLeftStickX) = kFull;
            else if (c == "rup") core::at(data, kControlRightStickY) = 0;
            else if (c == "rdown") core::at(data, kControlRightStickY) = kFull;
            else if (c == "rleft") core::at(data, kControlRightStickX) = 0;
            else if (c == "rright") core::at(data, kControlRightStickX) = kFull;
            else if (c == "triangle") button(4, false, kPressureTriangle);
            else if (c == "circle") button(kCircleBit, false, kPressureCircle);
            else if (c == "cross") button(kCrossBit, false, kPressureCross);
            else if (c == "square") button(kSquareBit, false, kPressureSquare);
            else if (c == "l2") button(0, false, kPressureL2);
            else if (c == "r2") button(1, false, kPressureR2);
            else if (c == "l1") button(2, false, kPressureL1);
            else if (c == "r1") button(3, false, kPressureR1);
        }
    }

private:
    std::vector<Press> script_;
    uint32_t frame_ = 0;
};

} // namespace

input::Source& scripted_input()
{
    static ScriptedInput source;
    return source;
}

} // namespace headless
