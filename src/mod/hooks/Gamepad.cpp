// Gamepad flight controls.
//
// How controller look reaches the game: the right stick is turned into a look delta by the game (InGamePlayScreen::
// handleDirection -> applyInput) and ends up in the very same LocalPlayer::localPlayerTurn / _applyTurnDelta path as
// the mouse, as a per-frame delta with no marker telling the two apart. Treating it as mouse input would give the wrong
// scaling and no roll mapping, so Input.cpp drops turn deltas while the stick is deflected and the stick is read here
// directly via XInput (loaded dynamically, works under Wine/Proton) and fed to FlightController as rate input
// (ControlifyCompat.applyToRotation: axis * dt * 1200 * sensitivity.controller).
//
// Default mapping (Java/Controlify has no default gamepad binds for the mod, so these are chosen here):
//   right stick X  roll          (yaw instead when general.controls.switch_roll_and_yaw)
//   right stick Y  pitch         (stick up = nose up; flipped by general.controls.invert_pitch)
//   LB / RB        yaw left/right (roll instead when switch_roll_and_yaw)
//   RT / LT        thrust forward / backward
// Config: general.misc.enable_gamepad, general.misc.gamepad_deadzone.

#include "mod/hooks/State.h"

#include "ll/api/memory/Hook.h"
#include "mc/client/gui/screens/InGamePlayScreen.h"
#include "mc/client/player/LocalPlayer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace do_a_barrel_roll::hooks {

namespace {

using Clock = std::chrono::steady_clock;

// Minimal XInput ABI (avoids depending on the SDK header / import library).
struct XGamepad {
    WORD  wButtons;
    BYTE  bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct XState {
    DWORD    dwPacketNumber;
    XGamepad Gamepad;
};
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XState*);

constexpr WORD kLeftShoulder  = 0x0100;
constexpr WORD kRightShoulder = 0x0200;
constexpr double kTriggerThreshold = 0.3;

XInputGetStateFn getState = nullptr;
bool             loaded   = false;
bool             active   = false;
Clock::time_point lastStickActive{};
Clock::time_point lastLog{}, lastDirLog{};
int              dirCount = 0;
double           dirSumX = 0, dirSumY = 0;
bool             wasConnected = false;
bool             wasStickActive = false;

void load() {
    if (loaded) return;
    loaded = true;
    for (auto const* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        if (HMODULE mod = LoadLibraryW(name)) {
            getState = reinterpret_cast<XInputGetStateFn>(GetProcAddress(mod, "XInputGetState"));
            if (getState) {
                state::debugLog("gamepad: using XInput from {}", std::filesystem::path(name).string());
                return;
            }
        }
    }
    state::debugLog("gamepad: XInput not available");
}

bool gameFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

double axis(SHORT v) { return std::clamp(static_cast<double>(v) / (v < 0 ? 32768.0 : 32767.0), -1.0, 1.0); }

// Radial deadzone with rescaling so the output ramps from 0 at the edge of the deadzone.
void radial(double& x, double& y, double dz) {
    double mag = std::hypot(x, y);
    if (mag <= dz) {
        x = y = 0.0;
        return;
    }
    double scaled = std::min((mag - dz) / (1.0 - dz), 1.0) / mag;
    x *= scaled;
    y *= scaled;
}

// Evidence for which path the game's stick look takes (debug_logging only).
LL_TYPE_INSTANCE_HOOK(
    HandleDirectionHook,
    ll::memory::HookPriority::Normal,
    InGamePlayScreen,
    &InGamePlayScreen::$handleDirection,
    void,
    DirectionId directionId,
    float       x,
    float       y,
    FocusImpact focusImpact
) {
    if (state::config.debugLogging) {
        ++dirCount;
        dirSumX += x;
        dirSumY += y;
        auto now = Clock::now();
        if ((x != 0.0f || y != 0.0f) && now - lastDirLog > std::chrono::milliseconds(500)) {
            lastDirLog = now;
            state::debugLog(
                "gamepad: handleDirection id={} x={:.3f} y={:.3f} (padStickActive={}, calls this second={})",
                static_cast<int>(directionId), x, y, state::pad.stickActive, dirCount
            );
        }
    }
    origin(directionId, x, y, focusImpact);
}

// Thrust: the controller's thrust reads state::keys in the player tick; Keys.cpp rewrites them every frame, so the
// triggers are OR-ed in right before each tick.
LL_TYPE_INSTANCE_HOOK(
    PadThrustTickHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::$normalTick,
    void
) {
    if (state::pad.connected) {
        if (state::pad.rt > kTriggerThreshold) state::keys.thrustForward = true;
        if (state::pad.lt > kTriggerThreshold) state::keys.thrustBackward = true;
    }
    origin();
}

} // namespace

void pollGamepad(bool allow) {
    auto& pad = state::pad;
    pad.axes = {};
    pad.rt = pad.lt = 0.0;
    auto now = Clock::now();
    if (!active || !state::config.enableGamepad) {
        pad.connected   = false;
        pad.stickActive = false;
        return;
    }
    load();
    XState st{};
    pad.connected = getState && getState(0, &st) == ERROR_SUCCESS;
    if (pad.connected != wasConnected) {
        wasConnected = pad.connected;
        state::debugLog("gamepad: {}", pad.connected ? "connected (XInput slot 0)" : "disconnected");
    }
    if (!pad.connected) {
        pad.stickActive = false;
        return;
    }

    double rx = axis(st.Gamepad.sThumbRX), ry = axis(st.Gamepad.sThumbRY);
    radial(rx, ry, std::clamp(state::config.gamepadDeadzone, 0.0, 0.95));
    bool stick = rx != 0.0 || ry != 0.0;
    // Also while unfocused: the game may still see the stick; we just must not treat it as flight input then.
    if (stick) lastStickActive = now;
    pad.stickActive = stick || now - lastStickActive < std::chrono::milliseconds(150);
    if (pad.stickActive != wasStickActive) {
        wasStickActive = pad.stickActive;
        state::debugLog("gamepad: right stick {}", pad.stickActive ? "active (game look deltas are dropped)" : "idle");
    }
    if (!allow || !gameFocused()) return;

    pad.axes.stickX = rx;
    pad.axes.stickY = ry;
    pad.axes.yaw    = ((st.Gamepad.wButtons & kRightShoulder) ? 1.0 : 0.0) - ((st.Gamepad.wButtons & kLeftShoulder) ? 1.0 : 0.0);
    pad.rt          = st.Gamepad.bRightTrigger / 255.0;
    pad.lt          = st.Gamepad.bLeftTrigger / 255.0;

    if (state::config.debugLogging && pad.axes.any() && now - lastLog > std::chrono::milliseconds(500)) {
        lastLog = now;
        state::debugLog(
            "gamepad: XInput path stick=({:.2f},{:.2f}) yawBtn={:.0f} rt={:.2f} lt={:.2f}", rx, ry, pad.axes.yaw, pad.rt,
            pad.lt
        );
    }
}

void installGamepad() {
    active = true;
    HandleDirectionHook::hook();
    PadThrustTickHook::hook();
}

void uninstallGamepad() {
    active = false;
    HandleDirectionHook::unhook();
    PadThrustTickHook::unhook();
    state::pad = {};
}

} // namespace do_a_barrel_roll::hooks
