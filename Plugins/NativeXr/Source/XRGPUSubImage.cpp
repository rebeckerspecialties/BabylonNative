#include "XRGPUSubImage.h"

#include <stdexcept>
#include <string>

namespace Babylon
{
    namespace
    {
        constexpr auto JS_CLASS_NAME{"XRGPUSubImage"};

        Napi::Object RequireGPUTexture(const Napi::CallbackInfo& info, size_t index, const char* name)
        {
            if (info.Length() <= index || !info[index].IsObject())
            {
                throw std::runtime_error{std::string{"XRGPUSubImage requires a GPUTexture for "} + name + "."};
            }

            const auto texture = info[index].As<Napi::Object>();
            if (!texture.Get("createView").IsFunction())
            {
                throw std::runtime_error{std::string{"XRGPUSubImage "} + name + " must be a GPUTexture."};
            }
            return texture;
        }

        Napi::Number RequireNumber(const Napi::CallbackInfo& info, size_t index, const char* name)
        {
            if (info.Length() <= index || !info[index].IsNumber())
            {
                throw std::runtime_error{std::string{"XRGPUSubImage "} + name + " must be a number."};
            }
            return info[index].As<Napi::Number>();
        }
    }

    void XRGPUSubImage::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};
        auto constructor = DefineClass(
            env,
            JS_CLASS_NAME,
            {
                InstanceAccessor("colorTexture", &XRGPUSubImage::GetColorTexture, nullptr),
                InstanceAccessor("depthStencilTexture", &XRGPUSubImage::GetDepthStencilTexture, nullptr),
                InstanceAccessor("colorTextureWidth", &XRGPUSubImage::GetColorTextureWidth, nullptr),
                InstanceAccessor("colorTextureHeight", &XRGPUSubImage::GetColorTextureHeight, nullptr),
                InstanceAccessor("depthStencilTextureWidth", &XRGPUSubImage::GetDepthStencilTextureWidth, nullptr),
                InstanceAccessor("depthStencilTextureHeight", &XRGPUSubImage::GetDepthStencilTextureHeight, nullptr),
                InstanceAccessor("viewport", &XRGPUSubImage::GetViewport, nullptr),
                InstanceMethod("getViewDescriptor", &XRGPUSubImage::GetViewDescriptor),
            });
        env.Global().Set(JS_CLASS_NAME, constructor);
    }

    Napi::Object XRGPUSubImage::New(Napi::Env env, const XRGPUSubImageData& data)
    {
        return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({
            data.ColorTexture,
            data.DepthStencilTexture,
            Napi::Number::New(env, data.Width),
            Napi::Number::New(env, data.Height),
            Napi::Number::New(env, data.ArrayLayer),
        });
    }

    XRGPUSubImage::XRGPUSubImage(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<XRGPUSubImage>{info}
        , m_colorTexture{Napi::Persistent(RequireGPUTexture(info, 0, "colorTexture"))}
        , m_depthStencilTexture{Napi::Persistent(RequireGPUTexture(info, 1, "depthStencilTexture"))}
        , m_viewport{Napi::Persistent(Napi::Object::New(info.Env()))}
        , m_width{RequireNumber(info, 2, "width").Uint32Value()}
        , m_height{RequireNumber(info, 3, "height").Uint32Value()}
        , m_arrayLayer{RequireNumber(info, 4, "array layer").Uint32Value()}
    {
        m_viewport.Set("x", Napi::Number::New(info.Env(), 0));
        m_viewport.Set("y", Napi::Number::New(info.Env(), 0));
        m_viewport.Set("width", Napi::Number::New(info.Env(), m_width));
        m_viewport.Set("height", Napi::Number::New(info.Env(), m_height));
    }

    Napi::Value XRGPUSubImage::GetColorTexture(const Napi::CallbackInfo&)
    {
        return m_colorTexture.Value();
    }

    Napi::Value XRGPUSubImage::GetDepthStencilTexture(const Napi::CallbackInfo&)
    {
        return m_depthStencilTexture.Value();
    }

    Napi::Value XRGPUSubImage::GetColorTextureWidth(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_width);
    }

    Napi::Value XRGPUSubImage::GetColorTextureHeight(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_height);
    }

    Napi::Value XRGPUSubImage::GetDepthStencilTextureWidth(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_width);
    }

    Napi::Value XRGPUSubImage::GetDepthStencilTextureHeight(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_height);
    }

    Napi::Value XRGPUSubImage::GetViewport(const Napi::CallbackInfo&)
    {
        return m_viewport.Value();
    }

    Napi::Value XRGPUSubImage::GetViewDescriptor(const Napi::CallbackInfo& info)
    {
        auto descriptor = Napi::Object::New(info.Env());
        descriptor.Set("dimension", "2d");
        descriptor.Set("baseMipLevel", Napi::Number::New(info.Env(), 0));
        descriptor.Set("mipLevelCount", Napi::Number::New(info.Env(), 1));
        descriptor.Set("baseArrayLayer", Napi::Number::New(info.Env(), m_arrayLayer));
        descriptor.Set("arrayLayerCount", Napi::Number::New(info.Env(), 1));
        descriptor.Set("aspect", "all");
        return descriptor;
    }
}
