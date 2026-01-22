#pragma once

#include <EGL/egl.h>
#include <Babylon/Graphics/RendererMode.h>

namespace Babylon::Graphics
{
    using DeviceT = EGLContext;
    using TextureT = unsigned int;
    using TextureFormatT = unsigned int;

    struct PlatformInfo
    {
        DeviceT Device;
    };
}
