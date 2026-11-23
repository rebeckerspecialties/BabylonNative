#include "../../../Embedding/Source/AnimationFrameGate.h"
#include <gtest/gtest.h>
#include <thread>

using Babylon::Embedding::AnimationFrameGate;

TEST(AnimationFrameGate, EmptyAndSingleFrame)
{
    AnimationFrameGate gate;
    EXPECT_FALSE(gate.Begin());
    EXPECT_FALSE(gate.Complete());
    ASSERT_TRUE(gate.Request());
    ASSERT_TRUE(gate.Begin());
    EXPECT_FALSE(gate.Complete());
    EXPECT_TRUE(gate.Request());
}

TEST(AnimationFrameGate, QueuedTicksCoalesceWithoutExtraFrame)
{
    AnimationFrameGate gate;
    ASSERT_TRUE(gate.Request());
    for (int i = 0; i < 100; ++i) EXPECT_FALSE(gate.Request());
    ASSERT_TRUE(gate.Begin());
    EXPECT_FALSE(gate.Begin());
    EXPECT_FALSE(gate.Complete());
}

TEST(AnimationFrameGate, BusyTicksProduceExactlyOneContinuation)
{
    AnimationFrameGate gate;
    ASSERT_TRUE(gate.Request());
    ASSERT_TRUE(gate.Begin());
    for (int i = 0; i < 100; ++i) EXPECT_FALSE(gate.Request());
    EXPECT_TRUE(gate.Complete());
    EXPECT_FALSE(gate.Request());
    ASSERT_TRUE(gate.Begin());
    EXPECT_FALSE(gate.Complete());
    EXPECT_FALSE(gate.Begin());
}

TEST(AnimationFrameGate, ControlPolicyDropsBusyTicks)
{
    AnimationFrameGate gate;
    ASSERT_TRUE(gate.Request(false));
    ASSERT_TRUE(gate.Begin());
    for (int i = 0; i < 100; ++i) EXPECT_FALSE(gate.Request(false));
    EXPECT_FALSE(gate.Complete());
}

TEST(AnimationFrameGate, StopCancelsIdleQueuedRunningAndPendingLifetimes)
{
    for (int phase = 0; phase < 4; ++phase)
    {
        AnimationFrameGate gate;
        if (phase >= 1) ASSERT_TRUE(gate.Request());
        if (phase >= 2) ASSERT_TRUE(gate.Begin());
        if (phase >= 3) EXPECT_FALSE(gate.Request());
        gate.Stop();
        EXPECT_FALSE(gate.Request());
        EXPECT_FALSE(gate.Begin());
        EXPECT_FALSE(gate.Complete());
        gate.Abort();
        EXPECT_FALSE(gate.Request());
        AnimationFrameGate resumed;
        EXPECT_TRUE(resumed.Request());
        EXPECT_FALSE(gate.Request());
    }
}

TEST(AnimationFrameGate, ExceptionsAndFailedDispatchAllowNextDisplayTick)
{
    for (int phase = 0; phase < 3; ++phase)
    {
        AnimationFrameGate gate;
        ASSERT_TRUE(gate.Request());
        if (phase >= 1) ASSERT_TRUE(gate.Begin());
        if (phase >= 2) EXPECT_FALSE(gate.Request());
        gate.Abort();
        EXPECT_FALSE(gate.Begin());
        ASSERT_TRUE(gate.Request());
        ASSERT_TRUE(gate.Begin());
        EXPECT_FALSE(gate.Complete());
    }
}

TEST(AnimationFrameGate, ConcurrentCompletionAndTickNeverLoseOrDuplicateDispatch)
{
    for (int i = 0; i < 1000; ++i)
    {
        AnimationFrameGate gate;
        ASSERT_TRUE(gate.Request());
        ASSERT_TRUE(gate.Begin());
        bool hostDispatch = false;
        bool continuation = false;
        std::thread tick{[&] { hostDispatch = gate.Request(); }};
        continuation = gate.Complete();
        tick.join();
        EXPECT_NE(hostDispatch, continuation);
        ASSERT_TRUE(gate.Begin());
        EXPECT_FALSE(gate.Complete());
    }
}

TEST(AnimationFrameGate, RepeatedBusyFramesRemainBoundedByDisplayRequests)
{
    AnimationFrameGate gate;
    ASSERT_TRUE(gate.Request());
    for (int frame = 0; frame < 100; ++frame)
    {
        ASSERT_TRUE(gate.Begin());
        EXPECT_FALSE(gate.Request());
        ASSERT_TRUE(gate.Complete());
        EXPECT_FALSE(gate.Request());
    }
    ASSERT_TRUE(gate.Begin());
    EXPECT_FALSE(gate.Complete());
    EXPECT_FALSE(gate.Begin());
}

TEST(AnimationFrameGate, StopWinsConcurrentCompletionWithoutRevivingCancelledWork)
{
    for (int i = 0; i < 1000; ++i)
    {
        AnimationFrameGate gate;
        ASSERT_TRUE(gate.Request());
        ASSERT_TRUE(gate.Begin());
        EXPECT_FALSE(gate.Request());
        std::thread stop{[&] { gate.Stop(); }};
        gate.Complete();
        stop.join();
        EXPECT_FALSE(gate.Begin());
        gate.Abort();
        EXPECT_FALSE(gate.Request());
    }
}
