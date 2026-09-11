#include <gtest/gtest.h>
#include <wgpu.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

using namespace std::chrono_literals;

namespace
{
    template<auto Release, typename Handle>
    auto Own(Handle handle)
    {
        return std::unique_ptr<std::remove_pointer_t<Handle>, decltype(Release)>{handle, Release};
    }

    std::string Message(WGPUStringView view)
    {
        return view.data ? std::string{view.data, view.length == WGPU_STRLEN ? std::strlen(view.data) : view.length} : std::string{};
    }

    template<typename Handle>
    struct RequestResult
    {
        Handle handle{};
        std::string error;
    };
}

TEST(NativeWebGPUCAPI, AdapterInfoAndGpuBufferRoundTrip)
{
    auto instance = Own<wgpuInstanceRelease>(wgpuCreateInstance(nullptr));
    ASSERT_NE(instance, nullptr);

    // Callback ownership lasts until completion, including if a timeout ends
    // the test while a spontaneous callback is still pending.
    auto* adapterPromise = new std::promise<RequestResult<WGPUAdapter>>{};
    auto adapterFuture = adapterPromise->get_future();
    WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.userdata1 = adapterPromise;
    adapterCallback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* data, void*) {
        std::unique_ptr<std::promise<RequestResult<WGPUAdapter>>> completion{static_cast<std::promise<RequestResult<WGPUAdapter>>*>(data)};
        completion->set_value({status == WGPURequestAdapterStatus_Success ? adapter : nullptr, Message(message)});
    };
    wgpuInstanceRequestAdapter(instance.get(), nullptr, adapterCallback);
    ASSERT_EQ(adapterFuture.wait_for(5s), std::future_status::ready);
    const auto adapterResult = adapterFuture.get();
    auto adapter = Own<wgpuAdapterRelease>(adapterResult.handle);
    ASSERT_NE(adapter, nullptr) << adapterResult.error;

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    ASSERT_EQ(wgpuAdapterGetInfo(adapter.get(), &info), WGPUStatus_Success);
    const auto adapterName = Message(info.device);
    std::cout << "wgpu-native C API adapter: " << adapterName << std::endl;
    EXPECT_NE(info.adapterType, WGPUAdapterType_CPU);
    wgpuAdapterInfoFreeMembers(info);

    auto* devicePromise = new std::promise<RequestResult<WGPUDevice>>{};
    auto deviceFuture = devicePromise->get_future();
    WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallback.userdata1 = devicePromise;
    deviceCallback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* data, void*) {
        std::unique_ptr<std::promise<RequestResult<WGPUDevice>>> completion{static_cast<std::promise<RequestResult<WGPUDevice>>*>(data)};
        completion->set_value({status == WGPURequestDeviceStatus_Success ? device : nullptr, Message(message)});
    };
    wgpuAdapterRequestDevice(adapter.get(), nullptr, deviceCallback);
    ASSERT_EQ(deviceFuture.wait_for(5s), std::future_status::ready);
    const auto deviceResult = deviceFuture.get();
    auto device = Own<wgpuDeviceRelease>(deviceResult.handle);
    ASSERT_NE(device, nullptr) << deviceResult.error;
    info = WGPU_ADAPTER_INFO_INIT;
    ASSERT_EQ(wgpuDeviceGetAdapterInfo(device.get(), &info), WGPUStatus_Success);
    EXPECT_EQ(Message(info.device), adapterName);
    wgpuAdapterInfoFreeMembers(info);

    auto queue = Own<wgpuQueueRelease>(wgpuDeviceGetQueue(device.get()));
    ASSERT_NE(queue, nullptr);
    constexpr std::array<uint32_t, 4> expected{0x12345678, 0x90abcdef, 0, 0xffffffff};
    WGPUBufferDescriptor bufferDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDescriptor.size = sizeof(expected);
    bufferDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    auto source = Own<wgpuBufferRelease>(wgpuDeviceCreateBuffer(device.get(), &bufferDescriptor));
    bufferDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    auto readback = Own<wgpuBufferRelease>(wgpuDeviceCreateBuffer(device.get(), &bufferDescriptor));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(readback, nullptr);
    EXPECT_EQ(wgpuBufferGetMapState(readback.get()), WGPUBufferMapState_Unmapped);
    wgpuQueueWriteBuffer(queue.get(), source.get(), 0, expected.data(), sizeof(expected));
    auto encoder = Own<wgpuCommandEncoderRelease>(wgpuDeviceCreateCommandEncoder(device.get(), nullptr));
    ASSERT_NE(encoder, nullptr);
    wgpuCommandEncoderCopyBufferToBuffer(encoder.get(), source.get(), 0, readback.get(), 0, sizeof(expected));
    auto commands = Own<wgpuCommandBufferRelease>(wgpuCommandEncoderFinish(encoder.get(), nullptr));
    ASSERT_NE(commands, nullptr);
    const WGPUCommandBuffer command = commands.get();
    wgpuQueueSubmit(queue.get(), 1, &command);

    auto* mapPromise = new std::promise<WGPUMapAsyncStatus>{};
    auto mapFuture = mapPromise->get_future();
    WGPUBufferMapCallbackInfo mapCallback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mapCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    mapCallback.userdata1 = mapPromise;
    mapCallback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* data, void*) {
        std::unique_ptr<std::promise<WGPUMapAsyncStatus>> completion{static_cast<std::promise<WGPUMapAsyncStatus>*>(data)};
        completion->set_value(status);
    };
    wgpuBufferMapAsync(readback.get(), WGPUMapMode_Read, 0, sizeof(expected), mapCallback);
    EXPECT_NE(wgpuDevicePoll(device.get(), true, nullptr, 5'000'000'000ULL), WGPUNativePollStatus_Timeout);
    ASSERT_EQ(mapFuture.wait_for(5s), std::future_status::ready);
    ASSERT_EQ(mapFuture.get(), WGPUMapAsyncStatus_Success);
    EXPECT_EQ(wgpuBufferGetMapState(readback.get()), WGPUBufferMapState_Mapped);
    const auto* mapped = wgpuBufferGetConstMappedRange(readback.get(), 0, sizeof(expected));
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(std::memcmp(mapped, expected.data(), sizeof(expected)), 0);
    wgpuBufferUnmap(readback.get());
    EXPECT_EQ(wgpuBufferGetMapState(readback.get()), WGPUBufferMapState_Unmapped);
}
