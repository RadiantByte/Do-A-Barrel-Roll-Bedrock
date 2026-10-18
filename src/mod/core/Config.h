#pragma once

// Port of nl.enjarai.doabarrelroll.config.ModConfig (client-side parts only).
// Skipped: server sync/limited server config, expression parser formulas (banking / control-surface
// efficacy use the hardcoded Java defaults, see FlightController), Realms/YACL/ModMenu bits, migrations.

#include <filesystem>
#include <string>

namespace do_a_barrel_roll::core {

/// config/ActivationBehaviour.java
enum class ActivationBehaviour {
    Vanilla,     // VANILLA: normal elytra / glide activation
    TripleJump,  // TRIPLE_JUMP: vanilla activation cancelled until third jump press
    Hybrid,      // HYBRID: vanilla activation, but mod only takes over after a third jump press
    HybridToggle // HYBRID_TOGGLE: like HYBRID, third jump press toggles mod control
};

/// config/Sensitivity.java (note the Java defaults: pitch 1, yaw 0.4, roll 1)
struct Sensitivity {
    double pitch = 1.0;
    double yaw   = 0.4;
    double roll  = 1.0;
};

struct Config {
    int formatVersion = 2;

    // general
    bool modEnabled = true;

    // general.controls
    bool                switchRollAndYaw      = false; // false => mouse X rolls, true => mouse X yaws
    bool                invertPitch           = false;
    bool                momentumBasedMouse    = false;
    double              momentumMouseDeadzone = 0.2;
    bool                showMomentumWidget    = true;
    ActivationBehaviour activationBehaviour   = ActivationBehaviour::Vanilla;
    bool                disableWhenSubmerged  = true;

    // general.hud
    bool showHorizon = false;

    // general.banking
    bool   enableBanking                = true;
    double bankingStrength              = 20.0;
    bool   simulateControlSurfaceEfficacy = false;
    bool   automaticRighting            = false;
    double rightingStrength             = 50.0;

    // general.thrust (Java additionally requires server permission; here client flag only)
    bool   enableThrust       = false;
    double maxThrust          = 2.0;
    double thrustAcceleration = 0.1;
    bool   thrustParticles    = true;

    bool enableEasterEggs = true;

    // Bedrock port calibration (not in the Java mod): see src/mod/hooks/Input.cpp / Camera.cpp.
    bool   debugLogging   = false; // log mouse turn deltas / hook activity
    double turnDeltaScale = 1.0;   // multiplier on the Bedrock turn delta (assumed degrees) before 1/0.15 conversion
    bool   swapTurnAxes   = false; // true if the turn Vec2 is (yaw, pitch) instead of (pitch, yaw)
    bool   thrustDirectWrite = false; // thrust fallback: write posDelta directly instead of Actor::lerpMotion
    bool   thrustAllowRemote = false; // apply thrust even when no in-process (integrated) server accepts it: rubber-bands on vanilla servers
    bool   enableGamepad     = true; // XInput gamepad flight controls (right stick pitch/roll, bumpers yaw, triggers thrust)
    double gamepadDeadzone   = 0.15; // radial stick deadzone, 0..1
    double cameraRollSign = 1.0;   // +1 / -1: flip camera roll direction if it rolls the wrong way

    // sensitivity.camera_smoothing: 0 disables smoothing for that axis. Larger = smoother (more lag).
    Sensitivity smoothing{1.0, 2.5, 1.0};
    // sensitivity.desktop / sensitivity.controller
    Sensitivity desktop;
    Sensitivity controller;

    /// Loads from `path` (same nested JSON layout as the Java mod's do_a_barrel_roll-client.json).
    /// Missing file/fields keep defaults. Returns false only if the file existed but was unparsable.
    bool load(const std::filesystem::path& path);
    /// Writes pretty-printed JSON. Creates parent dirs. Returns false on I/O failure.
    bool save(const std::filesystem::path& path) const;
};

[[nodiscard]] const char* toString(ActivationBehaviour b);

} // namespace do_a_barrel_roll::core
