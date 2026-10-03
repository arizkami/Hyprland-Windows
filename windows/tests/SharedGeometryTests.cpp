#include "../../src/layout/algorithm/tiled/GeometryMath.hpp"
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Geometry = Layout::Tiled::GeometryMath;
static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        for (int size = 1; size < 16000; size += 7) {
            for (const float ratio : {
                     0.1F,
                     0.5F,
                     1.0F,
                     1.9F,
                 }) {
                const double extent = size + 0.125;
                // Exact original Dwindle evaluation and float assignment.
                const float original = extent / 2.0 * ratio;
                const float shared   = Geometry::dwindleExtent(extent, ratio);
                require(std::bit_cast<uint32_t>(original) == std::bit_cast<uint32_t>(shared), "Dwindle changed inherited rounding");
                const float masterFraction = ratio / 2;
                const float originalMaster = static_cast<float>(extent) * masterFraction;
                require(std::bit_cast<uint32_t>(originalMaster) == std::bit_cast<uint32_t>(Geometry::masterExtent(static_cast<float>(extent), masterFraction)),
                        "Master changed inherited rounding");
            }
            require(Geometry::masterPixels(size, 3, 5) == size * 3 / 5, "Native master pixels changed");
        }
        for (int count = 1; count < 200; ++count) {
            for (const bool extraRight : {
                     false,
                     true,
                 }) {
                const int  larger   = 1 + (count - 1) / 2;
                const auto expected = extraRight ? std::pair{count - larger, larger} : std::pair{larger, count - larger};
                require(Geometry::centerSlaveColumns(count, extraRight) == expected, "Centered master column distribution changed");
            }
        }
        const int maximum = std::numeric_limits<int>::max();
        require(Geometry::masterPixels(maximum, 3, 5) == static_cast<int>(static_cast<int64_t>(maximum) * 3 / 5), "Master pixel multiplication overflow");
        bool invalidRejected = false;
        try {
            Geometry::masterPixels(100, 1, 0);
        } catch (const std::invalid_argument&) { invalidRejected = true; }
        require(invalidRejected, "Invalid pixel fraction accepted");
        std::cout << "Inherited floating-point geometry parity and native overflow regression passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
