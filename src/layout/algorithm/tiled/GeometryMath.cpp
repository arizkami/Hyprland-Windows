#include "GeometryMath.hpp"
#include <cstdint>
#include <stdexcept>

namespace Layout::Tiled::GeometryMath {
    double dwindleExtent(double extent, double splitRatio) {
        // Keep the original operation order and caller-owned ratio policy.
        return extent / 2.0 * splitRatio;
    }

    float masterExtent(float extent, float masterFraction) {
        return extent * masterFraction;
    }

    int masterPixels(int extent, int numerator, int denominator) {
        if (extent < 0 || numerator < 0 || denominator <= 0 || numerator > denominator)
            throw std::invalid_argument("Invalid master pixel fraction");
        // Native integer coordinates need exact truncation without float error
        // or overflow. The Linux floating-point API above retains its precision.
        return static_cast<int>(static_cast<int64_t>(extent) * numerator / denominator);
    }

    std::pair<int, int> centerSlaveColumns(int slaveCount, bool extraToRight) {
        const int LARGER  = 1 + (slaveCount - 1) / 2;
        const int SMALLER = slaveCount - LARGER;
        return extraToRight ? std::pair{SMALLER, LARGER} : std::pair{LARGER, SMALLER};
    }
}
