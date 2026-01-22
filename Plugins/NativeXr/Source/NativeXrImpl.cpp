#include <Babylon/Plugins/NativeXr.h>

#include <Babylon/JsRuntimeScheduler.h>

#include <XR.h>

#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/FrameBuffer.h>
#include <bgfx/bgfx.h>
#include <cstdint>
#include <napi/napi.h>
#include <napi/pointer.h>
#include <arcana/threading/task.h>
#include <arcana/tracing/trace_region.h>
#include "NativeXrImpl.h"

namespace Babylon
{
    namespace
    {
        bgfx::TextureFormat::Enum XrTextureFormatToBgfxFormat(xr::TextureFormat format)
        {
            switch (format)
            {
                // Color Formats
                // NOTE: Use linear formats even though XR requests sRGB to match what happens on the web.
                //       WebGL shaders expect sRGB output while native shaders expect linear output.
            case xr::TextureFormat::BGRA8_SRGB:
                return bgfx::TextureFormat::BGRA8;
            case xr::TextureFormat::RGBA8_SRGB:
                return bgfx::TextureFormat::RGBA8;

                // Depth Formats
            case xr::TextureFormat::D24S8:
                return bgfx::TextureFormat::D24S8;
            case xr::TextureFormat::D16:
                return bgfx::TextureFormat::D16;

            default:
                throw std::runtime_error{ "Unsupported texture format" };
            }
        }

        Graphics::Rect ViewportTransformForStereoView(uint32_t viewIndex, uint32_t viewCount)
        {
            if (viewCount == 2)
            {
                if (viewIndex == 0)
                {
                    return {0.0f, 0.0f, 0.5f, 1.0f};
                }
                if (viewIndex == 1)
                {
                    return {0.5f, 0.0f, 0.5f, 1.0f};
                }
            }

            return {0.0f, 0.0f, 1.0f, 1.0f};
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
                    m_capabilities = m_system.GetCapabilities();
                    m_sessionState->Capabilities = m_capabilities;

                    return xr::System::Session::CreateAsync(m_system, bgfx::getInternalData()->context, bgfx::getInternalData()->commandQueue, [this, thisRef{shared_from_this()}] { return m_windowPtr; })
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

            m_sessionState->ActiveViewConfigurations.clear();
            m_sessionState->ViewConfigurationStartViewIdx.clear();
            m_sessionState->TextureToViewConfigurationMap.clear();
            m_sessionState->ScheduleFrameCallbacks.clear();
            m_sessionState->CreateRenderTexture.Reset();

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
            if (m_sessionState->FrameScheduled)
            {
                return;
            }

            m_sessionState->FrameScheduled = true;

            // REVIEW: This should technically be before the check for m_frameScheduled, but for some
            // reason requestAnimationFrame is being called twice when starting XR.
            m_sessionState->ScheduleFrameCallbacks.emplace_back(callback);

            m_sessionState->FrameTask = arcana::make_task(m_sessionState->Update.Scheduler(), m_sessionState->CancellationSource, [this, thisRef{shared_from_this()}] {
                BeginFrame();

                return arcana::make_task(m_runtimeScheduler, m_sessionState->CancellationSource, [this, updateToken{m_sessionState->Update.GetUpdateToken()}, thisRef{shared_from_this()}]() {
                    m_sessionState->FrameScheduled = false;

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
                (void)texturePointer;
                return arcana::make_task(m_runtimeScheduler, arcana::cancellation::none(), [this]() {
                    if (!m_sessionState)
                    {
                        return;
                    }

                    std::vector<size_t> viewConfigKeys{};
                    viewConfigKeys.reserve(m_sessionState->TextureToViewConfigurationMap.size());
                    for (const auto& kvp : m_sessionState->TextureToViewConfigurationMap)
                    {
                        viewConfigKeys.push_back(kvp.first);
                    }

                    for (size_t viewConfigKey : viewConfigKeys)
                    {
                        const auto itViewConfig{m_sessionState->TextureToViewConfigurationMap.find(viewConfigKey)};
                        if (itViewConfig == m_sessionState->TextureToViewConfigurationMap.end())
                        {
                            continue;
                        }

                        auto& viewConfig = itViewConfig->second;
                        auto& frameBuffers = viewConfig.FrameBuffers;
                        for (const auto& frameBuffer : frameBuffers)
                        {
                            auto itJsTexture = viewConfig.JsTextures.find(frameBuffer);
                            if (itJsTexture != viewConfig.JsTextures.end())
                            {
                                m_sessionState->DestroyRenderTexture.Call({itJsTexture->second.Value()});
                            }
                            delete frameBuffer;
                        }

                        viewConfig.FrameBuffers.clear();
                        viewConfig.JsTextures.clear();

                        if (bgfx::isValid(viewConfig.ColorTexture))
                        {
                            bgfx::destroy(viewConfig.ColorTexture);
                            viewConfig.ColorTexture = BGFX_INVALID_HANDLE;
                        }
                        if (bgfx::isValid(viewConfig.DepthTexture))
                        {
                            bgfx::destroy(viewConfig.DepthTexture);
                            viewConfig.DepthTexture = BGFX_INVALID_HANDLE;
                        }

                        m_sessionState->TextureToViewConfigurationMap.erase(viewConfigKey);
                    }
                }).then(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [] {}); // Ensure continuations run on the render thread if they use inline_scheduler.
            });

            // Ending a session outside of calls to EndSessionAsync() is currently not supported.
            assert(!shouldEndSession);
            assert(m_sessionState->Frame != nullptr);

            // For runtimes like OpenXR where swapchain images rotate each frame, update the native
            // texture pointers on the render thread before JS submits any draws for this frame.
            for (uint32_t viewIdx = 0; viewIdx < m_sessionState->Frame->Views.size(); ++viewIdx)
            {
                const auto& view = m_sessionState->Frame->Views[viewIdx];
                const auto itViewConfig = m_sessionState->TextureToViewConfigurationMap.find(viewIdx);
                if (itViewConfig == m_sessionState->TextureToViewConfigurationMap.end())
                {
                    continue;
                }

                auto& viewConfig = itViewConfig->second;
                if (viewConfig.ColorTexturePointer != view.ColorTexturePointer)
                {
                    viewConfig.ColorTexturePointer = view.ColorTexturePointer;
                    if (viewConfig.ColorTexturePointer != nullptr && bgfx::isValid(viewConfig.ColorTexture))
                    {
                        bgfx::overrideInternal(viewConfig.ColorTexture, reinterpret_cast<uintptr_t>(viewConfig.ColorTexturePointer));
                    }
                }

                if (viewConfig.DepthTexturePointer != view.DepthTexturePointer)
                {
                    viewConfig.DepthTexturePointer = view.DepthTexturePointer;
                    if (viewConfig.DepthTexturePointer != nullptr && bgfx::isValid(viewConfig.DepthTexture))
                    {
                        bgfx::overrideInternal(viewConfig.DepthTexture, reinterpret_cast<uintptr_t>(viewConfig.DepthTexturePointer));
                    }
                }
            }
        }

        void NativeXr::Impl::BeginUpdate()
        {
            arcana::trace_region beginUpdateRegion{"NativeXR::BeginUpdate"};

            const auto viewCount = static_cast<uint32_t>(m_sessionState->Frame->Views.size());
            const bool useSceneCoreBackBuffer = m_system.GetNativeXrContextType() == "SceneCore";

            m_sessionState->ActiveViewConfigurations.resize(m_sessionState->Frame->Views.size());
            for (uint32_t viewIdx = 0; viewIdx < m_sessionState->Frame->Views.size(); viewIdx++)
            {
                const auto& view = m_sessionState->Frame->Views[viewIdx];
                const auto& it{m_sessionState->TextureToViewConfigurationMap.find(viewIdx)};
                auto cleanupViewConfig = [this](ViewConfiguration& viewConfig) {
                    for (auto* frameBuffer : viewConfig.FrameBuffers)
                    {
                        if (frameBuffer != nullptr)
                        {
                            auto jsIt = viewConfig.JsTextures.find(frameBuffer);
                        if (jsIt != viewConfig.JsTextures.end())
                        {
                            m_sessionState->DestroyRenderTexture.Call({jsIt->second.Value()});
                        }
                        delete frameBuffer;
                    }
                    }
                    viewConfig.FrameBuffers.clear();
                    viewConfig.JsTextures.clear();
                    if (bgfx::isValid(viewConfig.ColorTexture))
                    {
                        bgfx::destroy(viewConfig.ColorTexture);
                        viewConfig.ColorTexture = BGFX_INVALID_HANDLE;
                    }
                    if (bgfx::isValid(viewConfig.DepthTexture))
                    {
                        bgfx::destroy(viewConfig.DepthTexture);
                        viewConfig.DepthTexture = BGFX_INVALID_HANDLE;
                    }
                };

                if (it == m_sessionState->TextureToViewConfigurationMap.end() ||
                    it->second.ViewTextureSize.Width != view.ColorTextureSize.Width ||
                    it->second.ViewTextureSize.Height != view.ColorTextureSize.Height ||
                    it->second.ViewTextureSize.Depth != view.ColorTextureSize.Depth)
                {
                    if (it != m_sessionState->TextureToViewConfigurationMap.end())
                    {
                        cleanupViewConfig(it->second);
                    }

                    auto& viewConfig = m_sessionState->TextureToViewConfigurationMap[viewIdx] = {};
                    m_sessionState->ActiveViewConfigurations[viewIdx] = &viewConfig;
                    m_sessionState->ViewConfigurationStartViewIdx[&viewConfig] = viewIdx;

                    viewConfig.ColorTexturePointer = view.ColorTexturePointer;
                    viewConfig.DepthTexturePointer = view.DepthTexturePointer;
                    viewConfig.ViewTextureSize = view.ColorTextureSize;

                    if (useSceneCoreBackBuffer)
                    {
                        viewConfig.ViewTextureSize = {
                            std::max<size_t>(1, m_sessionState->GraphicsContext.GetWidth()),
                            std::max<size_t>(1, m_sessionState->GraphicsContext.GetHeight()),
                            1};

                        viewConfig.FrameBuffers.resize(1);
                        auto* frameBufferPtr = new Graphics::FrameBuffer(
                            m_sessionState->GraphicsContext,
                            BGFX_INVALID_HANDLE,
                            0,
                            0,
                            true,
                            true,
                            true);

                        const auto viewportTransform = ViewportTransformForStereoView(viewIdx, viewCount);
                        frameBufferPtr->SetViewportTransform(viewportTransform.X, viewportTransform.Y, viewportTransform.Width, viewportTransform.Height);

                        // WebXR, at least in its current implementation, specifies an implicit default clear to black.
                        // https://immersive-web.github.io/webxr/#xrwebgllayer-interface
                        frameBufferPtr->Clear(*m_sessionState->Update.GetUpdateToken().GetEncoder(), BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0, 1.0f, 0);

                        viewConfig.FrameBuffers[0] = frameBufferPtr;

                        auto jsWidth{Napi::Value::From(m_env, viewConfig.ViewTextureSize.Width)};
                        auto jsHeight{Napi::Value::From(m_env, viewConfig.ViewTextureSize.Height)};
                        auto jsFrameBuffer{Napi::Pointer<Graphics::FrameBuffer>::Create(m_env, frameBufferPtr, Napi::NapiPointerDeleter(frameBufferPtr))};
                        viewConfig.JsTextures[frameBufferPtr] = Napi::Persistent(m_sessionState->CreateRenderTexture.Call({jsWidth, jsHeight, jsFrameBuffer}).As<Napi::Object>());
                        viewConfig.JsTextures[frameBufferPtr].Set("skipInitialClear", false);
                        viewConfig.Initialized = true;
                        continue;
                    }

                    // If a texture width or height is 0, bgfx will assert (can't create 0 sized texture). Asserting here instead of deeper in bgfx rendering.
                    // Depth (numLayers) can be 0, bgfx will just reinterpret it as max(numLayers, 1).
                    assert(view.ColorTextureSize.Width != 0);
                    assert(view.ColorTextureSize.Height != 0);
                    assert(view.ColorTextureSize.Width == view.DepthTextureSize.Width);
                    assert(view.ColorTextureSize.Height == view.DepthTextureSize.Height);
                    assert(view.ColorTextureSize.Depth == view.DepthTextureSize.Depth);

                    const auto textureWidth = static_cast<uint16_t>(view.ColorTextureSize.Width);
                    const auto textureHeight = static_cast<uint16_t>(view.ColorTextureSize.Height);
                    const auto textureLayers = std::max(static_cast<uint16_t>(1), static_cast<uint16_t>(view.ColorTextureSize.Depth));

                    // Create textures with the desired size. It will be freed and replaced with overrideInternal call
                    // This is mandatory as overrideInternal do not update texture size.
                    // And size is used for determining viewport when rendering to texture.
                    auto colorTextureFormat = XrTextureFormatToBgfxFormat(view.ColorTextureFormat);
                    auto colorTexture = bgfx::createTexture2D(textureWidth, textureHeight, false, textureLayers, colorTextureFormat, BGFX_TEXTURE_RT);
                    m_sessionState->GraphicsContext.AddTexture(colorTexture, textureWidth, textureHeight, false, textureLayers, colorTextureFormat);
                    viewConfig.ColorTexture = colorTexture;

                    auto depthTextureFormat = XrTextureFormatToBgfxFormat(view.DepthTextureFormat);
                    auto depthTexture = bgfx::createTexture2D(textureWidth, textureHeight, false, textureLayers, depthTextureFormat, BGFX_TEXTURE_RT);
                    m_sessionState->GraphicsContext.AddTexture(depthTexture, textureWidth, textureHeight, false, textureLayers, depthTextureFormat);
                    viewConfig.DepthTexture = depthTexture;

                    auto requiresAppClear = view.RequiresAppClear;

                    arcana::make_task(m_sessionState->GraphicsContext.AfterRenderScheduler(), arcana::cancellation::none(), [colorTexture, depthTexture, &viewConfig]() {
                        if (viewConfig.ColorTexturePointer != nullptr)
                        {
                            bgfx::overrideInternal(colorTexture, reinterpret_cast<uintptr_t>(viewConfig.ColorTexturePointer));
                        }
                        if (viewConfig.DepthTexturePointer != nullptr)
                        {
                            bgfx::overrideInternal(depthTexture, reinterpret_cast<uintptr_t>(viewConfig.DepthTexturePointer));
                        }
                    }).then(m_runtimeScheduler, m_sessionState->CancellationSource, [this, thisRef{shared_from_this()}, colorTexture, depthTexture, requiresAppClear, &viewConfig]() {
                          const auto eyeCount = std::max(static_cast<uint16_t>(1), static_cast<uint16_t>(viewConfig.ViewTextureSize.Depth));
                          // TODO (rgerd): Remove old framebuffers from resource table?
                          viewConfig.FrameBuffers.resize(eyeCount);
                          for (uint16_t eyeIdx = 0; eyeIdx < eyeCount; eyeIdx++)
                          {
                              std::array<bgfx::Attachment, 2> attachments{};
                              attachments[0].init(colorTexture, bgfx::Access::Write, eyeIdx);
                              attachments[1].init(depthTexture, bgfx::Access::Write, eyeIdx);

                              auto frameBufferHandle = bgfx::createFrameBuffer(static_cast<uint8_t>(attachments.size()), attachments.data(), false);

                              const auto frameBufferPtr = new Graphics::FrameBuffer(
                                  m_sessionState->GraphicsContext,
                                  frameBufferHandle,
                                  static_cast<uint16_t>(viewConfig.ViewTextureSize.Width),
                                  static_cast<uint16_t>(viewConfig.ViewTextureSize.Height),
                                  true,
                                  true,
                                  true);

                              auto& frameBuffer = *frameBufferPtr;

                              // WebXR, at least in its current implementation, specifies an implicit default clear to black.
                              // https://immersive-web.github.io/webxr/#xrwebgllayer-interface
                              frameBuffer.Clear(*m_sessionState->Update.GetUpdateToken().GetEncoder(), BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0, 1.0f, 0);

                              viewConfig.FrameBuffers[eyeIdx] = frameBufferPtr;

                              auto jsWidth{Napi::Value::From(m_env, viewConfig.ViewTextureSize.Width)};
                              auto jsHeight{Napi::Value::From(m_env, viewConfig.ViewTextureSize.Height)};
                              auto jsFrameBuffer{Napi::Pointer<Graphics::FrameBuffer>::Create(m_env, frameBufferPtr, Napi::NapiPointerDeleter(frameBufferPtr))};
                              viewConfig.JsTextures[frameBufferPtr] = Napi::Persistent(m_sessionState->CreateRenderTexture.Call({jsWidth, jsHeight, jsFrameBuffer}).As<Napi::Object>());
                              // OpenXR doesn't pre-clear textures, and so we need to make sure the render target gets cleared before rendering the scene.
                              // ARCore and ARKit effectively pre-clear by pre-compositing the camera feed.
                              if (requiresAppClear)
                              {
                                  viewConfig.JsTextures[frameBufferPtr].Set("skipInitialClear", false);
                              }
                          }
                          viewConfig.Initialized = true;
                      }).then(arcana::inline_scheduler, m_sessionState->CancellationSource, [env{m_env}](const arcana::expected<void, std::exception_ptr>& result) {
                        if (result.has_error())
                        {
                            Napi::Error::New(env, result.error()).ThrowAsJavaScriptException();
                        }
                    });
                }
                else
                {
                    auto& viewConfig = it->second;
                    m_sessionState->ActiveViewConfigurations[viewIdx] = &viewConfig;
                    m_sessionState->ViewConfigurationStartViewIdx.try_emplace(&viewConfig, viewIdx);
                }

                auto* activeViewConfig = m_sessionState->ActiveViewConfigurations[viewIdx];
                if (activeViewConfig != nullptr
                    && !m_sessionState->Frame->CameraImageRGBA.empty()
                    && bgfx::isValid(activeViewConfig->ColorTexture))
                {
                    if (m_sessionState->Frame->CameraImageWidth != view.ColorTextureSize.Width
                        || m_sessionState->Frame->CameraImageHeight != view.ColorTextureSize.Height)
                    {
                        continue;
                    }

                    const auto width = static_cast<uint16_t>(view.ColorTextureSize.Width);
                    const auto height = static_cast<uint16_t>(view.ColorTextureSize.Height);
                    const size_t requiredSize = static_cast<size_t>(width) * height * 4;
                    if (m_sessionState->Frame->CameraImageRGBA.size() >= requiredSize)
                    {
                        const bgfx::Memory* mem = bgfx::copy(m_sessionState->Frame->CameraImageRGBA.data(), requiredSize);
                        bgfx::updateTexture2D(activeViewConfig->ColorTexture, 0, 0, 0, 0, width, height, mem, UINT16_MAX);
                    }
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

            m_sessionState->Frame->Render();
        }
    } // Plugins
} // Babylon
