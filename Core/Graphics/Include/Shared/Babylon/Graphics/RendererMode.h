#pragma once

namespace Babylon::Graphics
{
    // Runtime hint to select a renderer when multiple backends are available.
    enum class RendererMode
    {
        Automatic,
        ForceVulkan,
        ForceOpenGLES,
    };
}
