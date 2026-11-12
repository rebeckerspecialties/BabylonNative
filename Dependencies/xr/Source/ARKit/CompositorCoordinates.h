#pragma once

namespace xr
{
    // Metal clip Y points up; WebGPU and Metal texture rows run from top to bottom.
    constexpr float CompositorTextureV(float clipY)
    {
        return (1.0f - clipY) * 0.5f;
    }
}
