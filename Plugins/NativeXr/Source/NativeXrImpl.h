#pragma once

#include "XRGPUSubImage.h"

#include <gsl/util>
#include <optional>

namespace Babylon
{
    namespace Plugins
    {
        class NativeXr::Impl final : public std::enable_shared_from_this<NativeXr::Impl>
        {
        public:
            explicit Impl(Napi::Env);

            void UpdateWindow(void* windowPtr);
            void SetSessionStateChangedCallback(std::function<void(bool)> callback);

            arcana::task<void, std::exception_ptr> BeginSessionAsync();
            arcana::task<void, std::exception_ptr> EndSessionAsync();

            std::weak_ptr<xr::System::Session> GetPlatformSession() const
            {
                return m_sessionState ? m_sessionState->Session : nullptr;
            }

            void ScheduleFrame(std::function<void(const std::shared_ptr<const xr::System::Session::Frame>&)>&& callback);

            void SetRenderTextureFunctions(const Napi::Function& createFunction, const Napi::Function& destroyFunction);

            Napi::Value GetRenderTargetForViewIndex(uint32_t viewIndex) const;
            std::optional<XRGPUSubImageData> GetWebGPUSubImage(uint32_t viewIndex) const;
            uint32_t GetWebGPUTextureWidth() const;
            const char* GetWebGPUColorFormat() const;
            uint32_t GetWebGPUTextureHeight() const;
            uint32_t GetWebGPUTextureArrayLength() const;
            void SetProjectionAttachments(std::optional<std::string> format, uint32_t colorUsage)
            {
                m_sessionState->ProjectionDepthFormat = std::move(format);
                m_sessionState->ProjectionColorUsage = colorUsage;
            }

            void SetDepthsNarFar(float depthNear, float depthFar)
            {
                m_sessionState->Session->SetDepthsNearFar(depthNear, depthFar);
            }

            void SetPlaneDetectionEnabled(bool enabled)
            {
                m_sessionState->Session->SetPlaneDetectionEnabled(enabled);
            }

            bool TrySetFeaturePointCloudEnabled(bool enabled)
            {
                return m_sessionState->Session->TrySetFeaturePointCloudEnabled(enabled);
            }

            bool TrySetPreferredPlaneDetectorOptions(const xr::GeometryDetectorOptions& options)
            {
                return m_sessionState->Session->TrySetPreferredPlaneDetectorOptions(options);
            }

            bool TrySetMeshDetectorEnabled(const bool enabled)
            {
                return m_sessionState->Session->TrySetMeshDetectorEnabled(enabled);
            }

            bool TrySetPreferredMeshDetectorOptions(const xr::GeometryDetectorOptions& options)
            {
                return m_sessionState->Session->TrySetPreferredMeshDetectorOptions(options);
            }

            std::vector<xr::ImageTrackingScore>* GetImageTrackingScores()
            {
                return m_sessionState->Session->GetImageTrackingScores();
            }

            void CreateAugmentedImageDatabase(const std::vector<xr::System::Session::ImageTrackingRequest>& requests)
            {
                m_sessionState->Session->CreateAugmentedImageDatabase(requests);
            }

            uintptr_t GetNativeXrContext()
            {
                return m_system.GetNativeXrContext();
            }

            std::string GetNativeXrContextType()
            {
                return m_system.GetNativeXrContextType();
            }

        private:
            Napi::Env m_env;
            JsRuntimeScheduler m_runtimeScheduler;
            std::mutex m_sessionStateChangedCallbackMutex{};
            std::function<void(bool)> m_sessionStateChangedCallback{};
            void* m_windowPtr{};
            std::optional<arcana::task<void, std::exception_ptr>> m_beginTask{};
            arcana::task<void, std::exception_ptr> m_endTask{arcana::task_from_result<std::exception_ptr>()};
            bool m_sessionEnding{false};

            struct ViewConfiguration final
            {
                struct RenderTarget final
                {
                    Napi::ObjectReference JsRenderTarget{};
                    Napi::ObjectReference JsColorTexture{};
                    Napi::ObjectReference JsDepthTexture{};
                    uint64_t ColorTextureId{};
                    uint64_t DepthTextureId{};
                };

                void* ColorTexturePointer{nullptr};
                void* DepthTexturePointer{nullptr};
                xr::Size ViewTextureSize{};
                std::string DepthFormat{};
                uint32_t ColorUsage{};
                std::vector<RenderTarget> RenderTargets{};
                bool Initialized{false};
            };

            struct SessionState final
            {
                explicit SessionState(Graphics::DeviceContext& graphicsContext)
                    : GraphicsContext{graphicsContext}
                    , Update{graphicsContext.GetUpdate("NativeXR")}
                {
                }

                Graphics::DeviceContext& GraphicsContext;
                Graphics::Update Update;
                Napi::FunctionReference CreateRenderTexture{};
                Napi::FunctionReference DestroyRenderTexture{};
                std::vector<ViewConfiguration*> ActiveViewConfigurations{};
                std::unordered_map<ViewConfiguration*, uint32_t> ViewConfigurationStartViewIdx{};
                std::vector<uint64_t> FrameColorTextureIds{};
                std::unordered_map<void*, ViewConfiguration> TextureToViewConfigurationMap{};
                uint32_t LastTextureWidth{};
                uint32_t LastTextureHeight{};
                uint32_t LastTextureArrayLength{};
                // No projection layer uses the platform default. An explicitly color-only layer uses "".
                std::optional<std::string> ProjectionDepthFormat{};
                uint32_t ProjectionColorUsage{};
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                // Destroy the OpenXR session before returning the window to Vulkan WSI.
                std::optional<gsl::final_action<void (*)()>> ResumeWindowPresentation;
#endif
                std::shared_ptr<xr::System::Session> Session{};
                std::shared_ptr<xr::System::Session::Frame> Frame{};
                arcana::cancellation_source CancellationSource{};
                bool FrameScheduled{false};
                std::vector<std::function<void(const std::shared_ptr<const xr::System::Session::Frame>&)>> ScheduleFrameCallbacks{};
                arcana::task<void, std::exception_ptr> FrameTask{arcana::task_from_result<std::exception_ptr>()};
            };

            std::unique_ptr<SessionState> m_sessionState{};
            xr::System m_system{};

            void BeginFrame();
            void BeginUpdate();
            void EndUpdate();
            void EndFrame();
            void SchedulePendingFrame();

            void NotifySessionStateChanged(bool isSessionActive);
        };
    } // Plugins
} // Babylon
