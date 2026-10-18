#pragma once

// Engine-independent math helpers for the Do a Barrel Roll port.
//
// CONVENTIONS (inherited from Java Minecraft, which the original mod targets):
//  * Angles stored in "player" variables (pitch/yaw/roll) are DEGREES.
//  * Internal trig is done in RADIANS (see kToRad / kToDeg, = MagicNumbers.TORAD/TODEG).
//  * Pitch: positive = looking DOWN, range [-90, 90].
//  * Yaw: 0 = +Z (south), +90 = -X (west), i.e. look vector =
//        (-sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch)).
//  * Roll: positive = clockwise as seen by the player (right wing down).
//  UNCERTAIN: Bedrock is believed to share the pitch/yaw convention above (pitch+ = down, yaw 0 = +Z).
//  If the hook layer finds otherwise, convert at the boundary (see FlightController docs) rather than
//  editing the math here.

#include <cmath>
#include <functional>

namespace do_a_barrel_roll::core {

inline constexpr double kPi    = 3.14159265358979323846;
inline constexpr double kToRad = kPi / 180.0; // MagicNumbers.TORAD
inline constexpr double kToDeg = 180.0 / kPi; // MagicNumbers.TODEG

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    [[nodiscard]] double lengthSquared() const { return x * x + y * y; }
    [[nodiscard]] double length() const { return std::sqrt(lengthSquared()); }
    Vec2&                normalize();
    Vec2                 operator*(double s) const { return {x * s, y * s}; }
    Vec2                 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2                 operator-() const { return {-x, -y}; }
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    [[nodiscard]] double dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    [[nodiscard]] Vec3   cross(Vec3 o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    [[nodiscard]] double length() const { return std::sqrt(dot(*this)); }

    // Right-handed rotations, identical to JOML's Vector3d.rotateX/Y/Z (angle in radians).
    [[nodiscard]] Vec3 rotatedX(double a) const;
    [[nodiscard]] Vec3 rotatedY(double a) const;
    [[nodiscard]] Vec3 rotatedZ(double a) const;
    // Rodrigues rotation around a (unit) axis, same as JOML's rotateAxis.
    [[nodiscard]] Vec3 rotatedAxis(double angle, Vec3 axis) const;
};

[[nodiscard]] inline double lerp(double t, double a, double b) { return a + t * (b - a); }
[[nodiscard]] inline double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
[[nodiscard]] inline double sign(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }

/// Vanilla Entity#getRotationVector / getRotationVecClient. Degrees in.
[[nodiscard]] Vec3 lookVector(double pitchDeg, double yawDeg);

/// Port of ModMath.forBresenhamLine (integer line rasterisation, endpoints inclusive).
void forBresenhamLine(int x0, int y0, int x1, int y1, const std::function<void(int, int)>& consumer);

/// Result of changeLookInLocalFrame.
struct LookResult {
    double pitch; // degrees (unclamped; caller clamps like vanilla if wanted)
    double yaw;   // degrees, continuous with the input yaw (input yaw + wrapped delta)
    double roll;  // degrees, in [-180, 180]
    double deltaPitch;
    double deltaYaw;
    double deltaRoll; // newRoll - currentRoll (may be ~+-360 across the wrap; used for prevRoll bookkeeping)
};

/// Port of ClientPlayerEntityMixin#doABarrelRoll$changeElytraLook(float pitch, float yaw, float roll).
/// pitchIn/yawIn/rollIn are the *already sensitivity-scaled* deltas in "mouse units"; like vanilla they are
/// multiplied by 0.15 to get degrees. Applies pitch about the player's local left axis, yaw about the local
/// up axis, roll about the facing axis, then re-derives world pitch/yaw/roll.
[[nodiscard]] LookResult changeLookInLocalFrame(
    double currentPitchDeg,
    double currentYawDeg,
    double currentRollDeg,
    double pitchIn,
    double yawIn,
    double rollIn
);

} // namespace do_a_barrel_roll::core
