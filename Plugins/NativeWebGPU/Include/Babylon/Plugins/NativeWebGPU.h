#pragma once

#include <Babylon/Api.h>

#include <napi/env.h>
#include <napi/napi.h>

#include <cstdint>

namespace Babylon::Plugins::NativeWebGPU
{
    void BABYLON_API Initialize(Napi::Env env);
    Napi::Object BABYLON_API CreateTextureFromNativeId(
        Napi::Env env,
        uint64_t nativeId,
        const char* label,
        const char* format,
        uint32_t width,
        uint32_t height,
        uint32_t depthOrArrayLayers,
        uint32_t usage);
}
