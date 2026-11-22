#pragma once

#include "SafeTimespanGuarantor.h"
#include "ScreenShotData.h"
#include "continuation_scheduler.h"

#include <napi/env.h>
#include <arcana/threading/task.h>

#include <cstddef>
#include <functional>
#include <optional>

namespace Babylon::Graphics
{
    class Update;
    class DeviceContext;
    class DeviceImpl;

    // Keeps the current graphics frame open until the JavaScript work using
    // it has completed. NativeEngine and NativeXr acquire this from the frame
    // start callback and carry it through their JS-thread callback.
    class FrameCompletionScope final
    {
    public:
        FrameCompletionScope(const FrameCompletionScope&) = delete;
        FrameCompletionScope& operator=(const FrameCompletionScope&) = delete;
        FrameCompletionScope& operator=(FrameCompletionScope&&) = delete;

        FrameCompletionScope(FrameCompletionScope&&) noexcept;
        ~FrameCompletionScope();

    private:
        friend class DeviceContext;

        explicit FrameCompletionScope(DeviceImpl&);
        struct Adopt {};
        FrameCompletionScope(DeviceImpl& impl, Adopt) : m_impl{&impl} {}

        DeviceImpl* m_impl;
    };

    class UpdateToken final
    {
    public:
        UpdateToken(const UpdateToken&) = delete;
        UpdateToken& operator=(const UpdateToken&) = delete;
        UpdateToken(UpdateToken&&) noexcept = default;

    private:
        friend class Update;

        UpdateToken(DeviceContext&, SafeTimespanGuarantor&);

        [[maybe_unused]] DeviceContext& m_context;
        SafeTimespanGuarantor::SafetyGuarantee m_guarantee;
    };

    class Update
    {
    public:
        continuation_scheduler<>& Scheduler()
        {
            return m_safeTimespanGuarantor.OpenScheduler();
        }

        UpdateToken GetUpdateToken()
        {
            return {m_context, m_safeTimespanGuarantor};
        }

    private:
        friend class DeviceContext;

        Update(SafeTimespanGuarantor& safeTimespanGuarantor, DeviceContext& context)
            : m_safeTimespanGuarantor{safeTimespanGuarantor}
            , m_context{context}
        {
        }

        SafeTimespanGuarantor& m_safeTimespanGuarantor;
        DeviceContext& m_context;
    };

    class DeviceContext
    {
    public:
        explicit DeviceContext(DeviceImpl&);

        static DeviceContext& GetFromJavaScript(Napi::Env);
        static std::optional<arcana::task<bool, std::exception_ptr>> TryGetInitializationTask(Napi::Env);

        static Napi::Value Create(Napi::Env, DeviceImpl&);
        static DeviceContext& GetFromJavaScript(Napi::Value);

        continuation_scheduler<>& BeforeRenderScheduler();
        continuation_scheduler<>& AfterRenderScheduler();
        continuation_scheduler<>& FrameStartScheduler();

        Update GetUpdate(const char* updateName);
        FrameCompletionScope AcquireFrameCompletionScope();
        std::optional<FrameCompletionScope> TryAcquireFrameCompletionScope();

        void RequestScreenShot(std::function<void(ScreenShotData)> callback);
        void UpdateMSAA(uint8_t value);
        void SetRenderResetCallback(std::function<void()> callback);

        float GetHardwareScalingLevel();
        void SetHardwareScalingLevel(float level);

        size_t GetWidth() const;
        size_t GetHeight() const;
        float GetDevicePixelRatio();

        uintptr_t GetDeviceId() const;

    private:
        DeviceImpl& m_graphicsImpl;
    };
}
