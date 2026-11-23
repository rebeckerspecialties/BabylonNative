#pragma once

#include <openxr/openxr.h>
#include <stdexcept>
#include <string>

namespace xr
{
    template<typename T>
    T MakeXr(XrStructureType type)
    {
        T value{};
        value.type = type;
        return value;
    }

    inline void CheckXr(XrResult result, const char* operation)
    {
        if (XR_FAILED(result))
            throw std::runtime_error{std::string{operation} + " failed with OpenXR result " + std::to_string(result)};
    }
}
