// Mouse look interception + per-frame driver.
//
// Verified in game (debug.log):
//  * Mouse look goes LocalPlayer::localPlayerTurn -> LocalPlayer::_applyTurnDelta, with Vec2(x = -pitch delta,
//    y = yaw delta) in degrees, already sensitivity-scaled (rot.pitch -= x, rot.yaw += y).
//  * Writing ActorRotationComponent::mRot directly does not stick; the game re-syncs it. So the controller's result
//    is fed back through the original _applyTurnDelta as a delta.
// Core expects "Java mouse units" where degrees = units * 0.15, so deltas are divided by 0.15. Calibrate with
// general.misc.turn_delta_scale and general.misc.swap_turn_axes.

#include "mod/hooks/State.h"

#include "ll/api/memory/Hook.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/entity/components/ActorRotationComponent.h"

#include "mod/DoABarrelRoll.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace do_a_barrel_roll::hooks {

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point lastFrame{};
Clock::time_point lastTick{};
int               turnLogCount = 0;
bool              applyingOwnTurn = false; // set while we call _applyTurnDelta ourselves
bool              ownValid        = false; // controller-authoritative orientation (see ClientUpdateHook)
double            ownPitch = 0.0, ownYaw = 0.0;
int               flipLogCount = 0;
Clock::time_point lastNearVertical{};
double            statDp = 0, statDy = 0, statDr = 0, statIdleRot = 0;
int               statIdleFrames = 0;

// Per-second diagnostics (debug_logging).
struct TurnStats {
    int    calls = 0;
    double sx = 0, sy = 0;
};
TurnStats         statApply, statTurn;
int               statFrames = 0, statHandled = 0;
Clock::time_point lastStat{};

void count(TurnStats& st, Vec2 const& d) {
    ++st.calls;
    st.sx += d.x;
    st.sy += d.y;
}

int padDropped = 0;

void accumulate(char const* source, Vec2 const& d) {
    auto& cfg = state::config;
    // The game's own gamepad look arrives through the same turn path as the mouse (see hooks/Gamepad.cpp). While the
    // stick drives flight through XInput, drop it here so it is not counted twice (and with mouse scaling).
    if (state::pad.stickActive) {
        ++padDropped;
        return;
    }
    if (cfg.debugLogging && turnLogCount < 40) {
        ++turnLogCount;
        DoABarrelRoll::getInstance().getSelf().getLogger().info(
            "[turn #{}] {}: x={:.4f} y={:.4f}", turnLogCount, source, d.x, d.y
        );
    }
    double pitchDeg = cfg.swapTurnAxes ? d.y : -d.x;
    double yawDeg   = cfg.swapTurnAxes ? -d.x : d.y;
    // core: x -> yaw slot (cursorDeltaX), y -> pitch slot (cursorDeltaY); degrees = units * 0.15
    state::mouseDx += yawDeg * cfg.turnDeltaScale / 0.15;
    state::mouseDy += pitchDeg * cfg.turnDeltaScale / 0.15;
}

LL_TYPE_INSTANCE_HOOK(
    ApplyTurnDeltaHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::_applyTurnDelta,
    void,
    Vec2 const& turnOffset
) {
    count(statApply, turnOffset);
    if (!applyingOwnTurn && state::isFlightActive(*this)) {
        accumulate("_applyTurnDelta", turnOffset);
        return;
    }
    origin(turnOffset);
}

LL_TYPE_INSTANCE_HOOK(
    LocalPlayerTurnHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::localPlayerTurn,
    void,
    Vec2 const& deltaRot
) {
    count(statTurn, deltaRot);
    if (state::isFlightActive(*this)) {
        accumulate("localPlayerTurn", deltaRot);
        return;
    }
    origin(deltaRot);
}

// Once per rendered frame (after the game updated, so this frame's mouse input is already accumulated).
LL_TYPE_INSTANCE_HOOK(
    ClientUpdateHook,
    ll::memory::HookPriority::Normal,
    ClientInstance,
    &ClientInstance::$update,
    bool,
    bool isInitFinished
) {
    // Poll before the game update so that the stick state used to filter this frame's turn deltas is current.
    pollGamepad(isInitFinished && state::isHudTopScreen(*this));
    bool result = origin(isInitFinished);

    auto  now = Clock::now();
    float dt  = lastFrame.time_since_epoch().count() == 0
                  ? 0.0f
                  : std::chrono::duration<float>(now - lastFrame).count();
    lastFrame = now;
    dt        = std::clamp(dt, 0.0f, 0.25f);

    auto* player = getLocalPlayer();
    ++statFrames;
    if (state::config.debugLogging && now - lastStat > std::chrono::seconds(1)) {
        lastStat = now;
        if (player) {
            auto s = state::snapshot(*player);
            state::debugLog(
                "screen={} gliding={} rolling={} rot=({:.2f},{:.2f}) roll={:.2f} frames={} handled={} | "
                "_applyTurnDelta n={} sum=({:.3f},{:.3f}) | localPlayerTurn n={} sum=({:.3f},{:.3f}) | vel=({:.3f},{:.3f},{:.3f})",
                getTopScreenName(), s.isGliding, state::controller.isRolling(), s.pitch, s.yaw,
                state::controller.getRoll(), statFrames, statHandled, statApply.calls, statApply.sx, statApply.sy,
                statTurn.calls, statTurn.sx, statTurn.sy, s.velocity.x, s.velocity.y, s.velocity.z
            );
        } else {
            state::debugLog("no local player, screen={}", getTopScreenName());
        }
        state::debugLog(
            "gamepad: connected={} stick=({:.2f},{:.2f}) yawBtn={:.0f} rt={:.2f} lt={:.2f} active={} turnDeltasDropped={}",
            state::pad.connected, state::pad.axes.stickX, state::pad.axes.stickY, state::pad.axes.yaw, state::pad.rt,
            state::pad.lt, state::pad.stickActive, padDropped
        );
        padDropped = 0;
        state::debugLog(
            "frame deltas (1s): dPitch={:.3f} dYaw={:.3f} dRoll={:.3f} | idle frames (no mouse)={} idle |dRot|={:.3f} "
            "(must be 0 without banking/righting) banking={} own=({:.2f},{:.2f}) camRoll={:.2f} override={}",
            statDp, statDy, statDr, statIdleFrames, statIdleRot, state::config.enableBanking, ownPitch, ownYaw,
            state::cameraRollDeg, state::cameraOverride
        );
        statDp = statDy = statDr = statIdleRot = 0;
        statIdleFrames = 0;
        statApply = statTurn = {};
        statFrames = statHandled = 0;
    }
    if (!player) {
        state::mouseDx = state::mouseDy = 0.0;
        state::keys                      = {};
        state::pad.axes                  = {};
        state::cameraRollDeg             = 0.0f;
        state::cameraOverride            = false;
        state::hudVisible                = false;
        ownValid                         = false;
        return result;
    }
    if (!state::isHudTopScreen(*this)) {
        // Menu / overlay on top: no look input, but keep the camera state (a transient overlay must not snap the view
        // upright).
        state::mouseDx = state::mouseDy = 0.0;
        state::keys                      = {};
        state::pad.axes                  = {};
        return result;
    }

    // The controller keeps its own authoritative orientation while flying instead of re-reading the player's rotation
    // every frame (the game applies turn deltas lagged / clamped). Re-sync when the two disagree noticeably.
    auto actual = state::snapshot(*player);
    auto self   = actual;
    if (ownValid) {
        auto a = core::lookVector(actual.pitch, actual.yaw);
        auto b = core::lookVector(ownPitch, ownYaw);
        if (a.dot(b) < std::cos(15.0 * core::kToRad)) {
            state::debugLog(
                "resync own orientation: own=({:.2f},{:.2f}) actual=({:.2f},{:.2f})", ownPitch, ownYaw, actual.pitch,
                actual.yaw
            );
            ownValid = false;
        }
    }
    if (!ownValid) {
        ownPitch = actual.pitch;
        ownYaw   = actual.yaw;
        ownValid = true;
    }
    self.pitch = ownPitch;
    self.yaw   = ownYaw;

    core::FrameInput in;
    in.dt          = dt;
    in.mouseDx     = state::mouseDx;
    in.mouseDy     = state::mouseDy;
    in.keys        = state::keys;
    in.pad         = state::pad.axes;
    in.player      = self;
    state::mouseDx = state::mouseDy = 0.0;

    double rollBefore = state::controller.getRoll();
    auto   res        = state::controller.frame(in);
    if (res.handled) {
        ++statHandled;
        double dp = res.pitch - self.pitch;
        double dy = res.yaw - self.yaw;
        double dr = res.roll - rollBefore;
        statDp += dp;
        statDy += dy;
        statDr += dr;
        if (in.mouseDx == 0.0 && in.mouseDy == 0.0 && !in.pad.any()) {
            ++statIdleFrames;
            statIdleRot += std::abs(dp) + std::abs(dy);
        }
        // Loop transition diagnostics: yaw / roll flip by ~180 when the look passes vertical.
        bool flip = std::abs(std::remainder(dy, 360.0)) > 90.0 || std::abs(dr) > 90.0;
        if ((flip && flipLogCount < 200)
            || (std::abs(res.pitch) > 85.0 && now - lastNearVertical > std::chrono::milliseconds(100))) {
            lastNearVertical = now;
            if (flip) ++flipLogCount;
            state::debugLog(
                "{} pitch {:.2f}->{:.2f} yaw {:.2f}->{:.2f} roll {:.2f}->{:.2f} (dYaw={:.2f} dRoll={:.2f}) "
                "actual=({:.2f},{:.2f})",
                flip ? "LOOP-FLIP" : "near-vertical", self.pitch, res.pitch, self.yaw, res.yaw, rollBefore, res.roll, dy,
                dr, actual.pitch, actual.yaw
            );
        }
        ownPitch = res.pitch;
        ownYaw   = res.yaw;

        // Feed the result back through the game's own turn path (Vec2(-dPitch, dYaw), degrees), relative to the
        // actual player rotation so that any lag or clamping is corrected next time.
        double gp = res.pitch - actual.pitch;
        double gy = std::remainder(res.yaw - actual.yaw, 360.0);
        if (gp != 0.0 || gy != 0.0) {
            applyingOwnTurn = true;
            player->_applyTurnDelta(Vec2{static_cast<float>(-gp), static_cast<float>(gy)});
            applyingOwnTurn = false;
        }
        state::cameraOverride = true;
        state::cameraPitch    = res.pitch;
        state::cameraYaw      = res.yaw;
        state::cameraRollDeg  = static_cast<float>(state::config.cameraRollSign) * state::controller.getRoll();
    } else {
        ownValid              = false;
        state::cameraOverride = false;
        // Partial tick estimate for the decaying roll-back after flight ends.
        float tickDelta = lastTick.time_since_epoch().count() == 0
                            ? 1.0f
                            : std::clamp(std::chrono::duration<float>(now - lastTick).count() / 0.05f, 0.0f, 1.0f);
        state::cameraRollDeg = static_cast<float>(state::config.cameraRollSign) * state::controller.cameraRoll(tickDelta);
    }
    state::hudVisible = state::controller.isRolling();
    return result;
}

} // namespace

void markTick() { lastTick = Clock::now(); }

void installInput() {
    turnLogCount = 0;
    lastFrame    = {};
    ownValid     = false;
    state::controller.setTransitionLogger([](std::string const& m) { state::debugLog("{}", m); });
    ApplyTurnDeltaHook::hook();
    LocalPlayerTurnHook::hook();
    ClientUpdateHook::hook();
}

void uninstallInput() {
    ApplyTurnDeltaHook::unhook();
    LocalPlayerTurnHook::unhook();
    ClientUpdateHook::unhook();
}

} // namespace do_a_barrel_roll::hooks
