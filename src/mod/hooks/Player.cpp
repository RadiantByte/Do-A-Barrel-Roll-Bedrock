// Per-tick hook: flight controller tick and thrust (velocity + smoke particles).
//
// Bedrock has no travelGliding() to wrap, so thrust is added to the player's velocity once per tick after the
// vanilla tick. Server agreement is handled in Server.cpp.

#include "mod/hooks/State.h"

#include <cmath>

#include "ll/api/memory/Hook.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"

namespace do_a_barrel_roll::hooks {

void markTick(); // Input.cpp

namespace {

constexpr std::int64_t kServerAliveMs = 1500;

// The legacy Level::addParticle path returns null on this version, so use the particle effect system.
void spawnThrustParticle(LocalPlayer& player, Vec3 const& velocity) {
    Vec3 const pos   = player.getFeetPos() - velocity * 0.5f;
    auto&      level = static_cast<ILevel&>(player.getLevel()); // Level::addParticleEffect hides the ILevel overload
    level.addParticleEffect(HashedString{"minecraft:campfire_tall_smoke_particle"}, pos);
}

void applyVelocity(LocalPlayer& player, Vec3 const& velocity) {
    player.lerpMotion(velocity);
    Vec3 const actual = player.getPosDelta();
    if (std::abs(actual.x - velocity.x) + std::abs(actual.y - velocity.y) + std::abs(actual.z - velocity.z) > 1e-4f) {
        const_cast<Vec3&>(player.getPosDelta()) = velocity; // lerpMotion did not stick
    }
}

LL_TYPE_INSTANCE_HOOK(
    NormalTickHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::$normalTick,
    void
) {
    origin();
    markTick();
    ++state::tickCount;

    state::localPlayerId.store(static_cast<std::int64_t>(getOrCreateUniqueID().rawID));

    auto const player = state::snapshot(*this);
    state::controller.tick(player);

    if (state::controller.isFallFlying(player)) {
        auto const now = state::nowMs();
        state::lastFlyingMs.store(now);

        if (auto const thrust = state::controller.thrust(player, state::inputKeys(), state::tickCount)) {
            auto const& delta     = thrust->velocityDelta;
            bool const  thrusting = delta.x != 0.0 || delta.y != 0.0 || delta.z != 0.0;
            // Thrust needs the in-process server to accept the client position, otherwise a remote server rubber-bands.
            bool const serverAlive = now - state::lastServerPolicyMs.load() < kServerAliveMs;
            if (thrusting && (serverAlive || state::config.thrustAllowRemote)) {
                state::lastThrustMs.store(now);
                Vec3 const before = getPosDelta();
                applyVelocity(
                    *this,
                    {before.x + static_cast<float>(delta.x),
                     before.y + static_cast<float>(delta.y),
                     before.z + static_cast<float>(delta.z)}
                );
                if (thrust->spawnParticle) spawnThrustParticle(*this, before);
            }
        }
    }

    Vec3 const velocity = getPosDelta(); // read by the in-process server (Server.cpp)
    state::clientVelX.store(velocity.x);
    state::clientVelY.store(velocity.y);
    state::clientVelZ.store(velocity.z);
    state::controller.onTickMovementEnd(player.onGround, false);
}

} // namespace

void installPlayer() { NormalTickHook::hook(); }
void uninstallPlayer() { NormalTickHook::unhook(); }

} // namespace do_a_barrel_roll::hooks
