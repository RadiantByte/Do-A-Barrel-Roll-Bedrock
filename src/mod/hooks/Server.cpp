// Server-side cooperation for thrust.
//
// Bedrock movement is server-authoritative with client prediction: each tick the client sends a PlayerAuthInputPacket
// with its predicted position, the server re-simulates the player from the inputs (which know nothing about thrust)
// and ServerCorrectionPolicy::shouldCorrectMovement compares the two. When they differ too much it answers
// CorrectionMethod::CorrectClient, the client receives a CorrectPlayerMovePredictionPacket and rewinds/replays,
// which shows up as the player glitching back and forth.
//
// In a world hosted by this process (singleplayer / LAN host) the server policy runs in-process, so it is hooked:
// while the local player is thrusting, a correction for that player is turned into AcceptClient with the client
// position, so prediction and server agree. A remote vanilla server never calls this code; Player.cpp detects that
// (no recent policy call) and does not apply thrust there (config thrust_allow_remote overrides).

#include "mod/hooks/State.h"

#include "ll/api/memory/Hook.h"
#include "mc/client/input/ClientPlayerRewindListener.h"
#include "mc/entity/components/CorrectionMethod.h"
#include "mc/entity/components/MovementCorrection.h"
#include "mc/entity/components/ServerCorrectionPolicy.h"
#include "mc/network/packet/CorrectPlayerMovePredictionPacket.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/world/actor/player/Player.h"

namespace do_a_barrel_roll::hooks {

namespace {

constexpr std::int64_t kThrustWindowMs = 300;
// After thrusting the server's own simulation of the player has diverged (it never saw the extra velocity), so keep
// accepting the client for the rest of the flight (plus a grace period after landing / stopping the glide).
constexpr std::int64_t kFlightGraceMs     = 2000;
constexpr std::int64_t kThrustMemoryMs    = 120000;

bool divergedByThrust(std::int64_t now) {
    auto const lastThrust = state::lastThrustMs.load();
    if (now - lastThrust <= kThrustWindowMs) return true;
    return now - lastThrust <= kThrustMemoryMs && now - state::lastFlyingMs.load() <= kFlightGraceMs;
}

std::atomic<std::int64_t> lastServerLogMs{-1000000};
std::atomic<std::int64_t> lastClientLogMs{-1000000};

LL_TYPE_INSTANCE_HOOK(
    ShouldCorrectMovementHook,
    ll::memory::HookPriority::Normal,
    ServerCorrectionPolicy,
    &ServerCorrectionPolicy::$shouldCorrectMovement,
    MovementCorrection,
    EntityContext&               entity,
    PlayerAuthInputPacket const& packet,
    uint64                       frame,
    uchar const                  currentCounter,
    bool                         isStrictMovement
) {
    MovementCorrection result = origin(entity, packet, frame, currentCounter, isStrictMovement);

    auto const now = state::nowMs();
    state::lastServerPolicyMs.store(now);

    auto* player = Player::tryGetFromEntity(entity, false);
    if (!player || static_cast<std::int64_t>(player->getOrCreateUniqueID().rawID) != state::localPlayerId.load()) {
        return result;
    }
    bool const hasCorrection = result.mMethod != CorrectionMethod::None;
    if (hasCorrection) state::statCorrectionsSeen.fetch_add(1);

    bool const thrusting = divergedByThrust(now);
    if (thrusting) {
        // Keep the server's velocity in line with the client's, so its simulation stops drifting away.
        const_cast<Vec3&>(player->getPosDelta()) =
            Vec3{state::clientVelX.load(), state::clientVelY.load(), state::clientVelZ.load()};
    }
    if (thrusting && hasCorrection) {
        Vec3 const& clientPos = packet.mPos;
        auto const  was       = result.mMethod;
        result.mMethod         = CorrectionMethod::AcceptClient;
        result.mAcceptPosition = clientPos;
        state::statCorrectionsOverridden.fetch_add(1);
        if (state::config.debugLogging && now - lastServerLogMs.load() >= 1000) {
            lastServerLogMs.store(now);
            state::debugLog(
                "server: correction {} -> AcceptClient for local player (client pos {:.2f},{:.2f},{:.2f}); "
                "overridden={} seen={}",
                static_cast<int>(was), clientPos.x, clientPos.y, clientPos.z, state::statCorrectionsOverridden.load(),
                state::statCorrectionsSeen.load()
            );
        }
    }
    return result;
}

LL_TYPE_STATIC_HOOK(
    ClientCorrectionHook,
    ll::memory::HookPriority::Normal,
    ClientPlayerRewindListener,
    &ClientPlayerRewindListener::_onCorrectPlayerMovePredictionPacket,
    EventResult,
    Player&                                  player,
    CorrectPlayerMovePredictionPacket const& packet
) {
    auto const n   = state::statClientRewinds.fetch_add(1) + 1;
    auto const now = state::nowMs();
    if (state::config.debugLogging && now - lastClientLogMs.load() >= 1000) {
        lastClientLogMs.store(now);
        Vec3 const& p = packet.mPos;
        state::debugLog(
            "client: CorrectPlayerMovePrediction #{} pos ({:.2f},{:.2f},{:.2f}) thrusting={}", n, p.x, p.y, p.z,
            divergedByThrust(now)
        );
    }
    return origin(player, packet);
}

} // namespace

void installServer() {
    ShouldCorrectMovementHook::hook();
    ClientCorrectionHook::hook();
}
void uninstallServer() {
    ClientCorrectionHook::unhook();
    ShouldCorrectMovementHook::unhook();
}

} // namespace do_a_barrel_roll::hooks
