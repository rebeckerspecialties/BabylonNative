#include <Babylon/Graphics/RendererType.h>
#include "DeviceImpl.h"

#include <bgfx/platform.h>

namespace Babylon::Graphics
{
    const bgfx::RendererType::Enum DeviceImpl::s_bgfxRenderType = bgfx::RendererType::Vulkan;

    PlatformInfo DeviceImpl::GetPlatformInfo() const
    {
        PlatformInfo info{};
#if BGFX_CONFIG_RENDERER_VULKAN
        const auto& init = m_state.Bgfx.InitState;
        if (init.externalVulkanInstance != nullptr && init.externalVulkanDevice != nullptr)
        {
            info.Device.Instance = reinterpret_cast<VkInstance>(init.externalVulkanInstance);
            info.Device.PhysicalDevice = reinterpret_cast<VkPhysicalDevice>(init.externalVulkanPhysicalDevice);
            info.Device.Device = reinterpret_cast<VkDevice>(init.externalVulkanDevice);
            info.Device.Queue = reinterpret_cast<VkQueue>(init.externalVulkanQueue);
            info.Device.QueueFamilyIndex = init.externalVulkanQueueFamily;
            info.Device.QueueIndex = init.externalVulkanQueueIndex;
        }
#endif
        return info;
    }
}
