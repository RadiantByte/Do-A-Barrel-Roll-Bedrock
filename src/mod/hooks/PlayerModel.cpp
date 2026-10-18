// Player model roll (third person).
//
// Java (PlayerEntityRendererMixin): in PlayerEntityRenderer.setupTransforms the glide branch applies, after the yaw,
//   multiply(POSITIVE_X.rotationDegrees(t * (-90 - pitch)))              // lay the body into the flight direction
//   multiply(POSITIVE_Y.rotationDegrees(<sideways tilt from velocity>))  // ordinal 1, replaced by the mod with the roll
// so the roll is a rotation about the body's long axis, applied after the glide pitch.
//
// Bedrock has the very same two-step glide transform, in ActorRenderData::applyGlobalAnims (RVA 0x1d5d280, 1.26.51),
// fed by the static ActorRenderData::getDamageOrGlidingXYRotation(Mob const&, float alpha, bool isFirstPersonPlayer)
// (RVA 0x1d5d070). Disassembly of that function:
//   - flag 0x38 set (and not first person):  result = {const - pitch, 0}
//   - flag 0x20 (gliding) set, not first person:
//       x = (const - pitch) * clamp((fallFlyTicks + alpha)^2 / 100, 0, 1)         (const is -90)
//       y = degrees(angle between horizontal velocity and horizontal look) * sign  (the Java "sideways tilt")
//     result = {x, y}, engaged flag at +8; otherwise the optional is empty.
//   applyGlobalAnims then rotates the world matrix about (1,0,0) by x and, only if y != 0, about (0,1,0) by y, in that
//   order (degrees * deg2rad; call 0x140538f40 = Matrix::rotate-like). That is exactly Java's X-then-Y, so replacing
//   the returned y with the roll gives Java's result. The first-person call passes isFirstPersonPlayer=true, which
//   makes the function return nothing for the glide branch, so first person and the arm are untouched; other
//   entities are filtered by comparing the Mob with the local player.

#include "mod/hooks/State.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/actor/ActorRenderData.h"

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace do_a_barrel_roll::hooks {

namespace {

using Clock = std::chrono::steady_clock;

// Model-space Y points from feet to head, which becomes the flight direction after the glide tilt, so a positive
// (right wing down) roll is a positive rotation about it. Flip to -1 if the model rolls the wrong way in game.
using GlideRotation = std::optional<std::pair<float, float>>;

constexpr float kModelRollSign = 1.0f;

Clock::time_point lastLog{};
int               callsSinceLog = 0;
int               appliedSinceLog = 0;

LL_STATIC_HOOK(
    GlidingRotationHook,
    ll::memory::HookPriority::Normal,
    &ActorRenderData::getDamageOrGlidingXYRotation,
    GlideRotation,
    Mob const& mob,
    float      alpha,
    bool       isFirstPersonPlayer
) {
    auto result = origin(mob, alpha, isFirstPersonPlayer);

    ++callsSinceLog;
    float const roll = kModelRollSign * state::cameraRollDeg * static_cast<float>(state::config.cameraRollSign);
    bool applied = false;
    float before = 0.0f;
    if (result && !isFirstPersonPlayer && roll != 0.0f && std::isfinite(roll)) {
        auto client = ll::service::getClientInstance();
        if (client && static_cast<Actor const*>(client->getLocalPlayer()) == static_cast<Actor const*>(&mob)) {
            before        = result->second;
            result->second = roll; // a y of exactly 0 would be skipped by the caller, roll != 0 here
            applied       = true;
            ++appliedSinceLog;
        }
    }

    if (state::config.debugLogging) {
        auto now = Clock::now();
        if (now - lastLog > std::chrono::seconds(1)) {
            lastLog = now;
            state::debugLog(
                "playerModel[getDamageOrGlidingXYRotation] calls={} applied={} lastApplied={} firstPerson={} "
                "engaged={} x={:.2f} yOriginal={:.2f} yNow={:.2f} roll={:.2f}",
                callsSinceLog, appliedSinceLog, applied, isFirstPersonPlayer, result.has_value(),
                result ? result->first : 0.0f, before, result ? result->second : 0.0f, roll
            );
            callsSinceLog = appliedSinceLog = 0;
        }
    }
    return result;
}

} // namespace

void installPlayerModel() {
    lastLog = {};
    GlidingRotationHook::hook();
}
void uninstallPlayerModel() { GlidingRotationHook::unhook(); }

} // namespace do_a_barrel_roll::hooks
