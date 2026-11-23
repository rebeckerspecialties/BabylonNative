#pragma once

#include <Babylon/Graphics/ScreenShotData.h>

#include <cstdint>
#include <string>
#if defined(__ANDROID__)
#include <memory>
struct ANativeWindow;
#endif

namespace Babylon::Graphics
{
    struct WgpuBootstrapConfig final
    {
        uint32_t Width{1};
        uint32_t Height{1};
#if defined(__ANDROID__)
        // Vulkan acquires its reference only after asynchronous bootstrap reaches surface creation.
        std::shared_ptr<ANativeWindow> SurfaceLayer;
#else
        void* SurfaceLayer{nullptr};
#endif
        bool PreferLowPower{false};
        bool EnableValidation{false};
        bool Hdr10{false};
    };

    struct WgpuBootstrapInfo final
    {
        uint32_t Backend{};
        uint32_t VendorId{};
        uint32_t DeviceId{};
        std::string AdapterName{};
    };

    class WgpuNative final
    {
    public:
        explicit WgpuNative(const WgpuBootstrapConfig& config);
        ~WgpuNative();

        WgpuNative(const WgpuNative&) = delete;
        WgpuNative& operator=(const WgpuNative&) = delete;

        WgpuNative(WgpuNative&&) noexcept = delete;
        WgpuNative& operator=(WgpuNative&&) noexcept = delete;

        bool IsValid() const;
        bool Resize(uint32_t width, uint32_t height);
        bool Render();
        bool RequestScreenShot();
        ScreenShotData CopyScreenShot() const;
        WgpuBootstrapInfo GetInfo() const;
        const std::string& GetLastError() const;

    private:
        void* m_context{};
        std::string m_lastError{};
    };
}
