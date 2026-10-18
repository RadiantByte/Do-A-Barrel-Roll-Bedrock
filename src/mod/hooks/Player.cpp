// Per-tick hook: controller tick, thrust (velocity + smoke particles), jump reset.
//
// NOT PORTED: activation behaviours that hook the glide-start check (TRIPLE_JUMP / HYBRID): with these set the
// mod behaves as VANILLA.
//
// Thrust application: Bedrock has no travelGliding() to wrap (glide physics runs in component systems), so the
// delta is added to the current velocity once per tick after the vanilla tick, through the virtual
// Actor::lerpMotion (the same entry point the SetActorMotion packet uses, which replaces mPosDelta). Fallback
// (general.misc.thrust_direct_write): write StateVectorComponent::mPosDelta directly.
//
// Server agreement: see Server.cpp (in-process server accepts the client position while thrusting). Thrust is not
// applied when no in-process server is detected, unless general.misc.thrust_allow_remote is set.

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

long long lastThrustLog   = -1000;
long long lastParticleLog = -1000;
long long lastWhyLog      = -1000;
long long lastRemoteLog   = -1000;

// Vanilla particle effect id of campfire_smoke_tall (resource pack particles/campfire_smoke_tall.json). The legacy
// Level::addParticle(ParticleType::CampfireSmokeTall) path returns null on this version, so go through the particle
// effect system (LevelListener -> ParticleSystemInterface) of the client level instead.
void spawnThrustParticle(LocalPlayer& player, Vec3 const& velocity) {
    // Java: entityPos - velocity * 0.5 (feet position), campfire_signal_smoke, zero velocity.
    // Bedrock's getPosition() is the eye position, so use the feet position.
    Vec3 pos = player.getFeetPos() - velocity * 0.5f;
    // Level::addParticleEffect(3 args) hides the ILevel convenience overload, so call it through ILevel.
    auto& level = static_cast<ILevel&>(player.getLevel());
    level.addParticleEffect(HashedString{"minecraft:campfire_tall_smoke_particle"}, pos);
    if (state::tickCount - lastParticleLog >= 20) {
        lastParticleLog = state::tickCount;
        state::debugLog(
            "thrust particle effect dispatched at ({:.2f}, {:.2f}, {:.2f}) clientSide={}", pos.x, pos.y, pos.z,
            level.isClientSide()
        );
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

    auto ps = state::snapshot(*this);
    state::controller.tick(ps);

    bool const flying = state::controller.isFallFlying(ps);
    bool const anyKey = state::keys.thrustForward || state::keys.thrustBackward;
    if (flying) state::lastFlyingMs.store(state::nowMs());
    if (flying) {
        auto t = state::controller.thrust(ps, state::keys, state::tickCount);
        if (!t) {
            if (anyKey && state::tickCount - lastWhyLog >= 20) {
                lastWhyLog = state::tickCount;
                state::debugLog(
                    "thrust() -> nullopt: rolling={} enable_thrust={}", state::controller.isRolling(),
                    state::config.enableThrust
                );
            }
        } else {
            Vec3 const before = getPosDelta();
            Vec3 const after{
                before.x + static_cast<float>(t->velocityDelta.x),
                before.y + static_cast<float>(t->velocityDelta.y),
                before.z + static_cast<float>(t->velocityDelta.z)
            };
            bool thrusting = t->velocityDelta.x != 0.0 || t->velocityDelta.y != 0.0 || t->velocityDelta.z != 0.0;
            // Thrust needs an in-process server (Server.cpp accepts the client position); a remote vanilla server
            // would rubber-band the player, so do nothing there.
            bool const serverAlive = state::nowMs() - state::lastServerPolicyMs.load() < 1500;
            if (thrusting && !serverAlive && !state::config.thrustAllowRemote) {
                thrusting = false;
                if (state::tickCount - lastRemoteLog >= 100) {
                    lastRemoteLog = state::tickCount;
                    state::debugLog("thrust disabled: no in-process server movement policy seen (remote server?)");
                }
            }
            if (thrusting) {
                state::lastThrustMs.store(state::nowMs());
                char const* how = "direct";
                if (state::config.thrustDirectWrite) {
                    const_cast<Vec3&>(getPosDelta()) = after;
                } else {
                    lerpMotion(after);
                    Vec3 const rb = getPosDelta();
                    if (std::abs(rb.x - after.x) + std::abs(rb.y - after.y) + std::abs(rb.z - after.z) > 1e-4f) {
                        const_cast<Vec3&>(getPosDelta()) = after; // lerpMotion did not stick: write directly
                        how                              = "lerpMotion+direct";
                    } else {
                        how = "lerpMotion";
                    }
                }
                if (state::tickCount - lastThrustLog >= 20) {
                    lastThrustLog = state::tickCount;
                    Vec3 const now = getPosDelta();
                    state::debugLog(
                        "thrust applied ({}): delta ({:.4f}, {:.4f}, {:.4f}) vel ({:.3f}, {:.3f}, {:.3f}) -> ({:.3f}, "
                        "{:.3f}, {:.3f}) readback ({:.3f}, {:.3f}, {:.3f}) | server corrections seen={} overridden={} client rewinds={}",
                        how, t->velocityDelta.x, t->velocityDelta.y, t->velocityDelta.z,
                        before.x, before.y, before.z, after.x, after.y, after.z, now.x, now.y, now.z,
                        state::statCorrectionsSeen.load(), state::statCorrectionsOverridden.load(),
                        state::statClientRewinds.load()
                    );
                }
            } else if (anyKey && state::tickCount - lastWhyLog >= 20) {
                lastWhyLog = state::tickCount;
                state::debugLog("thrust() returned zero delta (already at max speed?)");
            }
            if (t->spawnParticle && thrusting) spawnThrustParticle(*this, before);
        }
    } else if (anyKey && state::tickCount - lastWhyLog >= 20) {
        lastWhyLog = state::tickCount;
        state::debugLog("thrust key held but not fall-flying (gliding={})", ps.isGliding);
    }
    {
        Vec3 const v = getPosDelta(); // read by the in-process server (Server.cpp) to keep its velocity in sync
        state::clientVelX.store(v.x);
        state::clientVelY.store(v.y);
        state::clientVelZ.store(v.z);
    }
    state::controller.onTickMovementEnd(ps.onGround, false);
}

} // namespace

void installPlayer() { NormalTickHook::hook(); }
void uninstallPlayer() { NormalTickHook::unhook(); }

} // namespace do_a_barrel_roll::hooks
