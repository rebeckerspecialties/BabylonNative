#pragma once

#include <XR.h>
#include <napi/napi.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace Babylon::Plugins
{
    inline xr::System::Session::ImageTrackingRequest CreateImageTrackingRequest(const Napi::Object& request)
    {
        const auto env{request.Env()};
        auto image{request.Get("image").As<Napi::Object>()};
        auto data{image.Get("data")};
        if (!data.IsTypedArray())
        {
            const auto getNativeImageData{image.Get("_getNativeImageData")};
            if (!getNativeImageData.IsFunction())
            {
                throw Napi::TypeError::New(env, "Tracked image pixel data is unavailable.");
            }

            const auto snapshot{getNativeImageData.As<Napi::Function>().Call(image, {})};
            if (!snapshot.IsObject())
            {
                throw Napi::TypeError::New(env, "Tracked image pixel data is unavailable.");
            }
            image = snapshot.As<Napi::Object>();
            data = image.Get("data");
        }
        if (!data.IsTypedArray())
        {
            throw Napi::TypeError::New(env, "Tracked image data must be a byte typed array.");
        }

        const auto readDimension = [&image, env](const char* name, bool defaultToOne = false) {
            const auto dimension{image.Get(name)};
            if (defaultToOne && dimension.IsUndefined())
            {
                return uint32_t{1};
            }
            const double value{dimension.ToNumber().DoubleValue()};
            if (!std::isfinite(value) || value <= 0 || std::floor(value) != value || value > std::numeric_limits<int32_t>::max())
            {
                throw Napi::RangeError::New(env, "Tracked image dimensions must be positive integers within the native image limits.");
            }
            return static_cast<uint32_t>(value);
        };
        const auto width{readDimension("width")};
        const auto height{readDimension("height")};
        const auto depth{readDimension("depth", true)};
        const float measuredWidth{request.Get("widthInMeters").ToNumber().FloatValue()};
        if (!std::isfinite(measuredWidth) || measuredWidth < 0)
        {
            throw Napi::RangeError::New(env, "Tracked image widthInMeters must be a finite non-negative number.");
        }

        // Property access above may run getters that detach the pixel buffer.
        const auto buffer{data.As<Napi::TypedArray>()};
        const auto size{buffer.ByteLength()};
        if (buffer.ElementSize() != 1 || size == 0 || size > std::numeric_limits<uint32_t>::max())
        {
            throw Napi::RangeError::New(env, "Tracked image data must be a non-empty byte typed array smaller than 4 GiB.");
        }
        if (depth != 1 || size % height != 0)
        {
            throw Napi::RangeError::New(env, "Tracked image dimensions do not match its pixel data.");
        }

        const auto stride{size / height};
        const auto pixelStride{stride / width};
        if (stride > std::numeric_limits<int32_t>::max() || stride % width != 0 ||
            (pixelStride != 1 && pixelStride != 2 && pixelStride != 3 && pixelStride != 4 && pixelStride != 6 && pixelStride != 8))
        {
            throw Napi::RangeError::New(env, "Tracked image rows do not contain a supported tightly packed pixel layout.");
        }

        auto pixels{std::make_shared<std::vector<uint8_t>>(size)};
        const auto* source{static_cast<const uint8_t*>(buffer.ArrayBuffer().Data()) + buffer.ByteOffset()};
        std::memcpy(pixels->data(), source, size);
        return {std::move(pixels), width, height, depth, static_cast<uint32_t>(stride), measuredWidth};
    }
}
