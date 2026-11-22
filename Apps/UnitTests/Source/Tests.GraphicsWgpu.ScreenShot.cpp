#include "../../../Core/GraphicsWgpu/Source/WgpuNative.h"

#include <Babylon/Graphics/WgpuInterop.h>
#include <gsl/util>
#include <gtest/gtest.h>

TEST(GraphicsWgpuScreenShot, MissingCaptureHasNoPixelsOrDimensions)
{
    Babylon::Graphics::WgpuBootstrapConfig config{};
    config.Width = 7;
    config.Height = 3;
    Babylon::Graphics::WgpuNative backend{config};
    ASSERT_TRUE(backend.IsValid()) << backend.GetLastError();

    const auto frame = backend.CopyScreenShot();
    EXPECT_TRUE(frame.Pixels.empty());
    EXPECT_EQ(frame.Width, 0u);
    EXPECT_EQ(frame.Height, 0u);
}

TEST(GraphicsWgpuScreenShot, CapturedDimensionsSurviveHostResize)
{
    Babylon::Graphics::WgpuBootstrapConfig config{};
    config.Width = 7;
    config.Height = 3;
    Babylon::Graphics::WgpuNative backend{config};
    ASSERT_TRUE(backend.IsValid()) << backend.GetLastError();

    constexpr uint64_t canvasId = 1;
    constexpr uint32_t usage = 0x01 | 0x10; // COPY_SRC | RENDER_ATTACHMENT.
    const auto texture = babylon_wgpu_native_canvas_get_current_texture(canvasId, 2, 2, "rgba8unorm", usage);
    const auto destroyCanvas = gsl::finally([] { babylon_wgpu_native_canvas_destroy(canvasId); });
    ASSERT_NE(texture, 0u);

    ASSERT_TRUE(backend.RequestScreenShot());
    ASSERT_TRUE(backend.Render());
    ASSERT_TRUE(backend.Resize(13, 5));

    // Neither the 2x2 source nor the new host size describes the captured pixels.
    const auto frame = backend.CopyScreenShot();
    EXPECT_EQ(frame.Width, 7u);
    EXPECT_EQ(frame.Height, 3u);
    EXPECT_EQ(frame.Pixels.size(), 7u * 3u * 4u);

    ASSERT_TRUE(backend.RequestScreenShot());
    ASSERT_TRUE(backend.Render());
    const auto resizedFrame = backend.CopyScreenShot();
    EXPECT_EQ(resizedFrame.Width, 13u);
    EXPECT_EQ(resizedFrame.Height, 5u);
    EXPECT_EQ(resizedFrame.Pixels.size(), 13u * 5u * 4u);
}
