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
#include <cstdlib>
#include <sstream>
#include "NativeXrImpl.h"

namespace Babylon
{
    namespace
    {
        constexpr uint32_t WEBGPU_TEXTURE_USAGE_COPY_SRC{0x01};
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
            uint32_t usage)
        {
            std::ostringstream descriptor{};
            descriptor
                << "{\"label\":\"" << label
                << "\",\"size\":{\"width\":" << width
                << ",\"height\":" << height
                << ",\"depthOrArrayLayers\":" << std::max<uint32_t>(1, depthOrArrayLayers)
                << "},\"mipLevelCount\":1,\"sampleCount\":1,\"dimension\":\"2d\",\"format\":\""
                << format << "\",\"usage\":" << usage << "}";
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

        bool ShouldUseAsyncXrComposite()
        {
            static const bool enabled = std::getenv("BABYLON_NATIVE_XR_ASYNC_COMPOSITE") != nullptr;
            static bool didLog{};
            if (enabled && !didLog)
            {
                didLog = true;
                std::fprintf(stderr, "NativeXR async composite enabled: skipping per-frame WebGPU queue wait before AR compositing.\n");
            }
            return enabled;
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

                    if (!m_system.IsInitialized() &&
                        !m_system.TryInitialize())
                    {
                        throw std::runtime_error{"Failed to initialize xr system."};
                    }

                    auto* metalDevice = babylon_wgpu_native_get_metal_device();
                    if (metalDevice == nullptr)
                    {
                        throw std::runtime_error{"NativeXR requires an initialized Metal-backed NativeWebGPU device."};
                    }

                    return xr::System::Session::CreateAsync(m_system, metalDevice, nullptr, [this, thisRef{shared_from_this()}] { return m_windowPtr; })
                        .then(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [this, thisRef{shared_from_this()}](std::shared_ptr<xr::System::Session> session) {
                            m_sessionState->Session = std::move(session);
                            NotifySessionStateChanged(true);
                        });
                }));

            return m_beginTask.value();
        }

        arcana::task<void, std::exception_ptr> NativeXr::Impl::EndSessionAsync()
        {
            assert(m_beginTask);
            assert(m_sessionState != nullptr);

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
            m_endTask = m_beginTask->then(arcana::inline_scheduler, arcana::cancellation::none(), [this, thisRef{shared_from_this()}] {
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

            if (m_sessionState->FrameScheduled || m_sessionState->ScheduleFrameCallbacks.empty())
            {
                return;
            }

            m_sessionState->FrameScheduled = true;

            m_sessionState->FrameTask = arcana::make_task(m_sessionState->Update.Scheduler(), m_sessionState->CancellationSource, [this, thisRef{shared_from_this()}] {
                BeginFrame();

                return arcana::make_task(m_runtimeScheduler, m_sessionState->CancellationSource, [this, updateToken{m_sessionState->Update.GetUpdateToken()}, thisRef{shared_from_this()}]() {
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
                  }).then(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [this, thisRef{shared_from_this()}](const arcana::expected<void, std::exception_ptr>&) {
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
            m_sessionState->Frame = m_sessionState->Session->GetNextFrame(shouldEndSession, shouldRestartSession, [this](void* texturePointer) {
                return arcana::make_task(m_runtimeScheduler, arcana::cancellation::none(), [this, texturePointer]() {
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
            for (uint32_t viewIdx = 0; viewIdx < m_sessionState->Frame->Views.size(); viewIdx++)
            {
                const auto& view = m_sessionState->Frame->Views[viewIdx];
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
                    const auto colorUsage = WEBGPU_TEXTURE_USAGE_COPY_SRC | WEBGPU_TEXTURE_USAGE_TEXTURE_BINDING | WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
                    const auto depthUsage = WEBGPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
                    const auto colorDescriptor = MakeTextureDescriptorJson("NativeXR.color", textureWidth, textureHeight, textureLayers, colorFormat, colorUsage);
                    const auto depthDescriptor = MakeTextureDescriptorJson("NativeXR.depth", textureWidth, textureHeight, textureLayers, depthFormat, depthUsage);
                    const auto colorTextureId = babylon_wgpu_native_import_metal_texture(view.ColorTexturePointer, colorDescriptor.c_str());
                    const auto depthTextureId = babylon_wgpu_native_create_texture(depthDescriptor.c_str());

                    if (colorTextureId == 0 || depthTextureId == 0)
                    {
                        std::ostringstream error{};
                        error << "NativeXR failed to create WebGPU render targets for ARKit frame"
                            << " (colorImportId=" << colorTextureId
                            << ", depthCreateId=" << depthTextureId << ")";
                        const auto detail = GetLastWgpuError();
                        if (!detail.empty())
                        {
                            error << ": " << detail;
                        }
                        throw std::runtime_error{error.str()};
                    }

                    auto requiresAppClear = view.RequiresAppClear;

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

                        auto jsWidth{Napi::Value::From(m_env, textureWidth)};
                        auto jsHeight{Napi::Value::From(m_env, textureHeight)};
                        auto jsRenderTarget = m_sessionState->CreateRenderTexture.Call({jsWidth, jsHeight, m_env.Null(), jsColorTexture, jsDepthTexture}).As<Napi::Object>();
                        jsRenderTarget.Set("skipInitialClear", Napi::Boolean::New(m_env, !requiresAppClear));

                        renderTarget.ColorTextureId = colorTextureId;
                        renderTarget.DepthTextureId = depthTextureId;
                        renderTarget.JsColorTexture = Napi::Persistent(jsColorTexture);
                        renderTarget.JsDepthTexture = Napi::Persistent(jsDepthTexture);
                        renderTarget.JsRenderTarget = Napi::Persistent(jsRenderTarget);
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

            if (!ShouldUseAsyncXrComposite() && !babylon_wgpu_native_queue_wait_submitted_work())
            {
                throw std::runtime_error{"NativeXR failed while waiting for NativeWebGPU frame work before AR compositing."};
            }

            m_sessionState->Frame->Render();
            m_sessionState->Frame.reset();
        }
    } // Plugins
} // Babylon
