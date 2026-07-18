#include <Babylon/Plugins/NativeXr.h>

#include "XRProjectionLayer.h"
#include "XRSession.h"

#include <stdexcept>

namespace Babylon
{
    namespace
    {
        constexpr auto JS_CLASS_NAME{"XRProjectionLayer"};
        constexpr uint32_t GPU_TEXTURE_USAGE_RENDER_ATTACHMENT{0x10};

        Napi::Object RequireXRSession(const Napi::CallbackInfo& info)
        {
            if (info.Length() < 1 || !info[0].IsObject())
            {
                throw std::runtime_error{"XRProjectionLayer requires an XRSession and projection-layer init object."};
            }

            const auto session = info[0].As<Napi::Object>();
            const auto constructor = info.Env().Global().Get("XRSession");
            if (!constructor.IsFunction() || !session.InstanceOf(constructor.As<Napi::Function>()))
            {
                throw std::runtime_error{"XRProjectionLayer first argument must be an XRSession."};
            }
            return session;
        }

        Napi::Object RequireLayerInit(const Napi::CallbackInfo& info)
        {
            if (info.Length() < 2 || !info[1].IsObject())
            {
                throw std::runtime_error{"XRProjectionLayer requires an XRSession and projection-layer init object."};
            }
            return info[1].As<Napi::Object>();
        }

        Plugins::XRSession* UnwrapXRSession(const Napi::Object& session)
        {
            auto* nativeSession = Plugins::XRSession::Unwrap(session);
            if (nativeSession == nullptr)
            {
                throw std::runtime_error{"XRProjectionLayer could not access the supplied XRSession."};
            }
            return nativeSession;
        }
    }

    void XRProjectionLayer::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};
        auto constructor = DefineClass(
            env,
            JS_CLASS_NAME,
            {
                InstanceAccessor("textureWidth", &XRProjectionLayer::GetTextureWidth, nullptr),
                InstanceAccessor("textureHeight", &XRProjectionLayer::GetTextureHeight, nullptr),
                InstanceAccessor("textureArrayLength", &XRProjectionLayer::GetTextureArrayLength, nullptr),
                InstanceAccessor("ignoreDepthValues", &XRProjectionLayer::GetIgnoreDepthValues, nullptr),
                InstanceAccessor("layout", &XRProjectionLayer::GetLayout, nullptr),
            });
        env.Global().Set(JS_CLASS_NAME, constructor);
    }

    Napi::Object XRProjectionLayer::New(Napi::Env env, const Napi::Object& session, const Napi::Object& init)
    {
        return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({session, init});
    }

    XRProjectionLayer::XRProjectionLayer(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<XRProjectionLayer>{info}
        , m_sessionReference{Napi::Weak(RequireXRSession(info))}
    {
        UnwrapXRSession(m_sessionReference.Value());
        const auto init = RequireLayerInit(info);
        m_colorFormat = init.Get("colorFormat").As<Napi::String>().Utf8Value();
        if (init.Has("depthStencilFormat") && init.Get("depthStencilFormat").IsString())
        {
            m_depthStencilFormat = init.Get("depthStencilFormat").As<Napi::String>().Utf8Value();
        }
        if (init.Has("textureUsage") && init.Get("textureUsage").IsNumber())
        {
            m_textureUsage = init.Get("textureUsage").As<Napi::Number>().Uint32Value();
        }
        else
        {
            m_textureUsage = GPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
        }
        if (init.Has("scaleFactor") && init.Get("scaleFactor").IsNumber())
        {
            m_scaleFactor = init.Get("scaleFactor").As<Napi::Number>().FloatValue();
        }
        if (init.Has("ignoreDepthValues") && init.Get("ignoreDepthValues").IsBoolean())
        {
            m_ignoreDepthValues = init.Get("ignoreDepthValues").As<Napi::Boolean>().Value();
        }
    }

    bool XRProjectionLayer::BelongsTo(const Plugins::XRSession& session) const
    {
        if (m_sessionReference.IsEmpty())
        {
            return false;
        }

        const auto sessionObject = m_sessionReference.Value();
        return !sessionObject.IsEmpty() && Plugins::XRSession::Unwrap(sessionObject) == &session;
    }

    const std::string& XRProjectionLayer::ColorFormat() const
    {
        return m_colorFormat;
    }

    const std::string& XRProjectionLayer::DepthStencilFormat() const
    {
        return m_depthStencilFormat;
    }

    uint32_t XRProjectionLayer::TextureUsage() const
    {
        return m_textureUsage;
    }

    Plugins::XRSession& XRProjectionLayer::GetSession() const
    {
        if (m_sessionReference.IsEmpty())
        {
            throw std::runtime_error{"XRProjectionLayer's XRSession is no longer available."};
        }

        const auto sessionObject = m_sessionReference.Value();
        if (sessionObject.IsEmpty())
        {
            throw std::runtime_error{"XRProjectionLayer's XRSession is no longer available."};
        }
        return *UnwrapXRSession(sessionObject);
    }

    Napi::Value XRProjectionLayer::GetTextureWidth(const Napi::CallbackInfo& info)
    {
        const auto width = GetSession().GetWebGPUTextureWidth();
        return Napi::Number::New(info.Env(), width == 0 ? 1 : width);
    }

    Napi::Value XRProjectionLayer::GetTextureHeight(const Napi::CallbackInfo& info)
    {
        const auto height = GetSession().GetWebGPUTextureHeight();
        return Napi::Number::New(info.Env(), height == 0 ? 1 : height);
    }

    Napi::Value XRProjectionLayer::GetTextureArrayLength(const Napi::CallbackInfo& info)
    {
        const auto layers = GetSession().GetWebGPUTextureArrayLength();
        return Napi::Number::New(info.Env(), layers == 0 ? 1 : layers);
    }

    Napi::Value XRProjectionLayer::GetIgnoreDepthValues(const Napi::CallbackInfo& info)
    {
        return Napi::Boolean::New(info.Env(), m_ignoreDepthValues);
    }

    Napi::Value XRProjectionLayer::GetLayout(const Napi::CallbackInfo& info)
    {
        return Napi::String::New(info.Env(), "default");
    }
}
