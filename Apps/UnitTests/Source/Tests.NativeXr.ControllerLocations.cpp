#include <XR.h>
#include <gtest/gtest.h>

TEST(XrControllerLocations, PartialTrackingNeverRemovesAnActiveInputOrExposesAnOldPose)
{
    using Source = xr::System::Session::Frame::InputSource;
    using State = Source::PoseState;
    Source source;
    xr::Space unrelated{};
    for (auto grip : {State::Located, State::Unlocated, State::Inactive})
    {
        for (auto aim : {State::Located, State::Unlocated, State::Inactive})
        {
            source.SetControllerPoseStates(grip, aim);
            EXPECT_EQ(source.TrackedThisFrame, grip != State::Inactive || aim != State::Inactive);
            EXPECT_EQ(source.GamepadTrackedThisFrame, source.TrackedThisFrame);
            EXPECT_EQ(source.HasUnavailableControllerPose(&source.GripSpace), grip != State::Located);
            EXPECT_EQ(source.HasUnavailableControllerPose(&source.AimSpace), aim != State::Located);
            EXPECT_FALSE(source.HasUnavailableControllerPose(&unrelated));
        }
    }
}
