#include "DeviceImpl.h"

#include <Babylon/JsRuntime.h>
#include <napi/napi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <sstream>
#include <stdexcept>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace
{
    constexpr auto JS_GRAPHICS_NAME = "_Graphics";
}

namespace Babylon::Graphics
{
    DeviceImpl::DeviceImpl(const Configuration& config)
        : m_context{*this}
    {
        std::scoped_lock lock{m_state.Mutex};
        m_state.Window = config.Window;
        m_state.Device = config.Device;
        m_state.Resolution.Width = std::max<size_t>(1, config.Width);
        m_state.Resolution.Height = std::max<size_t>(1, config.Height);
        m_state.Resolution.HardwareScalingLevel = 1.0f;
        m_state.Resolution.DevicePixelRatio = GetDevicePixelRatio(config.Window);
    }

    DeviceImpl::~DeviceImpl()
    {
        DisableRendering();
    }

    void DeviceImpl::UpdateWindow(WindowT window)
    {
        std::scoped_lock lock{m_state.Mutex};
        m_state.Window = window;
        m_state.Resolution.DevicePixelRatio = GetDevicePixelRatio(window);
    }

    void DeviceImpl::UpdateDevice(DeviceT device)
    {
        std::scoped_lock lock{m_state.Mutex};
        m_state.Device = device;
    }

    void DeviceImpl::UpdateSize(size_t width, size_t height)
    {
        std::shared_ptr<WgpuNative> wgpu{};
        uint32_t renderWidth{};
        uint32_t renderHeight{};

        {
            std::scoped_lock lock{m_state.Mutex};

            m_state.Resolution.Width = std::max<size_t>(1, width);
            m_state.Resolution.Height = std::max<size_t>(1, height);

            wgpu = m_wgpu;
            if (wgpu)
            {
                renderWidth = CurrentRenderWidth();
                renderHeight = CurrentRenderHeight();
            }
        }

        if (wgpu)
        {
            wgpu->Resize(renderWidth, renderHeight);
        }
    }

    void DeviceImpl::UpdateMSAA(uint8_t value)
    {
        if (m_diagnosticOutput && value > 1)
        {
            m_diagnosticOutput("WGPU backend currently ignores MSAA configuration.");
        }
    }

    void DeviceImpl::UpdateAlphaPremultiplied(bool enabled)
    {
        if (m_diagnosticOutput && enabled)
        {
            m_diagnosticOutput("WGPU backend does not yet apply alpha premultiplication controls.");
        }
    }

#ifdef GRAPHICS_BACK_BUFFER_SUPPORT
    void DeviceImpl::UpdateBackBuffer(BackBufferColorT, BackBufferDepthStencilT)
    {
        if (m_diagnosticOutput)
        {
            m_diagnosticOutput("WGPU backend ignores externally supplied back buffers.");
        }
    }
#endif

    void DeviceImpl::AddToJavaScript(Napi::Env env)
    {
        JsRuntime::NativeObject::GetFromJavaScript(env)
            .Set(JS_GRAPHICS_NAME, Napi::External<DeviceImpl>::New(env, this));
    }

    DeviceImpl& DeviceImpl::GetFromJavaScript(Napi::Env env)
    {
        return *JsRuntime::NativeObject::GetFromJavaScript(env)
                    .Get(JS_GRAPHICS_NAME)
                    .As<Napi::External<DeviceImpl>>()
                    .Data();
    }

    Napi::Value DeviceImpl::CreateContext(Napi::Env env)
    {
        return DeviceContext::Create(env, *this);
    }

    void DeviceImpl::SetRenderResetCallback(std::function<void()> callback)
    {
        m_renderResetCallback = std::move(callback);
    }

    void DeviceImpl::BeginRenderingInitialization()
    {
        WgpuBootstrapConfig config{};
        {
            std::scoped_lock lock{m_state.Mutex};
            if (m_wgpu || m_pendingWgpu.valid())
            {
                m_rendering = true;
                return;
            }

            m_cancellationSource = std::make_shared<arcana::cancellation_source>();

            config.Width = CurrentRenderWidth();
            config.Height = CurrentRenderHeight();
            config.PreferLowPower = false;
            config.EnableValidation = false;
#if defined(__APPLE__)
            config.SurfaceLayer = static_cast<void*>(m_state.Window);
#elif defined(__ANDROID__)
            config.SurfaceLayer = m_state.Window;
#endif

            m_pendingWgpuWidth = config.Width;
            m_pendingWgpuHeight = config.Height;
            m_rendering = true;
            m_wgpuInitializationLogged = false;
            m_pendingWgpu = std::async(std::launch::async, [config] {
                return std::make_shared<WgpuNative>(config);
            }).share();
        }
    }

    std::shared_ptr<WgpuNative> DeviceImpl::CompleteRenderingInitialization(std::exception_ptr& error)
    {
        std::shared_future<std::shared_ptr<WgpuNative>> pendingWgpu{};
        uint32_t pendingWidth{};
        uint32_t pendingHeight{};

        {
            std::scoped_lock lock{m_state.Mutex};
            if (m_wgpu)
            {
                return m_wgpu;
            }

            pendingWgpu = m_pendingWgpu;
            pendingWidth = m_pendingWgpuWidth;
            pendingHeight = m_pendingWgpuHeight;
        }

        if (!pendingWgpu.valid())
        {
            return {};
        }

        std::shared_ptr<WgpuNative> wgpu{};
        try
        {
            wgpu = pendingWgpu.get();
        }
        catch (...)
        {
            error = std::current_exception();
        }

        if (error)
        {
            std::scoped_lock lock{m_state.Mutex};
            m_pendingWgpu = {};
            m_cancellationSource.reset();
            m_rendering = false;
            return {};
        }

        if (!wgpu || !wgpu->IsValid())
        {
            std::string errorMessage{"Failed to initialize WGPU backend."};
            if (wgpu)
            {
                const auto& details = wgpu->GetLastError();
                if (!details.empty())
                {
                    if (m_diagnosticOutput)
                    {
                        m_diagnosticOutput(details.c_str());
                    }

                    errorMessage += " ";
                    errorMessage += details;
                }
            }

            {
                std::scoped_lock lock{m_state.Mutex};
                m_pendingWgpu = {};
                m_cancellationSource.reset();
                m_rendering = false;
            }

#if defined(__ANDROID__)
            if (m_diagnosticOutput)
            {
                using clock = std::chrono::steady_clock;
                static auto s_lastRetryLog = clock::now() - std::chrono::seconds{5};
                const auto now = clock::now();
                if (now - s_lastRetryLog >= std::chrono::seconds{1})
                {
                    m_diagnosticOutput(
                        "WGPU initialization failed; deferring and retrying on future frames.");
                    s_lastRetryLog = now;
                }
            }
            return {};
#else
            error = std::make_exception_ptr(std::runtime_error{errorMessage});
            return {};
#endif
        }

        bool shouldTriggerReset{};
        bool shouldResize{};
        {
            std::scoped_lock lock{m_state.Mutex};
            if (!m_wgpu)
            {
                m_wgpu = wgpu;
                m_pendingWgpu = {};
                shouldTriggerReset = m_deviceId != 0;
                shouldResize =
                    pendingWidth != CurrentRenderWidth() ||
                    pendingHeight != CurrentRenderHeight();
            }
            else
            {
                wgpu = m_wgpu;
            }
        }

        if (shouldResize)
        {
            wgpu->Resize(CurrentRenderWidth(), CurrentRenderHeight());
        }

        LogWgpuInitialized(wgpu);

        if (shouldTriggerReset && m_renderResetCallback)
        {
            m_renderResetCallback();
        }

        return wgpu;
    }

    std::shared_ptr<WgpuNative> DeviceImpl::CompleteRenderingInitialization()
    {
        std::exception_ptr error{};
        auto wgpu = CompleteRenderingInitialization(error);
        if (error)
        {
            std::rethrow_exception(error);
        }

        return wgpu;
    }

    void DeviceImpl::LogWgpuInitialized(const std::shared_ptr<WgpuNative>& wgpu)
    {
        if (!m_diagnosticOutput || !wgpu)
        {
            return;
        }

        {
            std::scoped_lock lock{m_state.Mutex};
            if (m_wgpuInitializationLogged)
            {
                return;
            }
            m_wgpuInitializationLogged = true;
        }

        auto info = wgpu->GetInfo();
        std::ostringstream stream{};
        stream << "WGPU initialized (backend=" << info.Backend
               << ", vendor=0x" << std::hex << info.VendorId
               << ", device=0x" << info.DeviceId << std::dec
               << ", adapter=\"" << info.AdapterName << "\").";
        const auto text = stream.str();
        m_diagnosticOutput(text.c_str());
    }

    void DeviceImpl::EnableRendering()
    {
        BeginRenderingInitialization();
        (void)CompleteRenderingInitialization();
    }

    void DeviceImpl::DisableRendering()
    {
        std::queue<std::function<void(std::vector<uint8_t>)>> pendingScreenShots{};
        std::shared_ptr<arcana::cancellation_source> cancellationSource{};
        std::shared_future<std::shared_ptr<WgpuNative>> pendingWgpu{};

        {
            std::scoped_lock lock{m_state.Mutex};
            pendingWgpu = m_pendingWgpu;
        }

        if (pendingWgpu.valid())
        {
            try
            {
                (void)pendingWgpu.get();
            }
            catch (...)
            {
            }
        }

        {
            std::scoped_lock lock{m_state.Mutex};

            if (!m_rendering)
            {
                return;
            }

            cancellationSource = m_cancellationSource;

            m_wgpu.reset();
            m_pendingWgpu = {};
            m_cancellationSource.reset();
            m_rendering = false;
            m_startFrameBeforeRenderPending = false;
            m_wgpuInitializationLogged = false;
            m_deviceId++;

            std::scoped_lock screenShotLock{m_screenShotCallbacksMutex};
            pendingScreenShots.swap(m_screenShotCallbacks);
        }

        if (cancellationSource)
        {
            cancellationSource->cancel();
        }

        while (!pendingScreenShots.empty())
        {
            pendingScreenShots.front()({});
            pendingScreenShots.pop();
        }
    }

    SafeTimespanGuarantor& DeviceImpl::GetSafeTimespanGuarantor(const char* updateName)
    {
        std::scoped_lock lock{m_updateSafeTimespansMutex};
        auto [iter, inserted] = m_updateSafeTimespans.try_emplace(updateName, [this]() {
            std::scoped_lock stateLock{m_state.Mutex};
            return m_cancellationSource;
        });
        if (inserted)
        {
            iter->second.Unlock();
        }

        return iter->second;
    }

    void DeviceImpl::SetDiagnosticOutput(std::function<void(const char* output)> diagnosticOutput)
    {
        m_diagnosticOutput = std::move(diagnosticOutput);
    }

    void DeviceImpl::StartRenderingCurrentFrame()
    {
        BeginRenderingInitialization();

        std::shared_ptr<arcana::cancellation_source> cancellationSource{};
        bool tickBeforeRender{};
        {
            std::scoped_lock lock{m_state.Mutex};
            if (!m_rendering)
            {
                return;
            }

            cancellationSource = m_cancellationSource;
            if (m_wgpu)
            {
                tickBeforeRender = true;
            }
            else
            {
                m_startFrameBeforeRenderPending = true;
            }
        }

        if (!cancellationSource || !tickBeforeRender)
        {
            return;
        }

        m_beforeRenderDispatcher.tick(*cancellationSource);
    }

    void DeviceImpl::FinishRenderingCurrentFrame()
    {
        std::shared_ptr<arcana::cancellation_source> cancellationSource{};
        std::shared_future<std::shared_ptr<WgpuNative>> pendingWgpu{};
        std::shared_ptr<WgpuNative> wgpu{};
        size_t renderWidth{};
        size_t renderHeight{};
        bool tickBeforeRender{};

        {
            std::scoped_lock lock{m_state.Mutex};
            if (!m_rendering)
            {
                return;
            }

            wgpu = m_wgpu;
            if (!wgpu)
            {
                pendingWgpu = m_pendingWgpu;
            }
        }

        if (!wgpu)
        {
            if (!pendingWgpu.valid() ||
                pendingWgpu.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
            {
                return;
            }

            wgpu = CompleteRenderingInitialization();
        }

        {
            std::scoped_lock lock{m_state.Mutex};
            cancellationSource = m_cancellationSource;
            if (!wgpu)
            {
                wgpu = m_wgpu;
            }
            if (wgpu)
            {
                renderWidth = CurrentRenderWidth();
                renderHeight = CurrentRenderHeight();
            }

            tickBeforeRender = m_startFrameBeforeRenderPending;
            m_startFrameBeforeRenderPending = false;
        }

        if (!cancellationSource)
        {
            return;
        }

        if (tickBeforeRender)
        {
            m_beforeRenderDispatcher.tick(*cancellationSource);
        }

        std::queue<std::function<void(std::vector<uint8_t>)>> pendingCallbacks{};
        {
            std::scoped_lock lock{m_screenShotCallbacksMutex};
            pendingCallbacks.swap(m_screenShotCallbacks);
        }

        if (wgpu && !pendingCallbacks.empty())
        {
            wgpu->RequestScreenShot();
        }

        if (wgpu)
        {
            wgpu->Render();
        }

        m_afterRenderDispatcher.tick(*cancellationSource);

        if (pendingCallbacks.empty())
        {
            return;
        }

        auto frame = wgpu ? wgpu->CopyScreenShot() : std::vector<uint8_t>{};
        if (frame.empty())
        {
            if (renderWidth == 0 || renderHeight == 0)
            {
                renderWidth = 1;
                renderHeight = 1;
            }

            frame.resize(renderWidth * renderHeight * 4u, 0);
        }

        while (!pendingCallbacks.empty())
        {
            pendingCallbacks.front()(frame);
            pendingCallbacks.pop();
        }
    }

    float DeviceImpl::GetHardwareScalingLevel() const
    {
        std::scoped_lock lock{m_state.Mutex};
        return m_state.Resolution.HardwareScalingLevel;
    }

    void DeviceImpl::SetHardwareScalingLevel(float level)
    {
        std::shared_ptr<WgpuNative> wgpu{};
        uint32_t renderWidth{};
        uint32_t renderHeight{};

        {
            std::scoped_lock lock{m_state.Mutex};

            m_state.Resolution.HardwareScalingLevel = std::max(level, 0.0001f);

            wgpu = m_wgpu;
            if (wgpu)
            {
                renderWidth = CurrentRenderWidth();
                renderHeight = CurrentRenderHeight();
            }
        }

        if (wgpu)
        {
            wgpu->Resize(renderWidth, renderHeight);
        }
    }

    float DeviceImpl::GetDevicePixelRatio() const
    {
        std::scoped_lock lock{m_state.Mutex};
        return m_state.Resolution.DevicePixelRatio;
    }

    PlatformInfo DeviceImpl::GetPlatformInfo() const
    {
        return {};
    }

    uintptr_t DeviceImpl::GetId() const
    {
        return m_deviceId;
    }

    size_t DeviceImpl::GetWidth() const
    {
        std::scoped_lock lock{m_state.Mutex};
        return m_state.Resolution.Width;
    }

    size_t DeviceImpl::GetHeight() const
    {
        std::scoped_lock lock{m_state.Mutex};
        return m_state.Resolution.Height;
    }

    continuation_scheduler<>& DeviceImpl::BeforeRenderScheduler()
    {
        return m_beforeRenderDispatcher.scheduler();
    }

    continuation_scheduler<>& DeviceImpl::AfterRenderScheduler()
    {
        return m_afterRenderDispatcher.scheduler();
    }

    void DeviceImpl::RequestScreenShot(std::function<void(std::vector<uint8_t>)> callback)
    {
        std::scoped_lock lock{m_screenShotCallbacksMutex};
        m_screenShotCallbacks.emplace(std::move(callback));
    }

    float DeviceImpl::GetDevicePixelRatio(WindowT)
    {
        return 1.0f;
    }

    uint32_t DeviceImpl::CurrentRenderWidth() const
    {
        std::scoped_lock lock{m_state.Mutex};
        const auto width = static_cast<float>(m_state.Resolution.Width);
        const auto level = std::max(m_state.Resolution.HardwareScalingLevel, 0.0001f);
        return std::max<uint32_t>(1, static_cast<uint32_t>(std::floor(width / level)));
    }

    uint32_t DeviceImpl::CurrentRenderHeight() const
    {
        std::scoped_lock lock{m_state.Mutex};
        const auto height = static_cast<float>(m_state.Resolution.Height);
        const auto level = std::max(m_state.Resolution.HardwareScalingLevel, 0.0001f);
        return std::max<uint32_t>(1, static_cast<uint32_t>(std::floor(height / level)));
    }
}
