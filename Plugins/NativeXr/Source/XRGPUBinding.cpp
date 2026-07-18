#include <Babylon/Plugins/NativeXr.h>

#include "Constants.h"
#include "XRGPUBinding.h"
#include "XRGPUSubImage.h"
#include "XRProjectionLayer.h"
#include "XRSession.h"

#include <cmath>
#include <stdexcept>

namespace Babylon
{
    namespace
    {
        constexpr auto JS_CLASS_NAME{"XRGPUBinding"};
        constexpr auto PREFERRED_COLOR_FORMAT{"bgra8unorm"};
        constexpr auto SUPPORTED_DEPTH_STENCIL_FORMAT{"depth24plus-stencil8"};
        constexpr uint32_t GPU_TEXTURE_USAGE_RENDER_ATTACHMENT{0x10};

        Napi::Object RequireXRSession(const Napi::CallbackInfo& info)
        {
            if (info.Length() < 1 || !info[0].IsObject())
            {
                throw std::runtime_error{"XRGPUBinding requires an XRSession and GPUDevice."};
            }

            const auto session = info[0].As<Napi::Object>();
            const auto constructor = info.Env().Global().Get("XRSession");
            if (!constructor.IsFunction() || !session.InstanceOf(constructor.As<Napi::Function>()))
            {
                throw std::runtime_error{"XRGPUBinding first argument must be an XRSession."};
            }
            return session;
        }

        Napi::Object RequireGPUDevice(const Napi::CallbackInfo& info)
        {
            if (info.Length() < 2 || !info[1].IsObject())
            {
                throw std::runtime_error{"XRGPUBinding requires an XRSession and GPUDevice."};
            }

            const auto device = info[1].As<Napi::Object>();
            if (!device.Get("createTexture").IsFunction())
            {
                throw std::runtime_error{"XRGPUBinding second argument must be a GPUDevice."};
            }
            return device;
        }

        Plugins::XRSession& UnwrapXRSession(const Napi::Object& session)
        {
            auto* nativeSession = Plugins::XRSession::Unwrap(session);
            if (nativeSession == nullptr)
            {
                throw std::runtime_error{"XRGPUBinding could not access the supplied XRSession."};
            }
            return *nativeSession;
        }
    }

    void XRGPUBinding::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};
        auto constructor = DefineClass(
            env,
            JS_CLASS_NAME,
            {
                InstanceAccessor("nativeProjectionScaleFactor", &XRGPUBinding::GetNativeProjectionScaleFactor, nullptr),
                InstanceMethod("getPreferredColorFormat", &XRGPUBinding::GetPreferredColorFormat),
                InstanceMethod("createProjectionLayer", &XRGPUBinding::CreateProjectionLayer),
                InstanceMethod("getSubImage", &XRGPUBinding::GetSubImage),
                InstanceMethod("getViewSubImage", &XRGPUBinding::GetViewSubImage),
            });
        env.Global().Set(JS_CLASS_NAME, constructor);
    }

    XRGPUBinding::XRGPUBinding(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<XRGPUBinding>{info}
        , m_sessionReference{Napi::Persistent(RequireXRSession(info))}
        , m_deviceReference{Napi::Persistent(RequireGPUDevice(info))}
        , m_session{UnwrapXRSession(m_sessionReference.Value())}
    {
        if (!m_session.IsWebGPUFeatureEnabled())
        {
            throw std::runtime_error{"XRGPUBinding requires a session created with the WebXR 'webgpu' feature."};
        }
    }

    Napi::Value XRGPUBinding::GetNativeProjectionScaleFactor(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), 1.0);
    }

    Napi::Value XRGPUBinding::GetPreferredColorFormat(const Napi::CallbackInfo& info)
    {
        return Napi::String::New(info.Env(), PREFERRED_COLOR_FORMAT);
    }

    Napi::Value XRGPUBinding::CreateProjectionLayer(const Napi::CallbackInfo& info)
    {
        auto init = info.Length() > 0 && info[0].IsObject() ? info[0].As<Napi::Object>() : Napi::Object::New(info.Env());
        if (!init.Has("colorFormat"))
        {
            init.Set("colorFormat", PREFERRED_COLOR_FORMAT);
        }

        const auto colorFormat = init.Get("colorFormat").As<Napi::String>().Utf8Value();
        if (colorFormat != PREFERRED_COLOR_FORMAT)
        {
            throw std::runtime_error{"NativeXR only supports bgra8unorm WebGPU projection-layer color textures."};
        }

        if (init.Has("depthStencilFormat") && !init.Get("depthStencilFormat").IsUndefined())
        {
            const auto depthStencilFormat = init.Get("depthStencilFormat").As<Napi::String>().Utf8Value();
            if (depthStencilFormat != SUPPORTED_DEPTH_STENCIL_FORMAT)
            {
                throw std::runtime_error{"NativeXR only supports depth24plus-stencil8 WebGPU projection-layer depth textures."};
            }
        }
        else
        {
            init.Set("depthStencilFormat", SUPPORTED_DEPTH_STENCIL_FORMAT);
        }

        const auto textureUsage = init.Has("textureUsage") ? init.Get("textureUsage").As<Napi::Number>().Uint32Value() : GPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
        if ((textureUsage & GPU_TEXTURE_USAGE_RENDER_ATTACHMENT) == 0)
        {
            throw std::runtime_error{"NativeXR WebGPU projection-layer textures require GPUTextureUsage.RENDER_ATTACHMENT."};
        }
        init.Set("textureUsage", Napi::Number::New(info.Env(), textureUsage));

        const auto scaleFactor = init.Has("scaleFactor") ? init.Get("scaleFactor").As<Napi::Number>().DoubleValue() : 1.0;
        if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0)
        {
            throw std::runtime_error{"XRGPUBinding.createProjectionLayer scaleFactor must be a finite positive number."};
        }
        init.Set("scaleFactor", Napi::Number::New(info.Env(), scaleFactor));

        return XRProjectionLayer::New(info.Env(), m_sessionReference.Value(), init);
    }

    Napi::Value XRGPUBinding::GetSubImage(const Napi::CallbackInfo& info)
    {
        if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject())
        {
            throw std::runtime_error{"XRGPUBinding.getSubImage requires an XR layer and current XRFrame."};
        }
        const auto eye = info.Length() > 2 && info[2].IsString() ? info[2].As<Napi::String>().Utf8Value() : std::string{XREye::NONE};
        return GetSubImageForEye(info.Env(), info[0].As<Napi::Object>(), eye);
    }

    Napi::Value XRGPUBinding::GetViewSubImage(const Napi::CallbackInfo& info)
    {
        if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject())
        {
            throw std::runtime_error{"XRGPUBinding.getViewSubImage requires an XRProjectionLayer and XRView."};
        }
        const auto view = info[1].As<Napi::Object>();
        const auto eye = view.Get("eye").As<Napi::String>().Utf8Value();
        return GetSubImageForEye(info.Env(), info[0].As<Napi::Object>(), eye);
    }

    Napi::Object XRGPUBinding::GetSubImageForEye(Napi::Env env, const Napi::Object& layer, const std::string& eye)
    {
        auto* projectionLayer = XRProjectionLayer::Unwrap(layer);
        if (projectionLayer == nullptr || !projectionLayer->BelongsTo(m_session))
        {
            throw std::runtime_error{"XRGPUBinding sub-image requests require a projection layer created by this binding's XRSession."};
        }
        if (!m_session.IsProjectionLayerActive(*projectionLayer))
        {
            throw std::runtime_error{"XRGPUBinding sub-image requests require an active projection layer in XRSession.renderState.layers."};
        }

        const auto viewIndex = eye == XREye::NONE ? 0 : XREye::EyeToIndex(eye);
        const auto subImage = m_session.GetWebGPUSubImage(viewIndex);
        if (!subImage)
        {
            throw std::runtime_error{"XRGPUBinding sub-image textures are only available for a view during its current XR animation frame."};
        }
        return XRGPUSubImage::New(env, *subImage);
    }
}
