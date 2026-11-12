#include "../../../Dependencies/xr/Source/ARKit/CompositorCoordinates.h"

#include <gtest/gtest.h>

TEST(NativeXrCompositor, MapsTopAndBottomToTheCorrespondingTextureRows)
{
    EXPECT_FLOAT_EQ(xr::CompositorTextureV(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(xr::CompositorTextureV(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(xr::CompositorTextureV(0.0f), 0.5f);
}

TEST(NativeXrCompositor, PreservesProjectedDisplayPositionsOnBothSidesOfCenter)
{
    for (const float height : {720.0f, 1125.0f, 2436.0f})
    {
        for (const float fraction : {0.0f, 0.125f, 0.25f, 0.5f, 0.75f, 0.875f, 1.0f})
        {
            const float projectedY = height * fraction;
            const float clipY = 1.0f - 2.0f * projectedY / height;
            EXPECT_FLOAT_EQ(xr::CompositorTextureV(clipY) * height, projectedY);
        }
    }
}
