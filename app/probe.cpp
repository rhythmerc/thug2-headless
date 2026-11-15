// The local skater, read for the host between frames: its position, velocity and physics state (the skater state
// component's phase, through its physics control).
#include "app/probe.h"

#include <cstdint>

#include "engine/objects/entity.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"

namespace headless {
namespace {
constexpr uint32_t kSkater = 0x5B8AB877;               // Skater (the local skater's alias)
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl
} // namespace

void read_skater(Thug2Skater* out)
{
    *out = {};
    out->state = -1;
    auto* skater = static_cast<objects::GameEntity*>(objects::find_object(kSkater));
    if (!skater) return;
    out->present = 1;
    const maths::Vec4& at = skater->at_point();
    const maths::Vec4& vel = skater->vel();
    out->position[0] = at.x;
    out->position[1] = at.y;
    out->position[2] = at.z;
    out->velocity[0] = vel.x;
    out->velocity[1] = vel.y;
    out->velocity[2] = vel.z;
    if (auto* control = static_cast<objects::RiderBoardPhysics*>(skater->fetch_module(kSkaterPhysicsControl))) {
        if (objects::RiderPoseState* pose = control->pose()) out->state = pose->cur_phase();
    }
}

} // namespace headless
