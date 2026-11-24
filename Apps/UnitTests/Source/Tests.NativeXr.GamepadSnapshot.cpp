#include "../../../Plugins/NativeXr/Source/GamepadSnapshot.h"
#include <gtest/gtest.h>
#include <stdexcept>

TEST(XrGamepadSnapshot, PublishesBothEmptyArraysExactlyOncePerSource)
{
    Babylon::Plugins::GamepadSnapshot left, right;
    xr::System::Session::Frame::GamePad empty{};
    int count{};
    auto publish = [&](bool buttons, bool axes, bool first) {
        EXPECT_TRUE(buttons && axes && first);
        ++count;
    };
    left.Update(empty, publish);
    left.Update(empty, publish);
    right.Update(empty, publish);
    EXPECT_EQ(count, 2);
}

TEST(XrGamepadSnapshot, ComparesLengthsOrderAndEachButtonFieldIndependently)
{
    Babylon::Plugins::GamepadSnapshot state;
    xr::System::Session::Frame::GamePad pad{};
    auto check = [&](int expected) {
        int mask{};
        state.Update(pad, [&](bool buttons, bool axes, bool first) { mask = int(buttons) | (int(axes) << 1) | (int(first) << 2); });
        EXPECT_EQ(mask, expected);
    };
    check(7);
    pad.Buttons.resize(2);
    check(1);
    pad.Buttons[0].Pressed = true;
    check(1);
    pad.Buttons[0].Touched = true;
    check(1);
    pad.Buttons[0].Value = 0.25f;
    check(1);
    std::swap(pad.Buttons[0], pad.Buttons[1]);
    check(1);
    pad.Axes = {0, 1};
    check(2);
    std::swap(pad.Axes[0], pad.Axes[1]);
    check(2);
    for (int i = 0; i < 1440; ++i) check(0);
    pad = {};
    check(3);
}

TEST(XrGamepadSnapshot, RetriesAfterPublisherFailure)
{
    Babylon::Plugins::GamepadSnapshot state;
    xr::System::Session::Frame::GamePad pad{};
    EXPECT_THROW(state.Update(pad, [](bool, bool, bool) { throw std::runtime_error{"publish"}; }), std::runtime_error);
    int count{};
    state.Update(pad, [&](bool buttons, bool axes, bool first) { ++count; EXPECT_TRUE(buttons && axes && first); });
    EXPECT_EQ(count, 1);
}
