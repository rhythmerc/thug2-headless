// The harness's view of the game: each frame (from the harness's input source, read once a frame before the pads)
// the local skater's position, velocity and physics state, logged every 30 frames; after THUG2_FRAMES frames the game
// is asked to quit.
#include "app/probe.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "engine/gfx/engine_parts.h"
#include "engine/objects/entity.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"

namespace headless {
namespace {

constexpr uint32_t kSkater = 0x5B8AB877;               // Skater (the local skater's alias)
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl

const char* state_name(int state)
{
    switch (state) {
    case objects::RiderPoseState::kGround:
        return "ground";
    case objects::RiderPoseState::kAir:
        return "air";
    case objects::RiderPoseState::kWall:
        return "wall";
    case objects::RiderPoseState::kLip:
        return "lip";
    case objects::RiderPoseState::kRail:
        return "rail";
    case objects::RiderPoseState::kWallplant:
        return "wallplant";
    default:
        return "?";
    }
}

class Probe {
public:
    void frame()
    {
        ++frame_;
        if (limit_ == 0) {
            const char* text = std::getenv("THUG2_FRAMES");
            limit_ = text ? std::strtoul(text, nullptr, 10) : 0xFFFFFFFFu;
        }
        if (frame_ >= limit_) {
            render::engine_parts().set_quit_requested(true);
        }
        auto* skater = static_cast<objects::GameEntity*>(objects::find_object(kSkater));
        if (!skater) {
            if (frame_ % 60 == 0) {
                std::printf("PROBE frame %u: no skater yet\n", frame_);
                std::fflush(stdout);
            }
            return;
        }
        if (!seen_) {
            seen_ = true;
            std::printf("PROBE frame %u: the skater is here\n", frame_);
        }
        if (frame_ % 30 != 0) return;
        const maths::Vec4& at = skater->at_point();
        const maths::Vec4& vel = skater->vel();
        const char* state = "?";
        if (auto* control = static_cast<objects::RiderBoardPhysics*>(skater->fetch_module(kSkaterPhysicsControl))) {
            if (objects::RiderPoseState* pose = control->pose()) state = state_name(pose->cur_phase());
        }
        std::printf("PROBE frame %u: at (%.1f, %.1f, %.1f) vel (%.1f, %.1f, %.1f) %s\n", frame_, at.x, at.y, at.z, vel.x,
                    vel.y, vel.z, state);
        std::fflush(stdout);
    }

private:
    uint32_t frame_ = 0;
    uint32_t limit_ = 0;
    bool seen_ = false;
};

} // namespace

void probe_frame()
{
    static Probe probe;
    probe.frame();
}

} // namespace headless
