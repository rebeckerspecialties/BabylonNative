#include "../../../Dependencies/xr/Source/OpenXR/ProjectionExtent.h"
#include <gtest/gtest.h>
#include <limits>

TEST(NativeXrProjectionExtent, PreservesAspectWhenEitherLimitBinds)
{
    const xr::ProjectionExtent base{1216, 1344};
    const auto widthLimited = xr::ScaleProjectionExtent(base, {2048, 4096}, 2.0);
    EXPECT_EQ(widthLimited.Width, 2048u);
    EXPECT_EQ(widthLimited.Height, 2263u);
    const auto heightLimited = xr::ScaleProjectionExtent(base, {4096, 672}, 2.0);
    EXPECT_EQ(heightLimited.Width, 608u);
    EXPECT_EQ(heightLimited.Height, 672u);
}

TEST(NativeXrProjectionExtent, ClampsBeforeNarrowing)
{
    const auto large = xr::ScaleProjectionExtent({4096, 2048}, {8192, 8192}, std::numeric_limits<double>::max());
    EXPECT_EQ(large.Width, 8192u);
    EXPECT_EQ(large.Height, 4096u);
    const auto small = xr::ScaleProjectionExtent({4, 2}, {32, 32}, 0.01);
    EXPECT_EQ(small.Width, 1u);
    EXPECT_EQ(small.Height, 1u);
    EXPECT_THROW(xr::ScaleProjectionExtent({0, 1}, {32, 32}, 1.0), std::invalid_argument);
    EXPECT_THROW(xr::ScaleProjectionExtent({1, 1}, {32, 32}, std::numeric_limits<double>::infinity()), std::invalid_argument);
}
