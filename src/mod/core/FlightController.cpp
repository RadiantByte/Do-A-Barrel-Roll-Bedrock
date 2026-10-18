#include "mod/core/FlightController.h"

#include <algorithm>
#include <utility>

namespace do_a_barrel_roll::core {

namespace {
double fixNaN(double v) { return std::isnan(v) ? 0.0 : v; }
RotationInstant fixNaN(RotationInstant r) { return {fixNaN(r.pitch), fixNaN(r.yaw), fixNaN(r.roll)}; }
} // namespace

RotationInstant RotationInstant::addAbsolute(double x, double y, double currentRoll) const {
    double c = std::cos(currentRoll), s = std::sin(currentRoll);
    return {pitch - y * c - x * s, yaw - y * s + x * c, roll};
}

// ---- Activation ---------------------------------------------------------------------------------------------

bool FlightController::isFallFlying(const PlayerState& p) const {
    if (isFallFlyingRaw(p)) return true;
    // Grace: the glide flag dropped, but we were flying a moment ago and nothing explains it.
    return mGraceTicks > 0 && mRolling && mConfig.modEnabled && !p.onGround
        && !(mConfig.disableWhenSubmerged && p.isSubmergedInWater);
}

bool FlightController::isFallFlyingRaw(const PlayerState& p) const {
    if (!mForceEnabled) {
        bool hybrid = mConfig.activationBehaviour == ActivationBehaviour::Hybrid
                   || mConfig.activationBehaviour == ActivationBehaviour::HybridToggle;
        if (hybrid && !mActivation.thirdJump) return false;
        if (!mConfig.modEnabled) return false;
    }
    if (mConfig.disableWhenSubmerged && p.isSubmergedInWater) return false;
    return p.isGliding;
}

bool FlightController::onGlideStartCheck() {
    auto behaviour = mConfig.activationBehaviour;
    if (behaviour == ActivationBehaviour::Vanilla) return false;

    bool shouldCancel = behaviour == ActivationBehaviour::TripleJump;
    if (!mActivation.wasJumping) {
        mActivation.wasJumping = true;
        if (!mActivation.secondJump) {
            mActivation.secondJump = true;
            if (shouldCancel) return true;
        } else {
            mActivation.thirdJump = behaviour != ActivationBehaviour::HybridToggle || !mActivation.thirdJump;
        }
        return false;
    }
    return shouldCancel;
}

void FlightController::onTickMovementEnd(bool onGround, bool jumpHeld) {
    if (onGround) {
        mActivation.secondJump = false;
        mActivation.thirdJump  = false;
    }
    mActivation.wasJumping = jumpHeld;
}

// ---- Roll state ---------------------------------------------------------------------------------------------

float FlightController::getRoll(float tickDelta) const {
    if (tickDelta == 1.0f) return mRoll;
    return static_cast<float>(lerp(tickDelta, mPrevRoll, mRoll));
}

void FlightController::setRoll(float roll) {
    if (!std::isfinite(roll)) return;
    float last = mRoll;
    mRoll      = roll;
    if (roll < -90 && last > 90) mPrevRoll -= 360;
    else if (roll > 90 && last < -90) mPrevRoll += 360;
}

float FlightController::cameraRoll(float tickDelta) {
    if (mRolling) {
        float r = getRoll(tickDelta);
        mRollBack = mLastRollBack = r;
        return r;
    }
    return static_cast<float>(lerp(tickDelta, mLastRollBack, mRollBack));
}

void FlightController::tick(const PlayerState& p) {
    constexpr int kGraceTicks = 6;
    bool          raw         = isFallFlyingRaw(p);
    if (raw) mGraceTicks = kGraceTicks;
    else if (mGraceTicks > 0) --mGraceTicks;
    bool wasRolling = mRolling;
    // clientTick: clear smoothers (and momentum) when not flying.
    if (!isFallFlying(p)) {
        mPitchSmoother.clear();
        mYawSmoother.clear();
        mRollSmoother.clear();
    }
    // PlayerEntityMixin baseTick tail
    mRolling  = isFallFlying(p);
    if (mTransitionLogger && mRolling != wasRolling) {
        std::string why = mRolling ? "glide flag set"
                        : !mConfig.modEnabled ? "mod disabled"
                        : p.onGround ? "on ground"
                        : (mConfig.disableWhenSubmerged && p.isSubmergedInWater) ? "submerged"
                        : "glide flag dropped for " + std::to_string(kGraceTicks) + " ticks";
        mTransitionLogger(std::string("rolling ") + (mRolling ? "ON" : "OFF") + ": " + why);
    }
    mPrevRoll = mRoll;
    if (!mRolling) setRoll(0.0f);
    // CameraMixin#interpolateRollnt (decays ~half per tick while not rolling)
    if (!mRolling) {
        mLastRollBack = mRollBack;
        mRollBack -= mRollBack * 0.5f;
    }
}

// ---- Modifiers ----------------------------------------------------------------------------------------------

RotationInstant FlightController::banking(RotationInstant r, const FrameInput& in, double dt, double currentRoll) const {
    (void)in;
    double roll = currentRoll * kToRad;
    // Default expression formulas of ModConfig.advanced:
    //   banking_x = sin(roll) * cos(pitch) * 10 * banking_strength
    //   banking_y = (-1 + cos(roll)) * cos(pitch) * 10 * banking_strength
    // (roll/pitch converted with TO_RAD)
    double pitch = in.player.pitch * kToRad;
    double s     = mConfig.bankingStrength;
    double dX    = fixNaN(std::sin(roll) * std::cos(pitch) * 10 * s);
    double dY    = fixNaN((-1 + std::cos(roll)) * std::cos(pitch) * 10 * s);
    return r.addAbsolute(dX * dt, dY * dt, roll);
}

RotationInstant FlightController::reorient(RotationInstant r, double dt, double currentRoll) const {
    double roll     = currentRoll * kToRad;
    double strength = 10 * mConfig.rightingStrength;
    double rollDelta = 0;
    if (-kRollReorientCutoff < roll && roll < kRollReorientCutoff) rollDelta = -std::pow(roll, 3) / 3.0 + roll;
    return r.add(0, 0, -rollDelta * strength * dt);
}

RotationInstant FlightController::controlSurfaceEfficacy(RotationInstant r, const PlayerState& p) const {
    // Default for elevator/aileron/rudder formulas: velocity . look. Applied to (pitch, yaw, roll) respectively
    // (the same value for all three by default).
    double e = p.velocity.dot(lookVector(p.pitch, p.yaw));
    if (std::isnan(e)) e = 1;
    return r.multiply(e, e, e);
}

RotationInstant FlightController::applyModifiers(RotationInstant delta, const FrameInput& in, double dt, double currentRoll) {
    delta = fixNaN(delta);

    // EARLY_CAMERA_MODIFIERS, in ascending priority: configureRotation (1000) then buttonControls (2000).
    // 1) Mouse axis mapping: unless switch_roll_and_yaw, mouse X (in the yaw slot) becomes roll and vice versa.
    {
        double pitch = delta.pitch, yaw = delta.yaw, roll = delta.roll;
        if (!mConfig.switchRollAndYaw) std::swap(yaw, roll);
        if (mConfig.invertPitch) pitch = -pitch;
        delta = {pitch, yaw, roll};
    }
    // 2) Key controls (power 1800 deg-ish per second; scaled by sensitivity afterwards). Not affected by the swap.
    {
        double d = 1800.0 * dt, p = 0, y = 0, r = 0;
        if (in.keys.pitchUp) p -= d;
        if (in.keys.pitchDown) p += d;
        if (in.keys.yawLeft) y -= d;
        if (in.keys.yawRight) y += d;
        if (in.keys.rollLeft) r -= d;
        if (in.keys.rollRight) r += d;
        delta = delta.add(p, y, r);
    }
    delta = fixNaN(delta);

    // Sensitivity (desktop)
    delta = fixNaN(delta.applySensitivity(mConfig.desktop));

    // Gamepad (ControlifyCompat.applyToRotation): rate based, degrees-ish per second = axis * 1200 * sensitivity.controller.
    // Stick Y is pitch and stick X is roll (yaw with switch_roll_and_yaw, in which case the yaw buttons roll).
    if (in.pad.any()) {
        double m     = dt * 1200.0;
        double pitch = -in.pad.stickY;
        if (mConfig.invertPitch) pitch = -pitch;
        double roll = in.pad.stickX, yaw = in.pad.yaw;
        if (mConfig.switchRollAndYaw) std::swap(roll, yaw);
        delta = fixNaN(delta.add(
            pitch * m * mConfig.controller.pitch, yaw * m * mConfig.controller.yaw, roll * m * mConfig.controller.roll
        ));
    }

    // LATE_CAMERA_MODIFIERS (priority 1000): efficacy, smoothing, banking, reorient
    if (mConfig.simulateControlSurfaceEfficacy) delta = controlSurfaceEfficacy(delta, in.player);
    {
        const auto& sm = mConfig.smoothing;
        delta = {
            sm.pitch == 0 ? delta.pitch : mPitchSmoother.smooth(delta.pitch, 1 / sm.pitch * dt),
            sm.yaw == 0 ? delta.yaw : mYawSmoother.smooth(delta.yaw, 1 / sm.yaw * dt),
            sm.roll == 0 ? delta.roll : mRollSmoother.smooth(delta.roll, 1 / sm.roll * dt),
        };
    }
    if (mConfig.enableBanking) delta = banking(delta, in, dt, currentRoll);
    if (mConfig.automaticRighting) delta = reorient(delta, dt, currentRoll);
    return fixNaN(delta);
}

FrameResult FlightController::frame(const FrameInput& in) {
    FrameResult res;
    res.pitch = in.player.pitch;
    res.yaw   = in.player.yaw;
    res.roll  = mRoll;

    if (!mRolling) {
        mMouseTurnVec = {};
        return res;
    }

    double dt = in.dt;
    double cx = in.mouseDx, cy = in.mouseDy;
    Vec2   turn{cx, cy}; // (x -> yaw slot, y -> pitch slot)

    // MouseMixin#updateMouse
    if (mConfig.momentumBasedMouse) {
        mMouseTurnVec = mMouseTurnVec + Vec2{cx, cy} * (1.0 / 300.0);
        if (mMouseTurnVec.lengthSquared() > 1.0) mMouseTurnVec.normalize();
        Vec2   ready    = mMouseTurnVec;
        double deadzone = mConfig.momentumMouseDeadzone;
        if (ready.lengthSquared() < deadzone * deadzone) ready = {};
        ready = ready * (1200.0 * dt);
        turn  = ready;
    } else {
        mMouseTurnVec = {};
    }

    RotationInstant delta{turn.y, turn.x, 0.0};
    delta = applyModifiers(delta, in, dt, mRoll);

    LookResult look = changeLookInLocalFrame(in.player.pitch, in.player.yaw, mRoll, delta.pitch, delta.yaw, delta.roll);

    // vanilla changeLookDirection clamps pitch
    res.pitch = clamp(look.pitch, -90.0, 90.0);
    res.yaw   = look.yaw;

    mRoll += static_cast<float>(look.deltaRoll);
    mPrevRoll += static_cast<float>(look.deltaRoll);
    res.roll    = mRoll;
    res.handled = true;
    return res;
}

// ---- Thrust ---------------------------------------------------------------------------------------------

std::optional<ThrustResult>
FlightController::thrust(const PlayerState& p, const KeyStates& keys, long long tickCount, double throttleOverride) const {
    if (!mRolling || !mConfig.enableThrust) return std::nullopt;

    Vec3   rotation = lookVector(p.pitch, p.yaw);
    Vec3   velocity = p.velocity;
    double throttle = keys.thrustForward ? 1 : keys.thrustBackward ? -1 : 0;
    if (!std::isnan(throttleOverride)) throttle = throttleOverride; // ThrustEvents.modifyThrustInput hook

    ThrustResult out;
    if (mConfig.thrustParticles) {
        int density = static_cast<int>(clamp(throttle * 10, 0, 10));
        if (throttle > 0.1 && tickCount % (11 - density) == 0) out.spawnParticle = true;
    }

    double maxSpeed      = mConfig.maxThrust;
    double speedIncrease = std::max(maxSpeed - velocity.length() * sign(rotation.dot(velocity) * throttle), 0.0) / maxSpeed * throttle;
    double acceleration  = mConfig.thrustAcceleration * speedIncrease;
    out.velocityDelta    = rotation * acceleration;
    return out;
}

} // namespace do_a_barrel_roll::core
