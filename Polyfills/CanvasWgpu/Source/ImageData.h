#pragma once

#include <Babylon/Polyfills/Canvas.h>
#include <vector>

namespace Babylon::Polyfills::Internal
{
    class ImageData final : public Napi::ObjectWrap<ImageData>
    {
    public:
        static void Initialize(Napi::Env env);
        static Napi::Value CreateInstance(Napi::Env env, uint32_t width, uint32_t height, std::vector<uint8_t> pixels);

        explicit ImageData(const Napi::CallbackInfo& info);

    private:
        Napi::Value GetWidth(const Napi::CallbackInfo&);
        Napi::Value GetHeight(const Napi::CallbackInfo&);
        Napi::Value GetData(const Napi::CallbackInfo&);

        uint32_t m_width{};
        uint32_t m_height{};
        Napi::Reference<Napi::Uint8Array> m_data{};
    };
}
