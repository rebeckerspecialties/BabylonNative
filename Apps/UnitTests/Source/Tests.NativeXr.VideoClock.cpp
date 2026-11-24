#include "../../../Plugins/NativeXr/Source/VideoPlaybackClock.h"
#include <gtest/gtest.h>

TEST(NativeXrVideoClock, PausesResumesAndLoopsAtDuration)
{
    using namespace std::chrono_literals;
    const std::chrono::steady_clock::time_point start{};
    Babylon::VideoPlaybackClock clock;
    clock.Reset(1000);
    clock.Play(true, start);
    clock.Play(true, start + 100ms);
    EXPECT_EQ(clock.Position(start + 270ms), 270);
    clock.Pause(start + 300ms);
    EXPECT_EQ(clock.Position(start + 2s), 300);
    clock.Play(true, start + 3s);
    EXPECT_EQ(clock.Position(start + 3800ms), 100);
    clock.SetPosition(900, start + 4s);
    EXPECT_EQ(clock.Position(start + 4100ms), 0);
}

TEST(NativeXrVideoClock, EndsWithoutWrappingAndResetsForNewSource)
{
    using namespace std::chrono_literals;
    const std::chrono::steady_clock::time_point start{};
    Babylon::VideoPlaybackClock clock;
    clock.Reset(1200);
    clock.Play(false, start);
    EXPECT_EQ(clock.Position(start + 2s), 1200);
    clock.Play(false, start + 3s);
    EXPECT_EQ(clock.Position(start + 3100ms), 100);
    clock.Reset(0);
    EXPECT_EQ(clock.Position(start + 3s), 0);
}
