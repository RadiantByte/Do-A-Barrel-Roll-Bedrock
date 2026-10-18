#pragma once

// Geometry-only ports of render/HorizonLineWidget and render/MomentumCrosshairWidget.
// Output is integer pixel (GUI-scaled) coordinates; the renderer plots Bresenham lines/pixels itself
// (forBresenhamLine in Math.h can generate the pixels, Java draws 1px white pixels with blankPixel).

#include "mod/core/Math.h"

#include <vector>

namespace do_a_barrel_roll::core {

struct IntLine {
    int x0, y0, x1, y1;
};

/// HorizonLineWidget.render: two line segments for the artificial horizon.
/// rollDeg/pitchDeg in degrees; scaledWidth/Height = GUI-scaled screen size.
[[nodiscard]] std::vector<IntLine> horizonLines(int scaledWidth, int scaledHeight, double rollDeg, double pitchDeg);

struct MomentumWidget {
    bool    hasLine = false;
    IntLine line{};          // valid if hasLine
    int     crosshairOffsetX = 0; // offset to apply to the crosshair sprite
    int     crosshairOffsetY = 0;
};

/// MomentumCrosshairWidget.render with FlightController::mouseTurnVec().
[[nodiscard]] MomentumWidget momentumWidget(int scaledWidth, int scaledHeight, Vec2 mouseTurnVec);

} // namespace do_a_barrel_roll::core
