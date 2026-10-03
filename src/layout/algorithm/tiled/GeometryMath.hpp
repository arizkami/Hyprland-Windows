#pragma once
#include <utility>

// Numerical policy shared by the original layouts and native platform adapters.
// BSD-3-Clause: inherited Hyprland mathematics, see ../../../../LICENSE.
namespace Layout::Tiled::GeometryMath {
    double              dwindleExtent(double extent, double splitRatio);
    float               masterExtent(float extent, float masterFraction);
    int                 masterPixels(int extent, int numerator, int denominator);
    std::pair<int, int> centerSlaveColumns(int slaveCount, bool extraToRight);
}
