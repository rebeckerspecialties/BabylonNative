#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace xr
{
    struct ProjectionExtent
    {
        uint32_t Width;
        uint32_t Height;
    };

    inline ProjectionExtent ScaleProjectionExtent(ProjectionExtent recommended, ProjectionExtent maximum, double requested)
    {
        if (!recommended.Width || !recommended.Height || !maximum.Width || !maximum.Height ||
            !std::isfinite(requested) || requested <= 0.0)
        {
            throw std::invalid_argument{"Invalid XR projection extent or scale"};
        }
        const double scale = std::min({requested, double(maximum.Width) / recommended.Width, double(maximum.Height) / recommended.Height});
        const auto dimension = [scale](uint32_t value, uint32_t limit) {
            return static_cast<uint32_t>(std::clamp(std::floor(value * scale), 1.0, double(limit)));
        };
        return {dimension(recommended.Width, maximum.Width), dimension(recommended.Height, maximum.Height)};
    }
}
