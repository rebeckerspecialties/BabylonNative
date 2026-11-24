#include <Babylon/Plugins/NativeXr.h>

#include <Babylon/JsRuntimeScheduler.h>

#include <XR.h>

#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/WgpuInterop.h>
#include <Babylon/Plugins/NativeWebGPU.h>
#include <napi/napi.h>
#include <arcana/threading/task.h>
#include <arcana/tracing/trace_region.h>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <sstream>
#include "NativeXrImpl.h"

namespace Babylon
{
    namespace
    {
        constexpr uint32_t WEBGPU_TEXTURE_USAGE_TEXTURE_BINDING{0x04};
        constexpr uint32_t WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT{0x10};

        const char* XrTextureFormatToWebGpuFormat(xr::TextureFormat format)
        {
            switch (format)
            {
            case xr::TextureFormat::BGRA8_SRGB:
                return "bgra8unorm";
            case xr::TextureFormat::RGBA8_SRGB:
                return "rgba8unorm";
            case xr::TextureFormat::D32FS8:
                return "depth32float-stencil8";
            case xr::TextureFormat::D24S8:
                return "depth24plus-stencil8";
            case xr::TextureFormat::D16:
                return "depth16unorm";

            default:
                throw std::runtime_error{ "Unsupported texture format" };
            }
        }

        const char* XrDepthTextureFormatToWebGpuFormat(xr::TextureFormat format)
        {
            switch (format)
            {
            case xr::TextureFormat::D32FS8:
            case xr::TextureFormat::D24S8:
                // WebGPU requires an optional feature for depth32float-stencil8.
                // NativeXR owns this wgpu depth target, so prefer the portable
                // browser-shaped depth/stencil format instead of mirroring ARKit.
                return "depth24plus-stencil8";

            default:
                return XrTextureFormatToWebGpuFormat(format);
            }
        }

        std::string MakeTextureDescriptorJson(
            const char* label,
            uint32_t width,
            uint32_t height,
            uint32_t depthOrArrayLayers,
            const char* format,
            uint32_t usage,
            bool transient = false)
        {
            std::ostringstream descriptor{};
            descriptor
                << "{\"label\":\"" << label
                << "\",\"size\":{\"width\":" << width
                << ",\"height\":" << height
                << ",\"depthOrArrayLayers\":" << std::max<uint32_t>(1, depthOrArrayLayers)
                << "},\"mipLevelCount\":1,\"sampleCount\":1,\"dimension\":\"2d\",\"format\":\""
                << format << "\",\"usage\":" << usage
                << ",\"transient\":" << (transient ? "true" : "false") << "}";
            return descriptor.str();
        }

        std::string GetLastWgpuError()
        {
            std::array<char, 2048> buffer{};
            if (babylon_wgpu_get_last_error(buffer.data(), buffer.size()) && buffer[0] != '\0')
            {
                return buffer.data();
            }

            return {};
        }

        // Opt-in frame-path trace (BABYLON_NATIVE_XR_TRACE=1). The XR frame loop
        // spans JS, the graphics update scheduler, and the platform session, so
        // when frames stop the useful question is which of those stages was last
        // reached. Writes to stderr, which host-driven runs capture.
        void XrTrace(const char* format, ...)
        {
            static const bool enabled = std::getenv("BABYLON_NATIVE_XR_TRACE") != nullptr;
            if (!enabled)
            {
                return;
            }

            std::fputs("XRTRACE ", stderr);
            va_list args;
            va_start(args, format);
            std::vfprintf(stderr, format, args);
            va_end(args);
            std::fputc('\n', stderr);
            std::fflush(stderr);
        }

        bool ShouldUseAsyncXrComposite()
        {
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
            // The Vulkan binding names the renderer's queue. The release-layout
            // transition must be submitted before xrReleaseSwapchainImage, but
            // the runtime accepts commands on that queue still in flight.
            return true;
#elif defined(__APPLE__)
            static const bool enabled = std::getenv("BABYLON_NATIVE_XR_ASYNC_COMPOSITE") != nullptr;
            static bool didLog{};
            if (enabled && !didLog)
            {
                didLog = true;
                std::fprintf(stderr, "NativeXR async composite enabled: skipping per-frame WebGPU queue wait before AR compositing.\n");
            }
            return enabled;
#else
            return false;
#endif
        }

    }

    namespace Plugins
    {
        NativeXr::Impl::Impl(Napi::Env env)
            : m_env{env}
            , m_runtimeScheduler{Babylon::JsRuntime::GetFromJavaScript(env)}
        {
        }

        void NativeXr::Impl::UpdateWindow(void* windowPtr)
        {
            m_windowPtr = windowPtr;
        }

        void NativeXr::Impl::SetSessionStateChangedCallback(std::function<void(bool)> callback)
        {
            {
                std::lock_guard<std::mutex> lock{m_sessionStateChangedCallbackMutex};
                m_sessionStateChangedCallback = std::move(callback);
            }
            NotifySessionStateChanged(m_sessionState != nullptr);
        }

        void NativeXr::Impl::NotifySessionStateChanged(bool isSessionActive)
        {
            std::unique_lock<std::mutex> lock{m_sessionStateChangedCallbackMutex};
            auto sessionStateChangedCallback{m_sessionStateChangedCallback};
            lock.unlock();

            if (sessionStateChangedCallback)
            {
                sessionStateChangedCallback(isSessionActive);
            }
        }

        void NativeXr::Impl::SetRenderTextureFunctions(const Napi::Function& createFunction, const Napi::Function& destroyFunction)
        {
            if (m_sessionState == nullptr)
            {
                throw std::runtime_error{"NativeXR render-target callbacks require an active XR session."};
            }
            m_sessionState->CreateRenderTexture = Napi::Persistent(createFunction);
            m_sessionState->DestroyRenderTexture = Napi::Persistent(destroyFunction);
        }

        Napi::Value NativeXr::Impl::GetRenderTargetForViewIndex(uint32_t viewIndex) const
        {
            if (m_sessionState == nullptr || m_sessionState->ActiveViewConfigurations.size() <= viewIndex)
            {
                return m_env.Null();
            }

            auto* viewConfig = m_sessionState->ActiveViewConfigurations[viewIndex];
            if (viewConfig == nullptr || !viewConfig->Initialized)
            {
                return m_env.Null();
            }

            const auto startViewIterator = m_sessionState->ViewConfigurationStartViewIdx.find(viewConfig);
            if (startViewIterator == m_sessionState->ViewConfigurationStartViewIdx.end())
            {
                return m_env.Null();
            }
            const auto renderTargetIndex = viewIndex - startViewIterator->second;
            if (viewConfig->RenderTargets.size() <= renderTargetIndex || viewConfig->RenderTargets[renderTargetIndex].JsRenderTarget.IsEmpty())
            {
                return m_env.Null();
            }
            return viewConfig->RenderTargets[renderTargetIndex].JsRenderTarget.Value();
        }

        std::optional<XRGPUSubImageData> NativeXr::Impl::GetWebGPUSubImage(uint32_t viewIndex) const
        {
            if (m_sessionState == nullptr || m_sessionState->ActiveViewConfigurations.size() <= viewIndex)
            {
                return std::nullopt;
            }

            auto* viewConfig = m_sessionState->ActiveViewConfigurations[viewIndex];
            if (viewConfig == nullptr || !viewConfig->Initialized)
            {
                return std::nullopt;
            }

            const auto startViewIterator = m_sessionState->ViewConfigurationStartViewIdx.find(viewConfig);
            if (startViewIterator == m_sessionState->ViewConfigurationStartViewIdx.end())
            {
                return std::nullopt;
            }
            const auto renderTargetIndex = viewIndex - startViewIterator->second;
            if (viewConfig->RenderTargets.size() <= renderTargetIndex)
            {
                return std::nullopt;
            }

            const auto& renderTarget = viewConfig->RenderTargets[renderTargetIndex];
            if (renderTarget.JsColorTexture.IsEmpty())
            {
                return std::nullopt;
            }

            return XRGPUSubImageData{
                renderTarget.JsColorTexture.Value(),
                renderTarget.JsDepthTexture.IsEmpty() ? m_env.Null() : Napi::Value{renderTarget.JsDepthTexture.Value()},
                static_cast<uint32_t>(viewConfig->ViewTextureSize.Width),
                static_cast<uint32_t>(viewConfig->ViewTextureSize.Height),
                renderTargetIndex};
        }

        const char* NativeXr::Impl::GetWebGPUColorFormat() const
        {
            if (!m_sessionState || !m_sessionState->Session)
            {
                throw std::runtime_error{"No active XR session for a WebGPU binding."};
            }
            return XrTextureFormatToWebGpuFormat(m_sessionState->Session->GetColorTextureFormat());
        }

        uint32_t NativeXr::Impl::GetWebGPUTextureWidth() const
        {
            return m_sessionState == nullptr ? 0 : m_sessionState->LastTextureWidth;
        }

        uint32_t NativeXr::Impl::GetWebGPUTextureHeight() const
        {
            return m_sessionState == nullptr ? 0 : m_sessionState->LastTextureHeight;
        }

        uint32_t NativeXr::Impl::GetWebGPUTextureArrayLength() const
        {
            return m_sessionState == nullptr ? 0 : m_sessionState->LastTextureArrayLength;
        }

        arcana::task<bool, std::exception_ptr> NativeXr::Impl::IsSessionSupportedAsync(xr::SessionType mode)
        {
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
            if (mode == xr::SessionType::IMMERSIVE_AR)
            {
                auto& context = Graphics::DeviceContext::GetFromJavaScript(m_env);
                return arcana::make_task(context.AfterRenderScheduler(), arcana::cancellation::none(), [self = shared_from_this()] {
                    return self->m_system.TryInitialize() && self->m_system.SupportsImmersiveAr();
                });
            }
#endif
            return xr::System::IsSessionSupportedAsync(mode);
        }

        arcana::task<void, std::exception_ptr> NativeXr::Impl::BeginSessionAsync(bool immersiveAr)
        {
            if (m_beginTask)
            {
                return arcana::task_from_error<void>(std::make_exception_ptr(std::runtime_error{"There is already an immersive XR session either currently active or in the process of being set up. There can only be one immersive XR session at a time."}));
            }

            Graphics::DeviceContext& context = Graphics::DeviceContext::GetFromJavaScript(m_env);

            // Don't try to start a session while it is still ending.
            m_beginTask.emplace(m_endTask.then(context.AfterRenderScheduler(), arcana::cancellation::none(),
                [this, thisRef{shared_from_this()}, &context, immersiveAr]() {
                    assert(m_sessionState == nullptr);

                    m_sessionState = std::make_unique<SessionState>(context);
                    m_sessionEnding = false;

                    if (!m_system.IsInitialized() &&
                        !m_system.TryInitialize())
                    {
                        throw std::runtime_error{"Failed to initialize xr system."};
                    }

#if defined(__APPLE__)
                    auto* graphicsDevice = babylon_wgpu_native_get_metal_device();
                    auto* graphicsQueue = babylon_wgpu_native_get_metal_queue();
                    if (graphicsDevice == nullptr || graphicsQueue == nullptr)
                    {
                        throw std::runtime_error{"NativeXR requires an initialized Metal-backed NativeWebGPU device and queue on Apple platforms."};
                    }
                    auto graphicsBindingLifetime = std::shared_ptr<void>{};
#elif defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                    if (immersiveAr && !m_system.SupportsImmersiveAr())
                        throw std::system_error{std::make_error_code(std::errc::not_supported), "OpenXR immersive AR is unavailable"};
                    BabylonWgpuVulkanBinding nativeVulkanBinding{};
                    if (!babylon_wgpu_native_get_vulkan_binding(&nativeVulkanBinding))
                    {
                        auto detail = GetLastWgpuError();
                        throw std::runtime_error{detail.empty()
                                ? "NativeXR requires an initialized Vulkan-backed NativeWebGPU device on Android XR."
                                : "NativeXR failed to get the NativeWebGPU Vulkan binding: " + detail};
                    }
                    auto vulkanBinding = std::make_shared<xr::VulkanGraphicsBinding>(xr::VulkanGraphicsBinding{
                        static_cast<uintptr_t>(nativeVulkanBinding.instance),
                        static_cast<uintptr_t>(nativeVulkanBinding.physical_device),
                        static_cast<uintptr_t>(nativeVulkanBinding.device),
                        static_cast<uintptr_t>(nativeVulkanBinding.queue),
                        nativeVulkanBinding.api_version,
                        nativeVulkanBinding.queue_family_index,
                        nativeVulkanBinding.queue_index,
                        nativeVulkanBinding.max_texture_dimension_2d,
                        babylon_wgpu_native_queue_wait_submitted_work,
                        nativeVulkanBinding.fragment_density_map != 0});
                    void* graphicsDevice = vulkanBinding.get();
                    void* graphicsQueue = nullptr;
                    std::shared_ptr<void> graphicsBindingLifetime = vulkanBinding;
#else
#error NativeXR requires an explicit platform graphics binding.
#endif

                    return xr::System::Session::CreateAsync(m_system, graphicsDevice, graphicsQueue, [this, thisRef{shared_from_this()}] { return m_windowPtr; })
                        .then(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [this, thisRef{shared_from_this()}, graphicsBindingLifetime{std::move(graphicsBindingLifetime)}, immersiveAr](std::shared_ptr<xr::System::Session> session) {
                            static_cast<void>(graphicsBindingLifetime);
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                            if (immersiveAr) session->EnableImmersiveAr();
#else
                            static_cast<void>(immersiveAr);
#endif
                            m_sessionState->Session = std::move(session);
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                            babylon_wgpu_suspend_window_presentation();
                            m_sessionState->ResumeWindowPresentation.emplace(&babylon_wgpu_resume_window_presentation);
#endif
                            NotifySessionStateChanged(true);
                        });
                }));

            return m_beginTask->then(m_runtimeScheduler, arcana::cancellation::none(),
                [self = shared_from_this()](const arcana::expected<void, std::exception_ptr>& result) {
                    if (result.has_error())
                    {
                        self->m_sessionState.reset();
                        self->m_beginTask.reset();
                        std::rethrow_exception(result.error());
                    }
                });
        }

        arcana::task<void, std::exception_ptr> NativeXr::Impl::EndSessionAsync()
        {
            if (m_sessionEnding || !m_beginTask)
            {
                return m_endTask;
            }

            assert(m_beginTask);
            assert(m_sessionState != nullptr);

            // Publish the shared task before cancellation can synchronously request another end.
            arcana::task_completion_source<void, std::exception_ptr> endCompletion{};
            m_endTask = endCompletion.as_task();
            m_sessionEnding = true;

            m_sessionState->CancellationSource.cancel();

            if (!m_sessionState->DestroyRenderTexture.IsEmpty())
            {
                for (auto& entry : m_sessionState->TextureToViewConfigurationMap)
                {
                    auto& viewConfig = entry.second;
                    for (auto& renderTarget : viewConfig.RenderTargets)
                    {
                        if (!renderTarget.JsRenderTarget.IsEmpty())
                        {
                            m_sessionState->DestroyRenderTexture.Call({renderTarget.JsRenderTarget.Value()});
                        }
                    }
                }
            }

            m_sessionState->ActiveViewConfigurations.clear();
            m_sessionState->ViewConfigurationStartViewIdx.clear();
            m_sessionState->TextureToViewConfigurationMap.clear();
            m_sessionState->ScheduleFrameCallbacks.ClearPending();
            m_sessionState->CreateRenderTexture.Reset();
            m_sessionState->DestroyRenderTexture.Reset();

            // Don't try to end the session while it is still starting.
            m_beginTask->then(arcana::inline_scheduler, arcana::cancellation::none(), [this, thisRef{shared_from_this()}] {
                                       // Also don't try to end the session while a frame is in progress.
                                       return m_sessionState->FrameTask;
                                   })
                            .then(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [this, thisRef{shared_from_this()}](const arcana::expected<void, std::exception_ptr>&) {
                                assert(m_sessionState != nullptr);
                                assert(m_sessionState->Session != nullptr);
                                assert(m_sessionState->Frame == nullptr);

                                m_sessionState->Session->RequestEndSession();

                                bool shouldEndSession{};
                                bool shouldRestartSession{};
                                do
                                {
                                    // Block and burn frames until XR successfully shuts down.
                                    m_sessionState->Frame = m_sessionState->Session->GetNextFrame(shouldEndSession, shouldRestartSession);
                                    m_sessionState->Frame->Render();
                                    m_sessionState->Frame.reset();
                                }
                                while (!shouldEndSession);

                                m_sessionState.reset();
                                m_beginTask.reset();
                                NotifySessionStateChanged(false);
                            })
                            .then(arcana::inline_scheduler, arcana::cancellation::none(), [endCompletion](const arcana::expected<void, std::exception_ptr>& result) mutable {
                                endCompletion.complete(result);
                            });

            return m_endTask;
        }

        void NativeXr::Impl::ScheduleFrame(std::function<void(const std::shared_ptr<const xr::System::Session::Frame>&)>&& callback)
        {
            assert(m_sessionState != nullptr);

            // Queue callbacks even while a frame is in progress. WebXR render
            // loops request the next frame from inside the current frame
            // callback; starting it immediately races BeginFrame/EndFrame.
            m_sessionState->ScheduleFrameCallbacks.Enqueue(std::move(callback));

            if (m_sessionState->FrameScheduled)
            {
                return;
            }

            SchedulePendingFrame();
        }

        void NativeXr::Impl::SchedulePendingFrame()
        {
            assert(m_sessionState != nullptr);

            XrTrace("SchedulePendingFrame scheduled=%d callbacks=%zu",
                m_sessionState->FrameScheduled ? 1 : 0,
                m_sessionState->ScheduleFrameCallbacks.PendingCount());

            if (m_sessionState->FrameScheduled || m_sessionState->ScheduleFrameCallbacks.PendingCount() == 0)
            {
                return;
            }

            m_sessionState->FrameScheduled = true;

            // Drive XR frames from the graphics frame-start scheduler on every
            // platform. The frame-completion scope acquired below keeps the
            // frame open while JavaScript handles the callback, which is what
            // the legacy per-update SafeTimespanGuarantor token used to do; the
            // WebGPU device never opens those per-update timespans, so a frame
            // task posted to one is never run.
            XrTrace("SchedulePendingFrame posting FrameTask to frame-start scheduler");
            m_sessionState->FrameTask = arcana::make_task(m_sessionState->GraphicsContext.FrameStartScheduler(), m_sessionState->CancellationSource, [this, thisRef{shared_from_this()}] {
                XrTrace("FrameTask running on frame-start scheduler");
                BeginFrame();

#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                // OpenXR produces frames while the session is synchronized but
                // not yet visible. Those frames have no acquired swapchain
                // images and must be submitted without invoking the WebXR
                // render callback. Keep the callback queued for the first
                // renderable frame. ARKit has no such pre-visible phase.
                if (!m_sessionState->Frame->IsTracking)
                {
                    EndFrame();
                    m_sessionState->FrameScheduled = false;
                    SchedulePendingFrame();
                    return arcana::task_from_result<std::exception_ptr>();
                }
#endif

                arcana::make_task(m_runtimeScheduler, m_sessionState->CancellationSource, [this, frameScope{std::make_shared<Graphics::FrameCompletionScope>(m_sessionState->GraphicsContext.AcquireFrameCompletionScope())}, thisRef{shared_from_this()}]() {
                    BeginUpdate();

                    {
                        arcana::trace_region scheduleRegion{"NativeXR::ScheduleFrame invoke JS callbacks"};
                        m_sessionState->ScheduleFrameCallbacks.Dispatch(m_sessionState->Frame);
                    }

                    EndUpdate();
                }).then(arcana::inline_scheduler, m_sessionState->CancellationSource, [this, thisRef{shared_from_this()}](const arcana::expected<void, std::exception_ptr>& result) {
                      if (!m_sessionState->CancellationSource.cancelled() && result.has_error())
                      {
                          Napi::Error::New(m_env, result.error()).ThrowAsJavaScriptException();
                      }
                  });

                // Queue EndFrame on this frame's AfterRender tick now, rather than as a continuation of the JS task.
                // The frame completion scope acquired above keeps that tick from running until the JS task is done,
                // but arcana releases the scope before running the task's continuations. A continuation could
                // therefore be queued after the render thread drained AfterRender, and EndFrame would then consume the
                // next frame and leave that frame's EndFrame without an active frame.
                return arcana::make_task(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [this, thisRef{shared_from_this()}] {
                    EndFrame();
                    m_sessionState->FrameScheduled = false;
                    SchedulePendingFrame();
                });
            });
        }

        void NativeXr::Impl::BeginFrame()
        {
            assert(m_sessionState != nullptr);
            assert(m_sessionState->Session != nullptr);

            arcana::trace_region beginFrameRegion{"NativeXR::BeginFrame"};

            bool shouldEndSession{};
            bool shouldRestartSession{};
            XrTrace("BeginFrame calling GetNextFrame");
            m_sessionState->Frame = m_sessionState->Session->GetNextFrame(shouldEndSession, shouldRestartSession, [this, thisRef{shared_from_this()}](void* texturePointer) {
                return arcana::make_task(m_runtimeScheduler, arcana::cancellation::none(), [this, texturePointer, thisRef] {
                    const auto itViewConfig{m_sessionState->TextureToViewConfigurationMap.find(texturePointer)};
                    if (itViewConfig != m_sessionState->TextureToViewConfigurationMap.end())
                    {
                        auto& viewConfig = itViewConfig->second;
                        if (!m_sessionState->DestroyRenderTexture.IsEmpty())
                        {
                            for (auto& renderTarget : viewConfig.RenderTargets)
                            {
                                if (!renderTarget.JsRenderTarget.IsEmpty())
                                {
                                    m_sessionState->DestroyRenderTexture.Call({renderTarget.JsRenderTarget.Value()});
                                }
                            }
                        }

                        m_sessionState->TextureToViewConfigurationMap.erase(texturePointer);
                    }
                }).then(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [] {}); // Ensure continuations run on the render thread if they use inline_scheduler.
            });

            // Ending a session outside of calls to EndSessionAsync() is currently not supported.
            assert(!shouldEndSession);
            assert(m_sessionState->Frame != nullptr);
        }

        void NativeXr::Impl::BeginUpdate()
        {
            arcana::trace_region beginUpdateRegion{"NativeXR::BeginUpdate"};

            m_sessionState->ActiveViewConfigurations.resize(m_sessionState->Frame->Views.size());
            m_sessionState->FrameColorTextureIds.clear();
            m_sessionState->LastTextureWidth = 0;
            m_sessionState->LastTextureHeight = 0;
            m_sessionState->LastTextureArrayLength = 0;
            for (uint32_t viewIdx = 0; viewIdx < m_sessionState->Frame->Views.size(); viewIdx++)
            {
                const auto& view = m_sessionState->Frame->Views[viewIdx];
                m_sessionState->LastTextureWidth = static_cast<uint32_t>(view.ColorTextureSize.Width);
                m_sessionState->LastTextureHeight = static_cast<uint32_t>(view.ColorTextureSize.Height);
                m_sessionState->LastTextureArrayLength = std::max<uint32_t>(1, static_cast<uint32_t>(view.ColorTextureSize.Depth));
                const auto& it{m_sessionState->TextureToViewConfigurationMap.find(view.ColorTexturePointer)};
                const auto colorUsage = WEBGPU_TEXTURE_USAGE_TEXTURE_BINDING | WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT | m_sessionState->ProjectionColorUsage;
                const auto* depthFormat = m_sessionState->ProjectionDepthFormat
                    ? m_sessionState->ProjectionDepthFormat->c_str()
                    : XrDepthTextureFormatToWebGpuFormat(view.DepthTextureFormat);
                const bool useDepth = *depthFormat != '\0';

                if (it == m_sessionState->TextureToViewConfigurationMap.end() ||
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                    it->second.DensityImage != view.DensityMapImage ||
#endif
                    it->second.ViewTextureSize.Width != view.ColorTextureSize.Width ||
                    it->second.ViewTextureSize.Height != view.ColorTextureSize.Height ||
                    it->second.ViewTextureSize.Depth != view.ColorTextureSize.Depth ||
                    it->second.DepthFormat != depthFormat || it->second.ColorUsage != colorUsage)
                {
                    if (it != m_sessionState->TextureToViewConfigurationMap.end() && !m_sessionState->DestroyRenderTexture.IsEmpty())
                    {
                        for (auto& target : it->second.RenderTargets)
                            if (!target.JsRenderTarget.IsEmpty())
                                m_sessionState->DestroyRenderTexture.Call({target.JsRenderTarget.Value()});
                    }
                    auto& viewConfig = m_sessionState->TextureToViewConfigurationMap[view.ColorTexturePointer] = {};
                    m_sessionState->ActiveViewConfigurations[viewIdx] = &viewConfig;
                    m_sessionState->ViewConfigurationStartViewIdx[&viewConfig] = viewIdx;

                    viewConfig.ColorTexturePointer = view.ColorTexturePointer;
                    viewConfig.DepthTexturePointer = view.DepthTexturePointer;
                    viewConfig.ViewTextureSize = view.ColorTextureSize;
                    viewConfig.DepthFormat = depthFormat;
                    viewConfig.ColorUsage = colorUsage;

                    assert(view.ColorTextureSize.Width != 0);
                    assert(view.ColorTextureSize.Height != 0);
                    assert(view.ColorTextureSize.Width == view.DepthTextureSize.Width);
                    assert(view.ColorTextureSize.Height == view.DepthTextureSize.Height);
                    assert(view.ColorTextureSize.Depth == view.DepthTextureSize.Depth);

                    const auto textureWidth = static_cast<uint32_t>(view.ColorTextureSize.Width);
                    const auto textureHeight = static_cast<uint32_t>(view.ColorTextureSize.Height);
                    const auto textureLayers = std::max<uint32_t>(1, static_cast<uint32_t>(view.ColorTextureSize.Depth));
                    const auto* colorFormat = XrTextureFormatToWebGpuFormat(view.ColorTextureFormat);
                    const auto depthUsage = WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
                    const auto colorDescriptor = MakeTextureDescriptorJson("NativeXR.color", textureWidth, textureHeight, textureLayers, colorFormat, colorUsage);
                    // The XR depth buffer is written and read only within the
                    // render pass -- never sampled, copied, or presented -- so in
                    // principle it can stay in tile memory (MTLStorageModeMemoryless)
                    // and never occupy DRAM.
                    //
                    // It is nonetheless requested as ordinary storage, because
                    // Babylon.js creates a texture *view* of this attachment
                    // (WebGPUEngine.setUsage -> GPUTexture.createView) and Metal
                    // refuses to create a view of a memoryless texture on some
                    // GPU families. Measured with the AR portal on this branch:
                    // iPhone 12 (A14) renders 1964 XR frames with the depth
                    // attachment memoryless, while iPhone XS (A12) dies on the
                    // second frame -- newTextureViewWithPixelFormat returns nil,
                    // which wgpu-hal's Metal backend unwraps into a panic
                    // (wgpu-hal/src/metal/device.rs, create_texture_view).
                    //
                    // A runtime probe cannot recover from that today: the failure
                    // aborts the process instead of surfacing an error. Once
                    // wgpu-hal returns an error there, this can become a
                    // capability check that keeps the bandwidth win on the GPUs
                    // that support it.
#if defined(__APPLE__)
                    const auto colorTextureId = babylon_wgpu_native_import_metal_texture(view.ColorTexturePointer, colorDescriptor.c_str());
#elif defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                    const auto colorTextureId = babylon_wgpu_native_import_vulkan_texture(
                        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(view.ColorTexturePointer)),
                        view.SwapchainWidth, view.SwapchainHeight, view.SwapchainFormat,
                        colorDescriptor.c_str(), 0);
                    viewConfig.DensityImage = view.DensityMapImage;
                    if (viewConfig.DensityImage && !babylon_wgpu_native_set_eye_density_map(colorTextureId, viewConfig.DensityImage))
                        throw std::runtime_error{"Could not attach the acquired projection density map: " + GetLastWgpuError()};
#else
#error NativeXR requires an explicit platform texture import path.
#endif
                    const auto depthTextureId = useDepth ? babylon_wgpu_native_create_texture(
                        MakeTextureDescriptorJson("NativeXR.depth", textureWidth, textureHeight, textureLayers, depthFormat, depthUsage, /* transient */ false).c_str()) : 0;

                    if (colorTextureId == 0 || (useDepth && depthTextureId == 0))
                    {
                        std::ostringstream error{};
                        error << "NativeXR failed to create WebGPU render targets for XR frame"
                            << " (colorImportId=" << colorTextureId
                            << ", depthCreateId=" << depthTextureId << ")";
                        const auto detail = GetLastWgpuError();
                        if (!detail.empty())
                        {
                            error << ": " << detail;
                        }
                        throw std::runtime_error{error.str()};
                    }

                    const auto eyeCount = std::max<uint32_t>(1, static_cast<uint32_t>(viewConfig.ViewTextureSize.Depth));
                    viewConfig.RenderTargets.resize(eyeCount);
                    auto jsColorTexture = Plugins::NativeWebGPU::CreateTextureFromNativeId(
                        m_env, colorTextureId, "NativeXR.color", colorFormat, textureWidth, textureHeight, textureLayers, colorUsage);
                    Napi::Value jsDepthTexture = m_env.Null();
                    if (useDepth)
                    {
                        jsDepthTexture = Plugins::NativeWebGPU::CreateTextureFromNativeId(
                            m_env, depthTextureId, "NativeXR.depth", depthFormat, textureWidth, textureHeight, textureLayers, depthUsage);
                    }
                    for (auto& renderTarget : viewConfig.RenderTargets)
                    {
                        renderTarget.ColorTextureId = colorTextureId;
                        renderTarget.DepthTextureId = depthTextureId;
                        renderTarget.JsColorTexture = Napi::Persistent(jsColorTexture);
                        if (useDepth)
                        {
                            renderTarget.JsDepthTexture = Napi::Persistent(jsDepthTexture.As<Napi::Object>());
                        }
                        if (!m_sessionState->CreateRenderTexture.IsEmpty())
                        {
                            auto jsWidth{Napi::Value::From(m_env, textureWidth)};
                            auto jsHeight{Napi::Value::From(m_env, textureHeight)};
                            auto jsRenderTarget = m_sessionState->CreateRenderTexture.Call({jsWidth, jsHeight, m_env.Null(), jsColorTexture, jsDepthTexture}).As<Napi::Object>();
                            jsRenderTarget.Set("skipInitialClear", Napi::Boolean::New(m_env, !view.RequiresAppClear));
                            renderTarget.JsRenderTarget = Napi::Persistent(jsRenderTarget);
                        }
                    }
                    viewConfig.Initialized = true;
                }
                else
                {
                    auto& viewConfig = it->second;
                    m_sessionState->ActiveViewConfigurations[viewIdx] = &viewConfig;
                    m_sessionState->ViewConfigurationStartViewIdx.try_emplace(&viewConfig, viewIdx);
                }
            }

#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
            for (uint32_t viewIdx = 0; viewIdx < m_sessionState->ActiveViewConfigurations.size(); ++viewIdx)
            {
                auto* viewConfig = m_sessionState->ActiveViewConfigurations[viewIdx];
                const auto startViewIterator = m_sessionState->ViewConfigurationStartViewIdx.find(viewConfig);
                if (viewConfig == nullptr ||
                    startViewIterator == m_sessionState->ViewConfigurationStartViewIdx.end())
                {
                    continue;
                }
                const auto renderTargetIndex = viewIdx - startViewIterator->second;
                if (renderTargetIndex < viewConfig->RenderTargets.size())
                {
                    // Views of one array are acquired and released as a single swapchain image.
                    if (renderTargetIndex == 0)
                    {
                        m_sessionState->FrameColorTextureIds.emplace_back(viewConfig->RenderTargets.front().ColorTextureId);
                    }
                }
            }
#endif
        }

#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
        void NativeXr::Impl::SetWarpDelta(const xr::Pose& delta)
        {
            m_sessionState->WarpDelta = delta;
        }

        void NativeXr::Impl::AcquireWarpAttachments(XRGPUSubImageData& subImage, const std::string& depthFormat)
        {
            auto& target = m_sessionState->Warp;
            if (target.JsDepthTexture.IsEmpty())
            {
                const auto images = m_sessionState->Session->AcquireWarpImages(babylon_wgpu_native_openxr_vk_format(depthFormat.c_str()));
                const auto bind = [&](xr::LayerImage image, const char* label, const char* format, uint64_t& id, Napi::ObjectReference& js) {
                    const auto usage = WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
                    const auto descriptor = MakeTextureDescriptorJson(label, images.Width, images.Height, images.Views, format, usage);
                    id = babylon_wgpu_native_import_openxr_data_texture(image.Image, images.Width, images.Height, image.Format, descriptor.c_str());
                    if (!id) throw std::runtime_error{"Could not import OpenXR space-warp attachment: " + GetLastWgpuError()};
                    js = Napi::Persistent(NativeWebGPU::CreateTextureFromNativeId(m_env, id, label, format, images.Width, images.Height, images.Views, usage));
                };
                if (target.JsColorTexture.IsEmpty())
                    bind(images.Motion, "XRProjectionLayer.motion", "rgba16float", target.ColorTextureId, target.JsColorTexture);
                bind(images.Depth, "XRProjectionLayer.warpDepth", depthFormat.c_str(), target.DepthTextureId, target.JsDepthTexture);
                m_sessionState->WarpViews = images.Views;
                if (!babylon_wgpu_native_watch_warp_images(target.ColorTextureId, target.DepthTextureId))
                    throw std::runtime_error{"Could not track OpenXR space-warp submission"};
            }
            subImage.MotionVectorTexture = target.JsColorTexture.Value();
            subImage.DepthStencilTexture = target.JsDepthTexture.Value();
        }

        XRGPUSubImageData NativeXr::Impl::AcquireLayerTexture(uint64_t id, uint32_t width, uint32_t height, uint32_t slices,
            const std::string& format, uint32_t usage, const std::string& depthFormat)
        {
            auto& target = m_sessionState->AcquiredLayers[id];
            if (target.JsColorTexture.IsEmpty())
            {
                const auto image = m_sessionState->Session->AcquireLayer(id);
                // A retained JS view must never reference an image that the runtime has reacquired.
                // COPY_DST selects the app-owned render texture + ordered Vulkan resolve path.
                const auto descriptor = MakeTextureDescriptorJson("XRCompositionLayer", width, height, slices, format.c_str(), usage | 2);
                target.ColorTextureId = babylon_wgpu_native_import_vulkan_texture(image.Image, width, height, image.Format, descriptor.c_str(), image.RightCubeImage);
                if (!target.ColorTextureId) throw std::runtime_error{"Could not import OpenXR compositor image: " + GetLastWgpuError()};
                target.JsColorTexture = Napi::Persistent(NativeWebGPU::CreateTextureFromNativeId(
                    m_env, target.ColorTextureId, "XRCompositionLayer", format.c_str(), width, height, slices, usage));
                if (!depthFormat.empty())
                {
                    const auto depth = MakeTextureDescriptorJson("XRCompositionLayer.depth", width, height, slices, depthFormat.c_str(), WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT);
                    target.DepthTextureId = babylon_wgpu_native_create_texture(depth.c_str());
                    if (!target.DepthTextureId) throw std::runtime_error{"Could not create layer depth: " + GetLastWgpuError()};
                    target.JsDepthTexture = Napi::Persistent(NativeWebGPU::CreateTextureFromNativeId(
                        m_env, target.DepthTextureId, "XRCompositionLayer.depth", depthFormat.c_str(), width, height, slices, WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT));
                }
            }
            return {target.JsColorTexture.Value(), target.JsDepthTexture.IsEmpty() ? m_env.Null() : Napi::Value{target.JsDepthTexture.Value()}, width, height};
        }
#endif

        void NativeXr::Impl::EndUpdate()
        {
            arcana::trace_region endUpdateRegion{"NativeXR::EndUpdate"};
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
            for (auto& [id, target] : m_sessionState->AcquiredLayers)
            {
                if (target.JsColorTexture.IsEmpty()) continue;
                const auto color = target.JsColorTexture.Value();
                if (!color.Get("__destroyed").ToBoolean().Value())
                {
                    if (!babylon_wgpu_native_prepare_openxr_textures_for_release(&target.ColorTextureId, 1))
                        throw std::runtime_error{"Could not resolve OpenXR layer: " + GetLastWgpuError()};
                    m_sessionState->Session->MarkLayerRendered(id);
                    color.Get("destroy").As<Napi::Function>().Call(color, {});
                }
                if (!target.JsDepthTexture.IsEmpty())
                {
                    const auto depth = target.JsDepthTexture.Value();
                    depth.Get("destroy").As<Napi::Function>().Call(depth, {});
                }
            }
            m_sessionState->AcquiredLayers.clear();
            auto& warp = m_sessionState->Warp;
            if (!warp.JsColorTexture.IsEmpty() || !warp.JsDepthTexture.IsEmpty())
            {
                const bool submitted = babylon_wgpu_native_finish_warp_images(m_sessionState->WarpViews);
                std::vector<uint64_t> live;
                for (auto [id, value] : {std::pair{warp.ColorTextureId, &warp.JsColorTexture}, std::pair{warp.DepthTextureId, &warp.JsDepthTexture}})
                    if (!value->IsEmpty() && !value->Value().Get("__destroyed").ToBoolean().Value()) live.push_back(id);
                if (!live.empty() && !babylon_wgpu_native_prepare_openxr_textures_for_release(live.data(), live.size()))
                    throw std::runtime_error{"Could not release OpenXR space-warp attachments: " + GetLastWgpuError()};
                if (live.size() == 2 && submitted)
                    m_sessionState->Session->MarkWarpImagesSubmitted(m_sessionState->WarpDelta);
                for (auto* ref : {&warp.JsColorTexture, &warp.JsDepthTexture})
                    if (!ref->IsEmpty())
                    {
                        auto texture = ref->Value();
                        texture.Get("destroy").As<Napi::Function>().Call(texture, {});
                    }
                warp = {};
                m_sessionState->WarpViews = 0;
            }
#endif
            m_sessionState->ActiveViewConfigurations.clear();
            m_sessionState->ViewConfigurationStartViewIdx.clear();
        }

        void NativeXr::Impl::EndFrame()
        {
            assert(m_sessionState != nullptr);
            assert(m_sessionState->Session != nullptr);
            assert(m_sessionState->Frame != nullptr);

            arcana::trace_region endFrameRegion{"NativeXR::EndFrame"};

#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
            const auto& colorTextures = m_sessionState->FrameColorTextureIds;
            if (!colorTextures.empty() && !babylon_wgpu_native_prepare_openxr_textures_for_release(colorTextures.data(), colorTextures.size()))
            {
                throw std::runtime_error{"NativeXR failed to prepare its WebGPU color textures for OpenXR release: " + GetLastWgpuError()};
            }
#endif

            if (!ShouldUseAsyncXrComposite() && !babylon_wgpu_native_queue_wait_submitted_work())
            {
                throw std::runtime_error{"NativeXR failed while waiting for NativeWebGPU frame work before AR compositing."};
            }

            m_sessionState->Frame->Render();
            m_sessionState->Frame.reset();
            m_sessionState->FrameColorTextureIds.clear();
        }
    } // Plugins
} // Babylon
