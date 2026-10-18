#include "mod/hooks/State.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"

#include <fstream>

namespace do_a_barrel_roll::state {

void debugWrite(std::string const& line) {
    static std::ofstream out(configPath.parent_path() / "debug.log", std::ios::trunc);
    out << line << '\n' << std::flush;
}

core::PlayerState snapshot(LocalPlayer& player) {
    core::PlayerState s;
    // ASSUMPTION: Bedrock rotation is Vec2(x = pitch, y = yaw) in degrees, pitch + = down, yaw 0 = +Z.
    Vec2 const& rot = player.getRotation();
    s.pitch         = rot.x;
    s.yaw           = rot.y;
    // ASSUMPTION: posDelta is blocks per tick (same as Java getVelocity()).
    Vec3 const& v = player.getPosDelta();
    s.velocity    = {v.x, v.y, v.z};
    s.isGliding          = player.isGliding();
    s.isSubmergedInWater = player.isInWater();
    s.onGround           = player.isOnGround();
    s.isJumping          = false;
    return s;
}

bool isFlightActive(LocalPlayer& player) { return controller.isFallFlying(snapshot(player)); }

bool isHudTopScreen(ClientInstance& client) { return client.getTopScreenName() == "hud_screen"; }

} // namespace do_a_barrel_roll::state
