#include "AndroidWindow.h"
#include "WgpuNative.h"
#include <media/NdkImageReader.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <cstdio>
#include <future>

// Runs on Android without an XR session. Queue the same config by value as
// DeviceImpl, then detach and replace the producer before allowing Vulkan to run.
int main()
{
    using namespace Babylon::Graphics;
    const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.enabledExtensionCount = 2;
    create.ppEnabledExtensionNames = extensions;
    VkInstance instance{};
    if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS || RetainWindow(nullptr)) return 1;
    int disconnectedRejected{};
    for (int i = 0; i < 64; ++i)
    {
        const bool disconnect = i % 2 != 0;
        AImageReader* reader{};
        if (AImageReader_new(64, 64, AIMAGE_FORMAT_RGBA_8888, 2, &reader) != AMEDIA_OK) return 2;
        ANativeWindow* window{};
        if (AImageReader_getWindow(reader, &window) != AMEDIA_OK || !window) return 3;
        WgpuBootstrapConfig config{};
        config.SurfaceLayer = RetainWindow(window);
        std::weak_ptr<ANativeWindow> owner = config.SurfaceLayer;
        std::promise<void> detached;
        auto run = std::async(std::launch::async, [instance, config, ready = detached.get_future()] {
            ready.wait();
            VkAndroidSurfaceCreateInfoKHR surfaceCreate{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
            surfaceCreate.window = config.SurfaceLayer.get();
            VkSurfaceKHR surface{};
            const auto result = vkCreateAndroidSurfaceKHR(instance, &surfaceCreate, nullptr, &surface);
            if (result == VK_SUCCESS) vkDestroySurfaceKHR(instance, surface, nullptr);
            return result;
        });
        if (disconnect) AImageReader_delete(reader);
        config.SurfaceLayer.reset();
        detached.set_value();
        const auto result = run.get();
        if (!disconnect) AImageReader_delete(reader);
        // A live ANativeWindow reference is not a guarantee that its consumer
        // is still connected. The system can reject the detached surface normally.
        const bool unavailable = disconnect && (result == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR || result == VK_ERROR_SURFACE_LOST_KHR);
        if ((result != VK_SUCCESS && !unavailable) || !owner.expired())
        {
            std::fprintf(stderr, "iteration %d: vkCreateAndroidSurfaceKHR=%d, released=%d\n", i, static_cast<int>(result), owner.expired());
            return 4;
        }
        disconnectedRejected += unavailable;
    }
    vkDestroyInstance(instance, nullptr);
    std::printf("PASS: 32 live surfaces created; 32 detached: %d created, %d unavailable; all references released\n",
        32 - disconnectedRejected, disconnectedRejected);
}
