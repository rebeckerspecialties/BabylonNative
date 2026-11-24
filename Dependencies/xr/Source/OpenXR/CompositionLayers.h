#pragma once

#include "OpenXRHelpers.h"
#include <XR.h>
#include <openxr/openxr_platform.h>

#include <mutex>
#include <variant>

namespace xr
{
    // The renderer submits the writes on the Vulkan queue named at session creation.
    // Keep image acquisition independent of submission: a static image remains
    // present for many frames without reacquiring or uploading it.
    class CompositionLayers
    {
        struct SwapchainImages
        {
            XrSwapchain Handle{};
            uint32_t Index{};
            std::vector<XrSwapchainImageVulkanKHR> Images;
            bool Acquired{};
        };
        struct ImageChain : SwapchainImages
        {
            LayerShape Shape{};
            LayerLayout Layout{};
            uint32_t Width{}, Height{}, Eyes{};
            std::optional<SwapchainImages> RightCube;
            std::shared_ptr<MediaSurfaceProducer> Media;
            jobject Surface{};
            bool Attached{};
            bool Rendered{}, Retiring{};
        };
        using Header = std::variant<XrCompositionLayerQuad, XrCompositionLayerCylinderKHR,
            XrCompositionLayerEquirect2KHR, XrCompositionLayerEquirectKHR, XrCompositionLayerCubeKHR>;

        std::mutex m_mutex;
        std::unordered_map<uint64_t, ImageChain> m_chains;
        uint64_t m_next{1};
        std::vector<LayerPlacement> m_order{LayerPlacement::Projection(false)};
        std::vector<Header> m_storage;
        std::vector<const XrCompositionLayerBaseHeader*> m_headers;

    public:
        bool (*WaitForSubmittedWork)(){};
        uint64_t CreateMedia(XrSession session, PFN_xrCreateSwapchainAndroidSurfaceKHR create, LayerShape shape,
            uint32_t width, uint32_t height, LayerLayout layout, std::shared_ptr<MediaSurfaceProducer> source)
        {
            std::scoped_lock lock{m_mutex};
            if (!create || !source || shape == LayerShape::Cube || layout == LayerLayout::Array)
                throw std::runtime_error{"Unsupported Android media surface configuration"};
            for (const auto& [_, c] : m_chains)
                if (!c.Retiring && c.Media == source) throw std::runtime_error{"Video already bound to a compositor surface"};
            ImageChain chain{};
            chain.Shape = shape; chain.Layout = layout; chain.Eyes = layout == LayerLayout::Mono ? 1 : 2;
            chain.Width = width; chain.Height = height; chain.Media = std::move(source);
            auto ci = MakeXr<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
            ci.width = width * (layout == LayerLayout::LeftRight ? 2 : 1);
            ci.height = height * (layout == LayerLayout::TopBottom ? 2 : 1);
            jobject surface{};
            CheckXr(create(session,&ci,&chain.Handle,&surface), "xrCreateSwapchainAndroidSurfaceKHR");
            auto* env = android::global::GetEnvForCurrentThread();
            chain.Surface = env->NewGlobalRef(surface);
            // The returned surface belongs to the runtime swapchain. In
            // particular Quest returns a global ref, not a caller-owned local.
            if (!chain.Surface) { xrDestroySwapchain(chain.Handle); throw std::runtime_error{"Cannot retain Android media surface"}; }
            const auto id = m_next++;
            m_chains.emplace(id, std::move(chain));
            return id;
        }
        void Visibility(bool visible)
        {
            std::scoped_lock lock{m_mutex};
            for (auto& [_, chain] : m_chains)
                if ((!visible || chain.Retiring) && chain.Attached)
                {
                    chain.Media->SetSurface(nullptr);
                    chain.Attached = false;
                }
        }
        uint64_t Create(XrSession session, LayerShape shape, int64_t format, uint32_t width, uint32_t height, LayerLayout layout)
        {
            std::scoped_lock lock{m_mutex};
            ImageChain chain{};
            chain.Shape = shape;
            chain.Width = width;
            chain.Height = height;
            chain.Layout = layout;
            chain.Eyes = layout == LayerLayout::Mono ? 1 : 2;
            auto ci = MakeXr<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
            ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            ci.width = width * (layout == LayerLayout::LeftRight ? 2 : 1);
            ci.height = height * (layout == LayerLayout::TopBottom ? 2 : 1);
            const bool stereoCube = shape == LayerShape::Cube && layout == LayerLayout::Array;
            ci.format = format; ci.arraySize = layout == LayerLayout::Array && !stereoCube ? 2 : 1;
            ci.faceCount = shape == LayerShape::Cube ? 6 : 1;
            ci.mipCount = ci.sampleCount = 1;
            auto createImages = [&](SwapchainImages& output) {
                CheckXr(xrCreateSwapchain(session, &ci, &output.Handle), "xrCreateSwapchain(composition layer)");
                uint32_t count{};
                CheckXr(xrEnumerateSwapchainImages(output.Handle, 0, &count, nullptr), "enumerate layer image count");
                output.Images.assign(count, MakeXr<XrSwapchainImageVulkanKHR>(XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR));
                CheckXr(xrEnumerateSwapchainImages(output.Handle, count, &count,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(output.Images.data())), "enumerate layer images");
            };
            try
            {
                createImages(chain);
                if (stereoCube) createImages(chain.RightCube.emplace());
                const auto id = m_next++;
                m_chains.emplace(id, std::move(chain));
                return id;
            }
            catch (...) { Destroy(chain); throw; }
        }

        LayerImage Acquire(uint64_t id, int64_t format)
        {
            std::scoped_lock lock{m_mutex};
            auto& chain = m_chains.at(id);
            if (chain.Retiring) throw std::runtime_error{"Layer is destroyed"};
            auto acquireImages = [](SwapchainImages& images)
            {
                if (!images.Acquired)
                {
                    auto acquire = MakeXr<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
                    CheckXr(xrAcquireSwapchainImage(images.Handle, &acquire, &images.Index), "acquire layer image");
                    auto wait = MakeXr<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
                    wait.timeout = XR_INFINITE_DURATION;
                    CheckXr(xrWaitSwapchainImage(images.Handle, &wait), "wait for layer image");
                    images.Acquired = true;
                }
                return reinterpret_cast<uint64_t>(images.Images.at(images.Index).image);
            };
            if (!chain.Acquired) chain.Rendered = false;
            return {acquireImages(chain), format, chain.RightCube ? acquireImages(*chain.RightCube) : 0};
        }

        void MarkRendered(uint64_t id)
        {
            std::scoped_lock lock{m_mutex};
            m_chains.at(id).Rendered = true;
        }
        void Retire(uint64_t id)
        {
            std::scoped_lock lock{m_mutex};
            if (auto it = m_chains.find(id); it != m_chains.end()) it->second.Retiring = true;
        }
        void SetOrder(std::vector<LayerPlacement> order)
        {
            std::scoped_lock lock{m_mutex};
            m_order = std::move(order);
        }
        void Clear()
        {
            std::scoped_lock lock{m_mutex};
            if (!m_chains.empty() && WaitForSubmittedWork) WaitForSubmittedWork();
            for (auto& [_, chain] : m_chains) Destroy(chain);
            m_chains.clear();
        }
        bool Finish(XrSession session, XrSpace space, XrSpace viewerSpace, XrFrameEndInfo& end,
            XrCompositionLayerProjection* projection, bool render, bool equirect2, uint32_t maxLayers)
        {
            std::scoped_lock lock{m_mutex};
            for (auto& [_, chain] : m_chains)
            {
                if (!chain.Acquired) continue;
                auto release = MakeXr<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
                CheckXr(xrReleaseSwapchainImage(chain.Handle, &release), "release layer image");
                chain.Acquired = false;
                if (chain.RightCube && chain.RightCube->Acquired)
                {
                    CheckXr(xrReleaseSwapchainImage(chain.RightCube->Handle, &release), "release right cube image");
                    chain.RightCube->Acquired = false;
                }
            }
            m_headers.clear();
            m_storage.clear();
            m_storage.reserve(m_order.size() * 2);
            bool submittedProjection{};
            if (render) for (const auto& p : m_order)
            {
                if (!p.Id)
                {
                    if (projection)
                    {
                        projection->layerFlags = p.BlendAlpha ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
                        m_headers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(projection));
                        submittedProjection = true;
                    }
                    continue;
                }
                const auto it = m_chains.find(p.Id);
                if (it == m_chains.end() || it->second.Retiring) continue;
                auto& c = it->second;
                if (c.Media && !c.Attached)
                {
                    c.Media->SetSurface(c.Surface);
                    c.Attached = true;
                }
                if (!c.Media && !c.Rendered) continue;
                const auto& q = p.Transform.Orientation;
                const auto& v = p.Transform.Position;
                const XrPosef pose{{q.X,q.Y,q.Z,q.W},{v.X,v.Y,v.Z}};
                for (uint32_t eye = 0; eye < c.Eyes; ++eye)
                {
                    XrSwapchainSubImage sub{c.Handle, {
                        {static_cast<int32_t>(c.Layout == LayerLayout::LeftRight ? eye * c.Width : 0),
                         static_cast<int32_t>(c.Layout == LayerLayout::TopBottom ? eye * c.Height : 0)},
                        {static_cast<int32_t>(c.Width),static_cast<int32_t>(c.Height)}},
                        c.Layout == LayerLayout::Array ? eye : 0};
                    auto append = [&](auto h) {
                        h.space = p.ViewerRelative ? viewerSpace : space;
                        h.layerFlags = p.BlendAlpha ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
                        h.eyeVisibility = c.Eyes == 1 ? XR_EYE_VISIBILITY_BOTH : eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
                        m_storage.emplace_back(h);
                        m_headers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&std::get<decltype(h)>(m_storage.back())));
                    };
                    switch (c.Shape)
                    {
                    case LayerShape::Quad: {
                        auto h = MakeXr<XrCompositionLayerQuad>(XR_TYPE_COMPOSITION_LAYER_QUAD);
                        h.pose = pose; h.size = {p.Width,p.Height}; h.subImage = sub; append(h); break;
                    }
                    case LayerShape::Cylinder: {
                        auto h = MakeXr<XrCompositionLayerCylinderKHR>(XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR);
                        h.pose = pose; h.radius = p.Radius; h.centralAngle = p.Angle; h.aspectRatio = p.Aspect; h.subImage = sub; append(h); break;
                    }
                    case LayerShape::Equirect: {
                        if (equirect2) {
                            auto h = MakeXr<XrCompositionLayerEquirect2KHR>(XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR);
                            h.pose = pose; h.radius = p.Radius; h.centralHorizontalAngle = p.Angle;
                            h.upperVerticalAngle = p.Upper; h.lowerVerticalAngle = p.Lower; h.subImage = sub; append(h);
                        } else {
                            auto h = MakeXr<XrCompositionLayerEquirectKHR>(XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR);
                            h.pose = pose; h.radius = p.Radius; h.scale = {1,1}; h.subImage = sub; append(h);
                        }
                        break;
                    }
                    case LayerShape::Cube: {
                        auto h = MakeXr<XrCompositionLayerCubeKHR>(XR_TYPE_COMPOSITION_LAYER_CUBE_KHR);
                        h.orientation = pose.orientation;
                        h.swapchain = eye == 1 && c.RightCube ? c.RightCube->Handle : c.Handle;
                        h.imageArrayIndex = 0;
                        append(h); break;
                    }
                    default: break;
                    }
                }
            }
            if (m_headers.size() > maxLayers) throw std::runtime_error{"OpenXR compositor layer budget exceeded"};
            end.layerCount = static_cast<uint32_t>(m_headers.size());
            end.layers = m_headers.empty() ? nullptr : m_headers.data();
            CheckXr(xrEndFrame(session, &end), "xrEndFrame");
            bool drained = false;
            for (auto it = m_chains.begin(); it != m_chains.end();)
            {
                if (!it->second.Retiring) { ++it; continue; }
                if (!it->second.Media && !drained && WaitForSubmittedWork)
                {
                    if (!WaitForSubmittedWork()) break;
                    drained = true;
                }
                Destroy(it->second);
                it = m_chains.erase(it);
            }
            return submittedProjection;
        }
    private:
        static void Destroy(ImageChain& chain)
        {
            if (chain.Attached) chain.Media->SetSurface(nullptr);
            if (chain.Surface) android::global::GetEnvForCurrentThread()->DeleteGlobalRef(chain.Surface);
            if (chain.RightCube && chain.RightCube->Handle) xrDestroySwapchain(chain.RightCube->Handle);
            if (chain.Handle) xrDestroySwapchain(chain.Handle);
        }
    };
}
