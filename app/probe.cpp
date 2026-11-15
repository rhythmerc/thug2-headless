// The local skater, read for the host between frames: its position, velocity and physics state (the skater state
// component's phase, through its physics control).
#include "app/probe.h"

#include <algorithm>
#include <cstdint>

#include "engine/objects/entity.h"
#include "game/rider/balance_trick.h"
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
    const maths::Mat4& frame = skater->basis_frame();
    out->forward[0] = frame.row[2].x;
    out->forward[1] = frame.row[2].y;
    out->forward[2] = frame.row[2].z;
    out->up[0] = frame.row[1].x;
    out->up[1] = frame.row[1].y;
    out->up[2] = frame.row[1].z;
    // (the balance trick's lean, as its meter shows it: a share of the angle the skater bails at)
    if (auto* balance = static_cast<objects::RiderTeeterMoves*>(skater->fetch_module(objects::RiderTeeterMoves::kType));
        balance && balance->trick() != 0 && balance->at_hand()) { // (the trick is set while it's balanced; `started` only by some scripts)
        using T = objects::RiderTeeterMoves;
        const uint32_t trick = balance->trick();
        out->balance_kind = trick == T::kGrind || trick == T::kSlide         ? THUG2_BALANCE_GRIND
                            : trick == T::kManual || trick == T::kNoseManual ? THUG2_BALANCE_MANUAL
                            : trick == T::kLip                               ? THUG2_BALANCE_LIP
                            : trick == T::kSkitch                            ? THUG2_BALANCE_SKITCH
                                                                             : THUG2_BALANCE_NONE;
        constexpr uint32_t kLeanBailAngle = 0xF81AB0E9; // Lean_Bail_Angle
        const float bail = balance->get_physics_value(kLeanBailAngle);
        // (the game's lean goes positive while left is held: turned round, as its meter shows it - left is left)
        out->balance = bail > 0.0F ? std::clamp(-balance->at_hand()->lean() / bail, -1.0F, 1.0F) : 0.0F;
    }
    if (auto* control = static_cast<objects::RiderBoardPhysics*>(skater->fetch_module(kSkaterPhysicsControl))) {
        if (objects::RiderPoseState* pose = control->pose()) out->state = pose->cur_phase();
    }
}

} // namespace headless
