#pragma once

// Engine-independent port of the client-side logic of "Do a Barrel Roll".
//
// Threading: single (client/render) thread. Call frame() once per rendered frame, tick() once per game tick (20 Hz).
//
// UNITS / CONVENTIONS (see Math.h): angles in DEGREES; pitch+ = down; yaw 0 = +Z (south); roll+ = right wing down
// (clockwise); dt in SECONDS; velocity in blocks/tick (Java getVelocity() units; Bedrock "velocity" per tick is believed
// identical, flag if thrust/banking efficacy feels wrong).
//  If Bedrock's pitch/yaw sign conventions differ from the above, negate/offset at the hook boundary; all
//  conversions are confined to FlightController::PlayerState (inputs) and FrameResult (outputs).

#include "mod/core/Config.h"
#include "mod/core/Math.h"
#include "mod/core/Smoother.h"

#include <functional>
#include <optional>
#include <string>

namespace do_a_barrel_roll::core {

/// api/rotation/RotationInstant: a (pitch, yaw, roll) triple of deltas or angles. Mutable value type.
struct RotationInstant {
    double pitch = 0.0;
    double yaw   = 0.0;
    double roll  = 0.0;

    [[nodiscard]] RotationInstant add(double p, double y, double r) const { return {pitch + p, yaw + y, roll + r}; }
    [[nodiscard]] RotationInstant multiply(double p, double y, double r) const { return {pitch * p, yaw * y, roll * r}; }
    /// Add upright (screen-space) rotation x/y rotated into the rolled frame. currentRoll in RADIANS.
    [[nodiscard]] RotationInstant addAbsolute(double x, double y, double currentRoll) const;
    [[nodiscard]] RotationInstant applySensitivity(const Sensitivity& s) const {
        return {pitch * s.pitch, yaw * s.yaw, roll * s.roll};
    }
};

struct KeyStates {
    bool rollLeft = false, rollRight = false;
    bool yawLeft = false, yawRight = false;
    bool pitchUp = false, pitchDown = false;
    bool thrustForward = false, thrustBackward = false;
};

/// Snapshot of the local player, supplied by the hook layer.
struct PlayerState {
    double pitch = 0.0; // degrees, +down
    double yaw   = 0.0; // degrees
    Vec3   velocity;    // blocks per tick
    bool   isGliding         = false; // elytra gliding (Java isGliding / fall flying)
    bool   isSubmergedInWater = false;
    bool   onGround           = false;
    bool   isJumping          = false; // jump key currently held (input.playerInput.jump())
};

/// Gamepad axes after deadzone, each in [-1, 1]. Port of ControlifyCompat.applyToRotation (rate based input).
struct GamepadAxes {
    double stickX = 0.0; // right stick, +right
    double stickY = 0.0; // right stick, +up
    double yaw    = 0.0; // yaw buttons (bumpers), +right
    [[nodiscard]] bool any() const { return stickX != 0.0 || stickY != 0.0 || yaw != 0.0; }
};

struct FrameInput {
    double      dt = 0.0;     // seconds since previous frame (Java Mouse "timeDelta")
    double      mouseDx = 0.0; // raw cursor delta this frame (Java: cursorDeltaX), pixel-ish units
    double      mouseDy = 0.0;
    KeyStates   keys;
    GamepadAxes pad;
    PlayerState player;
};

struct FrameResult {
    /// True if the mod owns the camera this frame: the caller must NOT apply vanilla look changes and must use
    /// the pitch/yaw below. False => do nothing (vanilla handles input).
    bool   handled = false;
    double pitch   = 0.0; // new world pitch, degrees (clamped to [-90, 90])
    double yaw     = 0.0; // new world yaw, degrees (continuous, unbounded like vanilla)
    double roll    = 0.0; // new roll, degrees (also stored in the controller; read with getRoll())
};

struct ThrustResult {
    Vec3 velocityDelta;     // add to the glide velocity (Java adds it to the vanilla-computed elytra velocity)
    bool spawnParticle = false; // campfire_signal_smoke at (pos - velocity*0.5)
};

/// util/MixinHooks + the activation logic of PlayerEntityMixin#checkGliding.
struct ActivationState {
    bool secondJump = false;
    bool thirdJump  = false;
    bool wasJumping = false;
};

class FlightController {
public:
    explicit FlightController(Config& config) : mConfig(config) {}

    // ---- Activation -----------------------------------------------------------------------------------------

    /// DoABarrelRollClient.isFallFlying(): "mod currently controls flight". Includes a short grace period after the
    /// raw glide flag drops (Bedrock may clear the flag for single ticks), unless the player landed or is submerged.
    [[nodiscard]] bool isFallFlying(const PlayerState& p) const;
    /// Same without the grace period.
    [[nodiscard]] bool isFallFlyingRaw(const PlayerState& p) const;

    /// Receives human readable rolling on/off transition reasons (debug logging).
    void setTransitionLogger(std::function<void(const std::string&)> f) { mTransitionLogger = std::move(f); }

    /// Server override equivalent (LimitedModConfigServer.forceEnabled). Default false.
    void setForceEnabled(bool v) { mForceEnabled = v; }

    /// Port of PlayerEntityMixin#doABarrelRoll$interceptFallFlyingStart. Call at the point where the game
    /// decides whether a jump should start gliding (only reached while jump is pressed in the air; not on
    /// ground/in water/levitating - caller checks those, as Java returns early for them).
    /// @return true if the glide start must be CANCELLED (TRIPLE_JUMP behaviour only); false to let it proceed.
    [[nodiscard]] bool onGlideStartCheck();

    /// Port of ClientPlayerEntityMixin#resetJump (tickMovement RETURN). Call once per tick after movement.
    void onTickMovementEnd(bool onGround, bool jumpHeld);

    [[nodiscard]] const ActivationState& activation() const { return mActivation; }

    // ---- Per-frame -----------------------------------------------------------------------------------------

    /// Mouse/keyboard -> rotation. Port of MouseMixin#updateMouse + ClientPlayerEntityMixin#changeElytraLook
    /// including momentum mouse, key controls, sensitivity, smoothing, banking, auto-righting.
    /// Call exactly once per frame (also with zero mouse delta; momentum, key-rotation, banking and smoothing
    /// all need to run every frame while flying).
    [[nodiscard]] FrameResult frame(const FrameInput& in);

    // ---- Per-tick -----------------------------------------------------------------------------------------

    /// Port of baseTick tail + ClientEvents.clientTick + Camera rollBack decay. Call once per game tick (20 Hz).
    /// Updates rolling flag, prevRoll, resets roll to 0 when not rolling, clears smoothers/momentum when not flying.
    void tick(const PlayerState& p);

    // ---- Thrust ---------------------------------------------------------------------------------------------

    /// Port of LivingEntityMixin#wrapElytraVelocity. Call each tick while gliding, where vanilla computes the
    /// glide velocity. `tickCount` is the world game time (for particle cadence). Returns nullopt if thrust
    /// disabled / not rolling.
    [[nodiscard]] std::optional<ThrustResult>
    thrust(const PlayerState& p, const KeyStates& keys, long long tickCount, double throttleOverride = NAN) const;

    // ---- Roll state / camera -------------------------------------------------------------------------------

    [[nodiscard]] bool   isRolling() const { return mRolling; }
    [[nodiscard]] float  getRoll() const { return mRoll; }
    /// Interpolated roll (PlayerEntityMixin#getRoll(tickDelta)); tickDelta in [0,1].
    [[nodiscard]] float getRoll(float tickDelta) const;
    void                setRoll(float roll);

    /// Camera roll to apply this frame, player-roll convention (positive = right wing down). While rolling
    /// returns the interpolated player roll; otherwise a decaying value that eases back to 0 (CameraMixin
    /// rollBack). Call once per rendered frame (it records state for the decay).
    /// Java feeds the NEGATED value to its camera quaternion rotationYXZ(yaw, pitch, roll_rad); verify the
    /// sign for the Bedrock camera/view matrix.
    [[nodiscard]] float cameraRoll(float tickDelta);

    // ---- HUD helpers ---------------------------------------------------------------------------------------

    /// Momentum vector (MouseMixin.mouseTurnVec); length <= 1. Zero when momentum mouse is off.
    [[nodiscard]] Vec2 mouseTurnVec() const { return mMouseTurnVec; }

    Config& config() { return mConfig; }

private:
    RotationInstant applyModifiers(RotationInstant delta, const FrameInput& in, double dt, double currentRoll);
    RotationInstant banking(RotationInstant r, const FrameInput& in, double dt, double currentRoll) const;
    RotationInstant reorient(RotationInstant r, double dt, double currentRoll) const;
    RotationInstant controlSurfaceEfficacy(RotationInstant r, const PlayerState& p) const;

    Config&         mConfig;
    ActivationState mActivation;
    bool            mForceEnabled = false;

    Smoother mPitchSmoother, mYawSmoother, mRollSmoother;
    Vec2     mMouseTurnVec;

    std::function<void(const std::string&)> mTransitionLogger;
    int                                     mGraceTicks = 0; // ticks left in which a dropped glide flag is ignored

    bool  mRolling  = false;
    float mRoll     = 0.0f;
    float mPrevRoll = 0.0f;

    float mRollBack     = 0.0f;
    float mLastRollBack = 0.0f;
};

/// RotationModifiers.ROLL_REORIENT_CUTOFF
inline const double kRollReorientCutoff = std::sqrt(10.0 / 3.0);

} // namespace do_a_barrel_roll::core
