// Replaces the sideways tilt (y) of the glide transform with the roll. applyGlobalAnims rotates about X by x, then
// about Y by y (skipped when y == 0). First person and other entities are left untouched.

#include "mod/hooks/State.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/actor/ActorRenderData.h"

#include <cmath>
#include <optional>
#include <utility>

namespace do_a_barrel_roll::hooks {

namespace {

using GlideRotation = std::optional<std::pair<float, float>>;

LL_STATIC_HOOK(
    GlidingRotationHook,
    ll::memory::HookPriority::Normal,
    &ActorRenderData::getDamageOrGlidingXYRotation,
    GlideRotation,
    Mob const& mob,
    float      alpha,
    bool       isFirstPersonPlayer
) {
    auto  result = origin(mob, alpha, isFirstPersonPlayer);
    float roll   = state::cameraRollDeg;
    if (result && !isFirstPersonPlayer && roll != 0.0f && std::isfinite(roll)) {
        auto client = ll::service::getClientInstance();
        if (client && static_cast<Actor const*>(client->getLocalPlayer()) == static_cast<Actor const*>(&mob)) {
            result->second = roll;
        }
    }
    return result;
}

} // namespace

void installPlayerModel() { GlidingRotationHook::hook(); }

void uninstallPlayerModel() { GlidingRotationHook::unhook(); }

} // namespace do_a_barrel_roll::hooks
