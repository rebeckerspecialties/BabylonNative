#include "Canvas.h"
#include "Context.h"
#include "ImageData.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "nanovg/nanovg.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace Babylon::Polyfills::Internal
{
    static constexpr auto JS_IMAGEDATA_CONSTRUCTOR_NAME = "ImageData";

    void ImageData::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};
        auto constructor = DefineClass(
            env,
            JS_IMAGEDATA_CONSTRUCTOR_NAME,
            {
                InstanceAccessor("width", &ImageData::GetWidth, nullptr),
                InstanceAccessor("height", &ImageData::GetHeight, nullptr),
                InstanceAccessor("data", &ImageData::GetData, nullptr),
            });

        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_IMAGEDATA_CONSTRUCTOR_NAME, constructor);
        if (env.Global().Get(JS_IMAGEDATA_CONSTRUCTOR_NAME).IsUndefined())
        {
            env.Global().Set(JS_IMAGEDATA_CONSTRUCTOR_NAME, constructor);
        }
    }

    Napi::Value ImageData::CreateInstance(Napi::Env env, uint32_t width, uint32_t height, std::vector<uint8_t> pixels)
    {
        Napi::HandleScope scope{env};
        auto data = Napi::Uint8Array::New(env, pixels.size(), napi_uint8_clamped_array);
        if (!pixels.empty())
        {
            std::memcpy(data.Data(), pixels.data(), pixels.size());
        }
        auto constructor = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_IMAGEDATA_CONSTRUCTOR_NAME).As<Napi::Function>();
        return constructor.New({data, Napi::Value::From(env, width), Napi::Value::From(env, height)});
    }

    ImageData::ImageData(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<ImageData>{info}
    {
        const auto readDimension = [&info](size_t index, const char* name) {
            if (info.Length() <= index || !info[index].IsNumber())
            {
                throw Napi::TypeError::New(info.Env(), std::string{"ImageData "} + name + " must be a number.");
            }

            const auto value = info[index].As<Napi::Number>().DoubleValue();
            if (!std::isfinite(value) || value <= 0 || value > std::numeric_limits<uint32_t>::max())
            {
                throw Napi::RangeError::New(info.Env(), std::string{"ImageData "} + name + " is out of range.");
            }
            return static_cast<uint32_t>(value);
        };

        if (info.Length() > 0 && info[0].IsTypedArray())
        {
            auto typedArray = info[0].As<Napi::TypedArray>();
            if (typedArray.TypedArrayType() != napi_uint8_clamped_array)
            {
                throw Napi::TypeError::New(info.Env(), "ImageData pixel data must be a Uint8ClampedArray.");
            }

            m_width = readDimension(1, "width");
            const auto bytesPerRow = static_cast<uint64_t>(m_width) * 4u;
            if (info.Length() > 2 && info[2].IsNumber())
            {
                m_height = readDimension(2, "height");
            }
            else
            {
                if (bytesPerRow == 0 || typedArray.ByteLength() % bytesPerRow != 0)
                {
                    throw Napi::RangeError::New(info.Env(), "ImageData pixel data length is not a multiple of width * 4.");
                }
                m_height = static_cast<uint32_t>(typedArray.ByteLength() / bytesPerRow);
                if (m_height == 0)
                {
                    throw Napi::RangeError::New(info.Env(), "ImageData height is out of range.");
                }
            }

            const auto expectedLength = bytesPerRow * static_cast<uint64_t>(m_height);
            if (expectedLength != typedArray.ByteLength())
            {
                throw Napi::RangeError::New(info.Env(), "ImageData pixel data length does not match its dimensions.");
            }
            m_data = Napi::Persistent(info[0].As<Napi::Uint8Array>());
            return;
        }

        m_width = readDimension(0, "width");
        m_height = readDimension(1, "height");
        const auto elementCount = static_cast<uint64_t>(m_width) * static_cast<uint64_t>(m_height) * 4u;
        if (elementCount > std::numeric_limits<size_t>::max())
        {
            throw Napi::RangeError::New(info.Env(), "ImageData dimensions are too large.");
        }
        m_data = Napi::Persistent(Napi::Uint8Array::New(info.Env(), static_cast<size_t>(elementCount), napi_uint8_clamped_array));
    }

    Napi::Value ImageData::GetWidth(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_width);
    }

    Napi::Value ImageData::GetHeight(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_height);
    }

    Napi::Value ImageData::GetData(const Napi::CallbackInfo& info)
    {
        (void)info;
        return m_data.Value();
    }
}
