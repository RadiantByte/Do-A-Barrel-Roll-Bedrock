#include "mod/core/Math.h"

namespace do_a_barrel_roll::core {

Vec2& Vec2::normalize() {
    double len = length();
    if (len > 0.0) {
        x /= len;
        y /= len;
    }
    return *this;
}

Vec3 Vec3::rotatedX(double a) const {
    double c = std::cos(a), s = std::sin(a);
    return {x, y * c - z * s, y * s + z * c};
}

Vec3 Vec3::rotatedY(double a) const {
    double c = std::cos(a), s = std::sin(a);
    return {x * c + z * s, y, -x * s + z * c};
}

Vec3 Vec3::rotatedZ(double a) const {
    double c = std::cos(a), s = std::sin(a);
    return {x * c - y * s, x * s + y * c, z};
}

Vec3 Vec3::rotatedAxis(double angle, Vec3 axis) const {
    double len = axis.length();
    if (len == 0.0) return *this;
    Vec3   k = axis * (1.0 / len);
    double c = std::cos(angle), s = std::sin(angle);
    return *this * c + k.cross(*this) * s + k * (k.dot(*this) * (1.0 - c));
}

Vec3 lookVector(double pitchDeg, double yawDeg) {
    double p = pitchDeg * kToRad;
    double y = -yawDeg * kToRad;
    double h = std::cos(y), i = std::sin(y), j = std::cos(p), k = std::sin(p);
    // Vanilla: new Vec3d(i * j, -k, h * j)
    return {i * j, -k, h * j};
}

void forBresenhamLine(int x0, int y0, int x1, int y1, const std::function<void(int, int)>& consumer) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        consumer(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

LookResult changeLookInLocalFrame(
    double currentPitch,
    double currentYaw,
    double currentRoll,
    double pitch,
    double yaw,
    double roll
) {
    // Facing + left vector (left = player's local "right-hand" axis as the Java mod builds it).
    Vec3 facing = lookVector(currentPitch, currentYaw);
    Vec3 left{1, 0, 0};
    left = left.rotatedZ(-currentRoll * kToRad);
    left = left.rotatedX(-currentPitch * kToRad);
    left = left.rotatedY(-(currentYaw + 180) * kToRad);

    // Pitch
    facing = facing.rotatedAxis(-0.15 * pitch * kToRad, left);

    // Yaw (about local up)
    Vec3 up = facing.cross(left);
    facing  = facing.rotatedAxis(0.15 * yaw * kToRad, up);
    left    = left.rotatedAxis(0.15 * yaw * kToRad, up);

    // Roll (about facing)
    left = left.rotatedAxis(0.15 * roll * kToRad, facing);

    // Extract new angles
    double newPitch = -std::asin(clamp(facing.y, -1.0, 1.0)) * kToDeg;
    double newYaw   = -std::atan2(facing.x, facing.z) * kToDeg;

    Vec3   normalLeft = Vec3{1, 0, 0}.rotatedY(-(newYaw + 180) * kToRad);
    double newRoll    = -std::atan2(left.cross(normalLeft).dot(facing), left.dot(normalLeft)) * kToDeg;

    LookResult r{};
    r.deltaPitch = newPitch - currentPitch;
    r.deltaYaw   = newYaw - currentYaw;
    // Java feeds (newYaw - currentYaw) straight into yaw (vanilla yaw is unbounded). The atan2 result is in
    // [-180,180], so when the unbounded currentYaw is outside that range the naive delta would snap by
    // multiples of 360. Wrap the delta to (-180,180] so yaw stays continuous.
    r.deltaYaw = r.deltaYaw - 360.0 * std::floor((r.deltaYaw + 180.0) / 360.0);
    r.deltaRoll = newRoll - currentRoll;
    r.pitch     = currentPitch + r.deltaPitch;
    r.yaw       = currentYaw + r.deltaYaw;
    r.roll      = newRoll;
    return r;
}

} // namespace do_a_barrel_roll::core
