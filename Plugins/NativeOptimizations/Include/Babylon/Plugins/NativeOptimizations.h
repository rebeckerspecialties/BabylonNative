#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

namespace Babylon::Plugins::NativeOptimizations
{
    void BABYLON_API Initialize(Napi::Env env);
    void BABYLON_API InitializeGaussianSplatting(Napi::Env env);
}
