#pragma once

#include <napi/napi.h>

namespace Babylon
{
    namespace Plugins
    {
        class XRSession;
    }

    // WebXR-WebGPU XRGPUBinding implementation.
    class XRGPUBinding final : public Napi::ObjectWrap<XRGPUBinding>
    {
    public:
        static void Initialize(Napi::Env env);

        explicit XRGPUBinding(const Napi::CallbackInfo& info);

    private:
        Napi::ObjectReference m_sessionReference{};
        Napi::ObjectReference m_deviceReference{};
        Plugins::XRSession& m_session;

        Napi::Value GetNativeProjectionScaleFactor(const Napi::CallbackInfo& info);
        Napi::Value GetPreferredColorFormat(const Napi::CallbackInfo& info);
        Napi::Value CreateProjectionLayer(const Napi::CallbackInfo& info);
        Napi::Value GetSubImage(const Napi::CallbackInfo& info);
        Napi::Value GetViewSubImage(const Napi::CallbackInfo& info);
        Napi::Object GetSubImageForEye(Napi::Env env, const Napi::Object& layer, const std::string& eye);
    };
}
