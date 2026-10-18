// Keybinds via ll::input::KeyRegistry (keys appear in the controls screen under the mod's name; key codes are
// Windows virtual-key codes). Handlers cannot be unregistered, so they are gated by `active`.
//
// Defaults follow the Java mod: toggle = I, yaw left/right = A/D, thrust forward = W; the rest are unbound
// (bind them in the controls menu). Note A/D/W also strafe/walk in vanilla.

#include "mod/hooks/Feedback.h"
#include "mod/hooks/State.h"

#include "ll/api/input/KeyRegistry.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/ClientInstance.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "user32.lib")
#include <vector>
#include "mod/DoABarrelRoll.h"

namespace do_a_barrel_roll::hooks {

namespace {

bool active     = false;
bool registered = false;

struct Held {
    char const*                 name;
    ll::input::KeyHandle*       handle;
    bool core::KeyStates::*     field;
    bool                        last = false;
};
std::vector<Held> helds;

// Registers the key for the controls screen. The held state itself is polled (see ClientUpdateKeys), because the
// registry's down/up handlers are not reliable for continuous state.
void hold(char const* name, std::vector<int> codes, bool core::KeyStates::*field) {
    auto& key = ll::input::KeyRegistry::getInstance().getOrCreateKey(name, codes, true);
    helds.push_back({name, &key, field});
}

void press(char const* name, std::vector<int> codes, void (*fn)()) {
    auto& key = ll::input::KeyRegistry::getInstance().getOrCreateKey(name, codes, true);
    key.registerButtonDownHandler([fn](FocusImpact, IClientInstance&) {
        if (active) fn();
    });
}

bool gameFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// Polls the held keys once per rendered frame, only while the HUD is the top screen and the window is focused.
LL_TYPE_INSTANCE_HOOK(
    PollKeysHook,
    ll::memory::HookPriority::Normal,
    ClientInstance,
    &ClientInstance::$update,
    bool,
    bool isInitFinished
) {
    if (active) {
        if (isInitFinished) flushMessages(*this);
        bool const poll = isInitFinished && state::isHudTopScreen(*this) && gameFocused();
        for (auto& h : helds) {
            bool down = false;
            if (poll) {
                for (int code : h.handle->getKeyCodes()) {
                    if (code > 0 && code < 256 && (GetAsyncKeyState(code) & 0x8000)) {
                        down = true;
                        break;
                    }
                }
            }
            state::keys.*h.field = down;
            if (down != h.last) {
                h.last = down;
                state::debugLog("key {} -> {}", h.name, down ? "DOWN" : "up");
            }
        }
    }
    return origin(isInitFinished);
}

void save() {
    if (!state::config.save(state::configPath)) {
        DoABarrelRoll::getInstance().getSelf().getLogger().warn("Failed to save config");
    }
}

} // namespace

void installKeys() {
    active = true;
    PollKeysHook::hook();
    if (registered) return;
    registered = true;
    using K    = core::KeyStates;
    constexpr int kI = 0x49, kA = 0x41, kD = 0x44, kW = 0x57;

    press("key.do_a_barrel_roll.toggle_enabled", {kI}, [] {
        state::config.modEnabled = !state::config.modEnabled;
        DoABarrelRoll::getInstance().getSelf().getLogger().info(
            "Do a Barrel Roll {}", state::config.modEnabled ? "enabled" : "disabled"
        );
        showMessage(
            state::config.modEnabled ? "\xC2\xA7" "aEnabled \xC2\xA7rDo a Barrel Roll"
                                     : "\xC2\xA7" "cDisabled \xC2\xA7rDo a Barrel Roll"
        );
        save();
    });
    press("key.do_a_barrel_roll.toggle_thrust", {}, [] {
        state::config.enableThrust = !state::config.enableThrust;
        DoABarrelRoll::getInstance().getSelf().getLogger().info(
            "Thrust {}", state::config.enableThrust ? "enabled" : "disabled"
        );
        showMessage(
            state::config.enableThrust ? "\xC2\xA7" "aEnabled \xC2\xA7rThrusting"
                                       : "\xC2\xA7" "cDisabled \xC2\xA7rThrusting"
        );
        save();
    });
    hold("key.do_a_barrel_roll.pitch_up", {}, &K::pitchUp);
    hold("key.do_a_barrel_roll.pitch_down", {}, &K::pitchDown);
    hold("key.do_a_barrel_roll.yaw_left", {kA}, &K::yawLeft);
    hold("key.do_a_barrel_roll.yaw_right", {kD}, &K::yawRight);
    hold("key.do_a_barrel_roll.roll_left", {}, &K::rollLeft);
    hold("key.do_a_barrel_roll.roll_right", {}, &K::rollRight);
    hold("key.do_a_barrel_roll.thrust_forward", {kW}, &K::thrustForward);
    hold("key.do_a_barrel_roll.thrust_backward", {}, &K::thrustBackward);
}

void uninstallKeys() {
    active = false;
    PollKeysHook::unhook();
    state::keys = {};
    for (auto& h : helds) h.last = false;
}

} // namespace do_a_barrel_roll::hooks
