// The host's pad on port 0: the sticks and buttons thug2_step was given this frame, as the port's control bytes -
// filled as PartyMod's SDL source fills them (engine/pad_input/sdl_input.cpp poll_controller): a set bit is a button
// down, sticks 0-255 with 0x80 at rest.
#include "backends/backends.h"

#include <cstdint>

#include "app/probe.h"
#include "base/core/checked_index.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_source.h"

namespace headless {
namespace {

class HostPad final : public input::Source {
public:
    bool has_pad(int port) override { return port == 0; }
    void poll_port(input::PortPoll& poll) override
    {
        if (poll.port != 0 || poll.blocked) return;
        poll.connected = true;
        using namespace sio;
        const Thug2Pad pad = current_pad();
        auto data = poll.data;
        core::at(data, kControlLeftStickX) = pad.left_x;
        core::at(data, kControlLeftStickY) = pad.left_y;
        core::at(data, kControlRightStickX) = pad.right_x;
        core::at(data, kControlRightStickY) = pad.right_y;
        auto button = [&](uint16_t mask, int bit, int pressure) {
            if ((pad.buttons & mask) == 0) return;
            core::at(data, kControlButtonsLow) |= static_cast<uint8_t>(1 << bit);
            core::at(data, pressure) = 0xFF;
        };
        button(THUG2_TRIANGLE, 4, kPressureTriangle);
        button(THUG2_CIRCLE, kCircleBit, kPressureCircle);
        button(THUG2_CROSS, kCrossBit, kPressureCross);
        button(THUG2_SQUARE, kSquareBit, kPressureSquare);
        button(THUG2_L2, 0, kPressureL2);
        button(THUG2_R2, 1, kPressureR2);
        button(THUG2_L1, 2, kPressureL1);
        button(THUG2_R1, 3, kPressureR1);
    }
};

} // namespace

input::Source& scripted_input()
{
    static HostPad source;
    return source;
}

} // namespace headless
