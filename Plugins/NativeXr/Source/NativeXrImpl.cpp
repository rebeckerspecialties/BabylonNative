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
#if defined(__APPLE__)
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
            if (renderTarget.JsColorTexture.IsEmpty() || renderTarget.JsDepthTexture.IsEmpty())
            {
                return std::nullopt;
            }

            return XRGPUSubImageData{
                renderTarget.JsColorTexture.Value(),
                renderTarget.JsDepthTexture.Value(),
                static_cast<uint32_t>(viewConfig->ViewTextureSize.Width),
                static_cast<uint32_t>(viewConfig->ViewTextureSize.Height),
                renderTargetIndex};
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

        arcana::task<void, std::exception_ptr> NativeXr::Impl::BeginSessionAsync()
        {
            if (m_beginTask)
            {
                return arcana::task_from_error<void>(std::make_exception_ptr(std::runtime_error{"There is already an immersive XR session either currently active or in the process of being set up. There can only be one immersive XR session at a time."}));
            }

            Graphics::DeviceContext& context = Graphics::DeviceContext::GetFromJavaScript(m_env);

            // Don't try to start a session while it is still ending.
            m_beginTask.emplace(m_endTask.then(context.AfterRenderScheduler(), arcana::cancellation::none(),
                [this, thisRef{shared_from_this()}, &context]() {
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
                        nativeVulkanBinding.queue_index});
                    void* graphicsDevice = vulkanBinding.get();
                    void* graphicsQueue = nullptr;
                    std::shared_ptr<void> graphicsBindingLifetime = vulkanBinding;
#else
#error NativeXR requires an explicit platform graphics binding.
#endif

                    return xr::System::Session::CreateAsync(m_system, graphicsDevice, graphicsQueue, [this, thisRef{shared_from_this()}] { return m_windowPtr; })
                        .then(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [this, thisRef{shared_from_this()}, graphicsBindingLifetime{std::move(graphicsBindingLifetime)}](std::shared_ptr<xr::System::Session> session) {
                            static_cast<void>(graphicsBindingLifetime);
                            m_sessionState->Session = std::move(session);
                            NotifySessionStateChanged(true);
                        });
                }));

            return m_beginTask.value();
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
            m_sessionState->ScheduleFrameCallbacks.clear();
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
            m_sessionState->ScheduleFrameCallbacks.emplace_back(std::move(callback));

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
                m_sessionState->ScheduleFrameCallbacks.size());

            if (m_sessionState->FrameScheduled || m_sessionState->ScheduleFrameCallbacks.empty())
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
                        auto callbacks{std::move(m_sessionState->ScheduleFrameCallbacks)};
                        for (auto& callback : callbacks)
                        {
                            callback(m_sessionState->Frame);
                        }
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

                if (it == m_sessionState->TextureToViewConfigurationMap.end() ||
                    it->second.ViewTextureSize.Width != view.ColorTextureSize.Width ||
                    it->second.ViewTextureSize.Height != view.ColorTextureSize.Height ||
                    it->second.ViewTextureSize.Depth != view.ColorTextureSize.Depth)
                {
                    auto& viewConfig = m_sessionState->TextureToViewConfigurationMap[view.ColorTexturePointer] = {};
                    m_sessionState->ActiveViewConfigurations[viewIdx] = &viewConfig;
                    m_sessionState->ViewConfigurationStartViewIdx[&viewConfig] = viewIdx;

                    viewConfig.ColorTexturePointer = view.ColorTexturePointer;
                    viewConfig.DepthTexturePointer = view.DepthTexturePointer;
                    viewConfig.ViewTextureSize = view.ColorTextureSize;

                    assert(view.ColorTextureSize.Width != 0);
                    assert(view.ColorTextureSize.Height != 0);
                    assert(view.ColorTextureSize.Width == view.DepthTextureSize.Width);
                    assert(view.ColorTextureSize.Height == view.DepthTextureSize.Height);
                    assert(view.ColorTextureSize.Depth == view.DepthTextureSize.Depth);

                    const auto textureWidth = static_cast<uint32_t>(view.ColorTextureSize.Width);
                    const auto textureHeight = static_cast<uint32_t>(view.ColorTextureSize.Height);
                    const auto textureLayers = std::max<uint32_t>(1, static_cast<uint32_t>(view.ColorTextureSize.Depth));
                    const auto* colorFormat = XrTextureFormatToWebGpuFormat(view.ColorTextureFormat);
                    const auto* depthFormat = XrDepthTextureFormatToWebGpuFormat(view.DepthTextureFormat);
                    const auto colorUsage = WEBGPU_TEXTURE_USAGE_TEXTURE_BINDING | WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
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
                    const auto depthDescriptor = MakeTextureDescriptorJson("NativeXR.depth", textureWidth, textureHeight, textureLayers, depthFormat, depthUsage, /* transient */ false);
#if defined(__APPLE__)
                    const auto colorTextureId = babylon_wgpu_native_import_metal_texture(view.ColorTexturePointer, colorDescriptor.c_str());
#elif defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                    const auto colorTextureId = babylon_wgpu_native_import_vulkan_texture(
                        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(view.ColorTexturePointer)),
                        colorDescriptor.c_str());
#else
#error NativeXR requires an explicit platform texture import path.
#endif
                    const auto depthTextureId = babylon_wgpu_native_create_texture(depthDescriptor.c_str());

                    if (colorTextureId == 0 || depthTextureId == 0)
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
                    for (uint32_t eyeIdx = 0; eyeIdx < eyeCount; eyeIdx++)
                    {
                        auto& renderTarget = viewConfig.RenderTargets[eyeIdx];
                        auto jsColorTexture = Plugins::NativeWebGPU::CreateTextureFromNativeId(
                            m_env,
                            colorTextureId,
                            "NativeXR.color",
                            colorFormat,
                            textureWidth,
                            textureHeight,
                            textureLayers,
                            colorUsage);
                        auto jsDepthTexture = Plugins::NativeWebGPU::CreateTextureFromNativeId(
                            m_env,
                            depthTextureId,
                            "NativeXR.depth",
                            depthFormat,
                            textureWidth,
                            textureHeight,
                            textureLayers,
                            depthUsage);

                        renderTarget.ColorTextureId = colorTextureId;
                        renderTarget.DepthTextureId = depthTextureId;
                        renderTarget.JsColorTexture = Napi::Persistent(jsColorTexture);
                        renderTarget.JsDepthTexture = Napi::Persistent(jsDepthTexture);
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
                    m_sessionState->FrameColorTextureIds.emplace_back(
                        viewConfig->RenderTargets[renderTargetIndex].ColorTextureId);
                }
            }
#endif
        }

        void NativeXr::Impl::EndUpdate()
        {
            arcana::trace_region endUpdateRegion{"NativeXR::EndUpdate"};
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
            for (const auto colorTextureId : m_sessionState->FrameColorTextureIds)
            {
                if (!babylon_wgpu_native_prepare_openxr_texture_for_release(colorTextureId))
                {
                    throw std::runtime_error{"NativeXR failed to prepare its WebGPU color texture for OpenXR release: " + GetLastWgpuError()};
                }
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
