#pragma once

#include <vulkan/vulkan.h>
#include <Babylon/Graphics/RendererMode.h>

namespace Babylon::Graphics
{
    struct DeviceT
    {
        VkInstance Instance{VK_NULL_HANDLE};
        VkPhysicalDevice PhysicalDevice{VK_NULL_HANDLE};
        VkDevice Device{VK_NULL_HANDLE};
        VkQueue Queue{VK_NULL_HANDLE};
        uint32_t QueueFamilyIndex{0};
        uint32_t QueueIndex{0};
    };

    using TextureT = VkImage;
    using TextureFormatT = VkFormat;

    struct DeviceConfiguration
    {
        DeviceT Device{};
        float DevicePixelRatio{1.f};
    };

    struct PlatformInfo
    {
        DeviceT Device{};
    };
}
