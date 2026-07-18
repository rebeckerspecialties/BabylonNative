#pragma once

#include <napi/napi.h>

#include <cstdint>

namespace Babylon
{
    struct XRGPUSubImageData final
    {
        Napi::Object ColorTexture;
        Napi::Object DepthStencilTexture;
        uint32_t Width{};
        uint32_t Height{};
        uint32_t ArrayLayer{};
    };

    // WebXR-WebGPU XRGPUSubImage implementation.
    class XRGPUSubImage final : public Napi::ObjectWrap<XRGPUSubImage>
    {
    public:
        static void Initialize(Napi::Env env);
        static Napi::Object New(Napi::Env env, const XRGPUSubImageData& data);

        explicit XRGPUSubImage(const Napi::CallbackInfo& info);

    private:
        Napi::ObjectReference m_colorTexture{};
        Napi::ObjectReference m_depthStencilTexture{};
        Napi::ObjectReference m_viewport{};
        uint32_t m_arrayLayer{};

        Napi::Value GetColorTexture(const Napi::CallbackInfo& info);
        Napi::Value GetDepthStencilTexture(const Napi::CallbackInfo& info);
        Napi::Value GetViewport(const Napi::CallbackInfo& info);
        Napi::Value GetViewDescriptor(const Napi::CallbackInfo& info);
    };
}
