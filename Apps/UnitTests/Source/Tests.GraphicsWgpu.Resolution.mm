#include <Babylon/Graphics/Device.h>
#include <gtest/gtest.h>
#import <QuartzCore/CAMetalLayer.h>

TEST(GraphicsWgpuResolution, DefaultPresentationUsesNativePixelDensity)
{
    for (const CGFloat scale : {1.0, 2.0, 3.0, 3.312})
    {
        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.contentsScale = scale;
        Babylon::Graphics::Configuration config{};
        config.Window = (__bridge CA::MetalLayer*)layer;
        config.Width = 375;
        config.Height = 812;
        Babylon::Graphics::Device device{config};
        EXPECT_FLOAT_EQ(device.GetDevicePixelRatio(), static_cast<float>(scale));
        EXPECT_FLOAT_EQ(device.GetHardwareScalingLevel(), 1.0f / static_cast<float>(scale));
        device.UpdateSize(812, 375);
        EXPECT_FLOAT_EQ(device.GetHardwareScalingLevel(), 1.0f / static_cast<float>(scale));
    }
}

TEST(GraphicsWgpuResolution, ExplicitScalingDoesNotApplyPixelDensityTwice)
{
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.contentsScale = 3;
    Babylon::Graphics::Configuration config{};
    config.Window = (__bridge CA::MetalLayer*)layer;
    config.Width = 375;
    config.Height = 812;
    Babylon::Graphics::Device device{config};
    for (const float level : {1.0f, 0.5f, 2.0f, 1.0f / 3.0f})
    {
        device.SetHardwareScalingLevel(level);
        EXPECT_FLOAT_EQ(device.GetHardwareScalingLevel(), level);
        device.UpdateSize(812, 375);
        EXPECT_FLOAT_EQ(device.GetHardwareScalingLevel(), level);
    }
}

TEST(GraphicsWgpuResolution, HeadlessPresentationDefaultsToUnitScaling)
{
    Babylon::Graphics::Configuration config{};
    config.Width = 640;
    config.Height = 480;
    Babylon::Graphics::Device device{config};
    EXPECT_FLOAT_EQ(device.GetDevicePixelRatio(), 1.0f);
    EXPECT_FLOAT_EQ(device.GetHardwareScalingLevel(), 1.0f);
}
