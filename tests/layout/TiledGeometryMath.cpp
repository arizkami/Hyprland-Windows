#include "../../src/layout/algorithm/tiled/GeometryMath.hpp"
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>

namespace Geometry = Layout::Tiled::GeometryMath;

TEST(TiledGeometryMath, PreservesOriginalDwindleRounding) {
    for (const double extent : {
             1.25,
             1377.75,
             3840.0,
         }) {
        for (const float ratio : {
                 0.1F,
                 1.0F,
                 1.9F,
             }) {
            const float inherited = extent / 2.0 * ratio;
            const float shared    = Geometry::dwindleExtent(extent, ratio);
            EXPECT_EQ(std::bit_cast<uint32_t>(inherited), std::bit_cast<uint32_t>(shared));
        }
    }
}

TEST(TiledGeometryMath, PreservesOriginalMasterArithmetic) {
    EXPECT_FLOAT_EQ(Geometry::masterExtent(1919.5F, 0.6F), 1919.5F * 0.6F);
    EXPECT_EQ(Geometry::centerSlaveColumns(5, false), (std::pair{3, 2}));
    EXPECT_EQ(Geometry::centerSlaveColumns(5, true), (std::pair{2, 3}));
    EXPECT_EQ(Geometry::centerSlaveColumns(4, true), (std::pair{2, 2}));
}
