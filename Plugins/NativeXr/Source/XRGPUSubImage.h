#pragma once

#include <napi/napi.h>

#include <cstdint>

namespace Babylon
{
    struct XRGPUSubImageData final
    {
        Napi::Object ColorTexture;
        Napi::Value DepthStencilTexture;
        uint32_t Width{};
        uint32_t Height{};
        uint32_t ArrayLayer{};
        uint32_t X{}, Y{}, ViewWidth{}, ViewHeight{};
        Napi::Value MotionVectorTexture{};
    };

    // WebXR-WebGPU XRGPUSubImage implementation.
    class XRGPUSubImage final
    {
    public:
        static void Initialize(Napi::Env env);
        static Napi::Object New(Napi::Env env, const XRGPUSubImageData& data);
    };
}
