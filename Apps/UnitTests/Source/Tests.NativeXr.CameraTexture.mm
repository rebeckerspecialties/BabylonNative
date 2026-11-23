#include "../../../Dependencies/xr/Source/ARKit/CameraTextureSnapshot.h"

#include <gtest/gtest.h>
#include <functional>

namespace
{
    class NativeXrCameraTexture : public testing::Test
    {
    protected:
        void SetUp() override
        {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) GTEST_SKIP() << "A Metal device is required for Core Video texture ownership tests.";
            ASSERT_EQ(CVMetalTextureCacheCreate(nullptr, nullptr, device, nullptr, &m_cache), kCVReturnSuccess);
            NSDictionary* attributes = @{
                (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES,
                (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{}
            };
            ASSERT_EQ(CVPixelBufferCreate(nullptr, 64, 64, kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
                (__bridge CFDictionaryRef)attributes, &m_image), kCVReturnSuccess);
            ASSERT_EQ(CVMetalTextureCacheCreateTextureFromImage(nullptr, m_cache, m_image, nullptr,
                MTLPixelFormatR8Unorm, 64, 64, 0, &m_luma), kCVReturnSuccess);
            ASSERT_EQ(CVMetalTextureCacheCreateTextureFromImage(nullptr, m_cache, m_image, nullptr,
                MTLPixelFormatRG8Unorm, 32, 32, 1, &m_chroma), kCVReturnSuccess);
        }

        void TearDown() override
        {
            if (m_luma) CFRelease(m_luma);
            if (m_chroma) CFRelease(m_chroma);
            if (m_image) CFRelease(m_image);
            if (m_cache) CFRelease(m_cache);
        }

        CVMetalTextureCacheRef m_cache{};
        CVPixelBufferRef m_image{};
        CVMetalTextureRef m_luma{};
        CVMetalTextureRef m_chroma{};
    };
}

TEST(NativeXrCameraTextureEmpty, MissingPlanesCanBeCopiedAndReleased)
{
    const xr::CameraTextureSnapshot empty;
    const auto copy{empty};
    EXPECT_EQ(copy.Luma(), nil);
    EXPECT_EQ(copy.Chroma(), nil);
}

TEST_F(NativeXrCameraTexture, RetainsEachPresentCoreVideoOwnerNotJustItsMetalTexture)
{
    // Relative retain counts test this helper's explicit CF ownership, not object liveness.
    for (const bool luma : {false, true})
    {
        const auto texture = luma ? m_luma : m_chroma;
        const auto before = CFGetRetainCount(texture);
        {
            const xr::CameraTextureSnapshot snapshot{luma ? texture : nullptr, luma ? nullptr : texture};
            EXPECT_EQ(CFGetRetainCount(texture), before + 1);
            EXPECT_EQ(luma ? snapshot.Luma() : snapshot.Chroma(), CVMetalTextureGetTexture(texture));
            EXPECT_EQ(luma ? snapshot.Chroma() : snapshot.Luma(), nil);
        }
        EXPECT_EQ(CFGetRetainCount(texture), before);
    }
}

TEST_F(NativeXrCameraTexture, CompletionOwnsBothPlanesAfterEncodingSnapshotIsGone)
{
    const auto lumaReferences = CFGetRetainCount(m_luma);
    const auto chromaReferences = CFGetRetainCount(m_chroma);
    std::function<void()> completion;
    {
        const xr::CameraTextureSnapshot snapshot{m_luma, m_chroma};
        completion = [snapshot] {
            EXPECT_EQ(snapshot.Luma().width, 64u);
            EXPECT_EQ(snapshot.Chroma().width, 32u);
        };
    }
    EXPECT_EQ(CFGetRetainCount(m_luma), lumaReferences + 1);
    EXPECT_EQ(CFGetRetainCount(m_chroma), chromaReferences + 1);
    completion();
    completion = {};
    EXPECT_EQ(CFGetRetainCount(m_luma), lumaReferences);
    EXPECT_EQ(CFGetRetainCount(m_chroma), chromaReferences);
}

TEST_F(NativeXrCameraTexture, MultipleInFlightConsumersReleaseIndependently)
{
    const auto lumaReferences = CFGetRetainCount(m_luma);
    const auto chromaReferences = CFGetRetainCount(m_chroma);
    std::function<void()> first;
    std::function<void()> second;
    {
        const xr::CameraTextureSnapshot snapshot{m_luma, m_chroma};
        first = [snapshot] { (void)snapshot; };
        second = [snapshot] { (void)snapshot; };
    }
    EXPECT_EQ(CFGetRetainCount(m_luma), lumaReferences + 2);
    EXPECT_EQ(CFGetRetainCount(m_chroma), chromaReferences + 2);
    first = {};
    EXPECT_EQ(CFGetRetainCount(m_luma), lumaReferences + 1);
    EXPECT_EQ(CFGetRetainCount(m_chroma), chromaReferences + 1);
    second = {};
    EXPECT_EQ(CFGetRetainCount(m_luma), lumaReferences);
    EXPECT_EQ(CFGetRetainCount(m_chroma), chromaReferences);
}

TEST_F(NativeXrCameraTexture, ObjectiveCCompletionBlockRetainsTheSnapshot)
{
    const auto lumaReferences = CFGetRetainCount(m_luma);
    const auto chromaReferences = CFGetRetainCount(m_chroma);
    void (^completion)();
    {
        const xr::CameraTextureSnapshot snapshot{m_luma, m_chroma};
        completion = ^{
            EXPECT_EQ(snapshot.Luma().width, 64u);
            EXPECT_EQ(snapshot.Chroma().width, 32u);
        };
    }
    EXPECT_EQ(CFGetRetainCount(m_luma), lumaReferences + 1);
    EXPECT_EQ(CFGetRetainCount(m_chroma), chromaReferences + 1);
    completion();
    completion = nil;
    EXPECT_EQ(CFGetRetainCount(m_luma), lumaReferences);
    EXPECT_EQ(CFGetRetainCount(m_chroma), chromaReferences);
}
