#import <Metal/Metal.h>

#include <Babylon/Graphics/WgpuInterop.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>

TEST(NativeWebGPUMetalInterop, SharedQueueOrdersWebGpuWritesBeforeNativeReads)
{
    @autoreleasepool
    {
        BabylonWgpuConfig config{};
        config.width = 2;
        config.height = 2;
        std::unique_ptr<void, decltype(&babylon_wgpu_destroy)> backend{babylon_wgpu_create(&config), babylon_wgpu_destroy};
        ASSERT_NE(backend, nullptr);
        id<MTLDevice> device = (__bridge id<MTLDevice>)babylon_wgpu_native_get_metal_device();
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)babylon_wgpu_native_get_metal_queue();
        ASSERT_NE(device, nil);
        ASSERT_NE(queue, nil);
        EXPECT_EQ(queue.device, device);
        EXPECT_EQ((__bridge void*)queue, babylon_wgpu_native_get_metal_queue());

        auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:2 mipmapped:NO];
        descriptor.storageMode = MTLStorageModePrivate;
        id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
        ASSERT_NE(texture, nil);
        const auto textureId = babylon_wgpu_native_import_metal_texture((__bridge void*)texture,
            R"({"size":{"width":2,"height":2,"depthOrArrayLayers":1},"format":"rgba8unorm","usage":3})");
        ASSERT_NE(textureId, 0u);
        id<MTLBuffer> readback = [device newBufferWithLength:512 options:MTLResourceStorageModeShared];
        ASSERT_NE(readback, nil);
        const auto destination = std::string{"{\"texture\":{\"$nativeId\":"} + std::to_string(textureId) + "}}";

        for (uint8_t frame = 1; frame <= 16; ++frame)
        {
            const std::array<uint8_t, 16> pixels{frame, 20, 30, 255, 40, frame, 60, 255,
                70, 80, frame, 255, frame, frame, frame, 255};
            ASSERT_TRUE(babylon_wgpu_native_queue_write_texture(destination.c_str(), pixels.data(), pixels.size(),
                R"({"bytesPerRow":8,"rowsPerImage":2})", R"({"width":2,"height":2,"depthOrArrayLayers":1})"));
            const auto encoder = babylon_wgpu_native_create_command_encoder();
            const auto commands = babylon_wgpu_native_command_encoder_finish(encoder);
            ASSERT_NE(commands, 0u);
            ASSERT_TRUE(babylon_wgpu_native_queue_submit(&commands, 1));

            // No wgpu CPU wait: native commands follow the producer on the same queue.
            id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(2, 2, 1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:256 destinationBytesPerImage:512];
            [blit endEncoding];
            [commandBuffer commit];
            [commandBuffer waitUntilCompleted];
            ASSERT_EQ(commandBuffer.status, MTLCommandBufferStatusCompleted);
            const auto* actual = static_cast<const uint8_t*>(readback.contents);
            EXPECT_EQ(std::memcmp(actual, pixels.data(), 8), 0) << "frame " << static_cast<unsigned>(frame);
            EXPECT_EQ(std::memcmp(actual + 256, pixels.data() + 8, 8), 0) << "frame " << static_cast<unsigned>(frame);
        }
    }
}
