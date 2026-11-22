#pragma once

#include <cstdint>
#include <vector>

namespace Babylon::Graphics
{
    struct ScreenShotData final
    {
        std::vector<uint8_t> Pixels{};
        uint32_t Width{};
        uint32_t Height{};
    };
}
