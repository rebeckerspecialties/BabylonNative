#pragma once

#include "OpenXRHelpers.h"
#include <openxr/openxr_platform.h>

#include <vector>

namespace xr
{
    // Motion and depth swapchains rotate independently, including when the two
    // images for a given frame happen to have the same index.
    class WarpSwapchain final
    {
    public:
        WarpSwapchain(XrSession session, VkFormat format, XrSwapchainUsageFlags attachment,
            uint32_t width, uint32_t height, uint32_t views)
        {
            auto info = MakeXr<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
            info.format = format;
            info.usageFlags = attachment | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            info.sampleCount = info.faceCount = info.mipCount = 1;
            info.width = width;
            info.height = height;
            info.arraySize = views;
            CheckXr(xrCreateSwapchain(session, &info, &m_handle), "create space-warp swapchain");
            try
            {
                uint32_t count{};
                CheckXr(xrEnumerateSwapchainImages(m_handle, 0, &count, nullptr), "count space-warp images");
                if (!count) throw std::runtime_error{"OpenXR supplied no space-warp images"};
                m_images.assign(count, MakeXr<XrSwapchainImageVulkanKHR>(XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR));
                CheckXr(xrEnumerateSwapchainImages(m_handle, count, &count,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(m_images.data())), "enumerate space-warp images");
            }
            catch (...)
            {
                xrDestroySwapchain(m_handle);
                throw;
            }
        }

        WarpSwapchain(const WarpSwapchain&) = delete;
        WarpSwapchain& operator=(const WarpSwapchain&) = delete;
        ~WarpSwapchain() { xrDestroySwapchain(m_handle); }

        VkImage Acquire()
        {
            if (!m_acquired)
            {
                auto info = MakeXr<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
                CheckXr(xrAcquireSwapchainImage(m_handle, &info, &m_index), "acquire space-warp image");
                m_acquired = true;
            }
            if (!m_waited)
            {
                auto wait = MakeXr<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
                wait.timeout = XR_INFINITE_DURATION;
                XrResult result;
                do { result = xrWaitSwapchainImage(m_handle, &wait); } while (result == XR_TIMEOUT_EXPIRED);
                CheckXr(result, "wait for space-warp image");
                m_waited = true;
            }
            return m_images.at(m_index).image;
        }

        void Release()
        {
            if (!m_acquired) return;
            if (!m_waited) Acquire();
            auto info = MakeXr<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
            CheckXr(xrReleaseSwapchainImage(m_handle, &info), "release space-warp image");
            m_acquired = m_waited = false;
        }

        XrSwapchain Handle() const { return m_handle; }

    private:
        XrSwapchain m_handle{};
        std::vector<XrSwapchainImageVulkanKHR> m_images;
        uint32_t m_index{};
        bool m_acquired{}, m_waited{};
    };

    class SpaceWarpSwapchains final
    {
    public:
        SpaceWarpSwapchains(XrSession session, VkFormat depthFormat, uint32_t width, uint32_t height, uint32_t views)
            : Motion{session, VK_FORMAT_R16G16B16A16_SFLOAT, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT, width, height, views}
            , Depth{session, depthFormat, XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, width, height, views}
            , m_extent{static_cast<int32_t>(width), static_cast<int32_t>(height)}
        {}

        XrCompositionLayerSpaceWarpInfoFB View(uint32_t eye, const XrPosef& delta, float nearZ, float farZ) const
        {
            auto result = MakeXr<XrCompositionLayerSpaceWarpInfoFB>(XR_TYPE_COMPOSITION_LAYER_SPACE_WARP_INFO_FB);
            result.appSpaceDeltaPose = delta;
            result.motionVectorSubImage = {Motion.Handle(), {{0, 0}, m_extent}, eye};
            result.depthSubImage = {Depth.Handle(), {{0, 0}, m_extent}, eye};
            result.minDepth = 0.f;
            result.maxDepth = 1.f;
            result.nearZ = nearZ;
            result.farZ = farZ;
            return result;
        }

        WarpSwapchain Motion;
        WarpSwapchain Depth;

    private:
        XrExtent2Di m_extent;
    };
}
