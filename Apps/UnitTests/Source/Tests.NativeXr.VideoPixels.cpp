#include "../../../Plugins/NativeXr/Source/VideoYuv420.h"
#include <gtest/gtest.h>
#include <random>
#include <vector>

TEST(NativeXrVideoPixels, MatchesScalarColorForCroppedPlanarAndInterleavedRows)
{
    std::mt19937 random{42};
    for (bool hd : {false, true})
    for (bool full : {false, true})
    for (int yStep : {1, 2})
    for (int uStep : {1, 2, 3})
    for (int vStep : {1, 2, 3})
    for (int left : {0, 1, 2, 3})
    for (int width : {0, 1, 15, 16, 17, 31, 32, 33, 129})
    {
        const int right = left + width;
        auto plane = [&](int n, int step) {
            std::vector<uint8_t> p(n == 0 ? 0 : (n - 1) * step + 1);
            for (auto& val : p) val = static_cast<uint8_t>(random());
            return p;
        };
        auto y = plane(right, yStep), u = plane((right + 1) / 2, uStep), v = plane((right + 1) / 2, vStep);
        std::vector<uint8_t> actual(width * 4 + 2, 42), expected{actual};
        Babylon::VideoYuv420::Row(actual.data() + 1, y.data(), u.data(), v.data(), left, right, yStep, uStep, vStep,
            Babylon::VideoYuv420::ColorMatrix(hd, full));
        for (int x = left; x < right; ++x)
        {
            const int l = full ? 256 * y[x*yStep] : 298 * (y[x*yStep] - 16);
            const int cb = u[(x/2)*uStep] - 128, cr = v[(x/2)*vStep] - 128;
            const int rv = hd ? (full ? 403 : 459) : (full ? 359 : 409);
            const int gu = hd ? (full ? 48 : 55) : (full ? 88 : 100);
            const int gv = hd ? (full ? 120 : 136) : (full ? 183 : 208);
            const int bu = hd ? (full ? 475 : 541) : (full ? 454 : 516);
            auto at = 1 + (x-left)*4;
            for (int c : {l+rv*cr, l-gu*cb-gv*cr, l+bu*cb})
                expected[at++] = static_cast<uint8_t>(std::clamp((c + 128) >> 8, 0, 255));
            expected[at] = 255;
        }
        ASSERT_EQ(expected, actual) << "crop=" << left << "+" << width << " strides=" << yStep << "," << uStep << "," << vStep;
    }
}
