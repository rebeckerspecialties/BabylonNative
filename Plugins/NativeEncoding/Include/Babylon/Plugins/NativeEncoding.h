#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Babylon::Plugins::NativeEncoding
{
    std::shared_ptr<std::vector<std::byte>> BABYLON_API EncodePng(
        const std::vector<std::byte>& pixelData,
        uint32_t width,
        uint32_t height,
        bool invertY,
        bool premultipliedAlpha);

    void BABYLON_API Initialize(Napi::Env env);
}
