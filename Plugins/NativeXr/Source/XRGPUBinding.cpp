#include <Babylon/Plugins/NativeXr.h>

#include "Constants.h"
#include "XRGPUBinding.h"
#include "JsInstanceOf.h"
#include "XRGPUSubImage.h"
#include "XRProjectionLayer.h"
#include "XRSession.h"
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
#include "XRShapedLayer.h"
#endif

#include <cmath>
#include <stdexcept>

namespace Babylon
{
    namespace
    {
        constexpr auto JS_CLASS_NAME{"XRGPUBinding"};
        constexpr auto SUPPORTED_DEPTH_STENCIL_FORMAT{"depth24plus-stencil8"};
        constexpr uint32_t GPU_TEXTURE_USAGE_RENDER_ATTACHMENT{0x10};

        Napi::Value RejectSubImage(Napi::Env env, const char* name, const char* message)
        {
            auto ctor = env.Global().Get("DOMException");
            if (ctor.IsFunction())
            {
                auto error = ctor.As<Napi::Function>().New({Napi::String::New(env, message), Napi::String::New(env, name)});
                napi_throw(env, error);
            }
            else
            {
                auto error = Napi::Error::New(env, message);
                error.Value().Set("name", name);
                error.ThrowAsJavaScriptException();
            }
            return env.Undefined();
        }

        Napi::Object RequireXRSession(const Napi::CallbackInfo& info)
        {
            if (info.Length() < 1 || !info[0].IsObject())
            {
                throw std::runtime_error{"XRGPUBinding requires an XRSession and GPUDevice."};
            }

            const auto session = info[0].As<Napi::Object>();
            if (!IsInstanceOfGlobalClass(session, "XRSession"))
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
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
        XRShapedLayer::Initialize(env);
        XRMediaBinding::Initialize(env);
#endif
        auto constructor = DefineClass(
            env,
            JS_CLASS_NAME,
            {
                InstanceAccessor("nativeProjectionScaleFactor", &XRGPUBinding::GetNativeProjectionScaleFactor, nullptr),
                InstanceAccessor("usesDepthValues", &XRGPUBinding::GetUsesDepthValues, nullptr),
                InstanceMethod("getPreferredColorFormat", &XRGPUBinding::GetPreferredColorFormat),
                InstanceMethod("createProjectionLayer", &XRGPUBinding::CreateProjectionLayer),
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                InstanceMethod("createQuadLayer", &XRGPUBinding::CreateShapedLayer, napi_default, const_cast<char*>("XRQuadLayer")),
                InstanceMethod("createCylinderLayer", &XRGPUBinding::CreateShapedLayer, napi_default, const_cast<char*>("XRCylinderLayer")),
                InstanceMethod("createEquirectLayer", &XRGPUBinding::CreateShapedLayer, napi_default, const_cast<char*>("XREquirectLayer")),
                InstanceMethod("createCubeLayer", &XRGPUBinding::CreateShapedLayer, napi_default, const_cast<char*>("XRCubeLayer")),
#endif
                InstanceMethod("getSubImage", &XRGPUBinding::GetSubImage),
                InstanceMethod("getViewSubImage", &XRGPUBinding::GetViewSubImage),
            });
        SetGlobalClass(env, JS_CLASS_NAME, constructor);
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
        m_preferredColorFormat = m_session.GetWebGPUColorFormat();
    }

    Napi::Value XRGPUBinding::GetNativeProjectionScaleFactor(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), 1.0);
    }

    Napi::Value XRGPUBinding::GetPreferredColorFormat(const Napi::CallbackInfo& info)
    {
        return Napi::String::New(info.Env(), m_preferredColorFormat);
    }

    Napi::Value XRGPUBinding::GetUsesDepthValues(const Napi::CallbackInfo& info)
    {
        // Depth is application-local; neither platform compositor receives a depth swapchain.
        return Napi::Boolean::New(info.Env(), false);
    }

    Napi::Value XRGPUBinding::CreateProjectionLayer(const Napi::CallbackInfo& info)
    {
        auto init = info.Length() > 0 && info[0].IsObject() ? info[0].As<Napi::Object>() : Napi::Object::New(info.Env());
        auto resolved = Napi::Object::New(info.Env());
        const auto requestedFormat = init.Get("colorFormat");
        const auto colorFormat = requestedFormat.IsUndefined() ? std::string{m_preferredColorFormat} : requestedFormat.As<Napi::String>().Utf8Value();
        if (colorFormat != m_preferredColorFormat)
        {
            throw std::runtime_error{std::string{"The XR session requires projection-layer color format "} + m_preferredColorFormat + ", not " + colorFormat + "."};
        }

        resolved.Set("colorFormat", colorFormat);
        const auto requestedDepth = init.Get("depthStencilFormat");
        if (!requestedDepth.IsUndefined() && !requestedDepth.IsNull())
        {
            const auto depthStencilFormat = requestedDepth.As<Napi::String>().Utf8Value();
            if (depthStencilFormat != SUPPORTED_DEPTH_STENCIL_FORMAT && depthStencilFormat != "depth24plus")
            {
                throw std::runtime_error{"Expected depth24plus or depth24plus-stencil8 for the WebGPU projection layer."};
            }
            resolved.Set("depthStencilFormat", depthStencilFormat);
        }

        const auto textureUsage = init.Has("textureUsage") ? init.Get("textureUsage").As<Napi::Number>().Uint32Value() : GPU_TEXTURE_USAGE_RENDER_ATTACHMENT;
        if ((textureUsage & GPU_TEXTURE_USAGE_RENDER_ATTACHMENT) == 0)
        {
            throw std::runtime_error{"NativeXR WebGPU projection-layer textures require GPUTextureUsage.RENDER_ATTACHMENT."};
        }
        resolved.Set("textureUsage", Napi::Number::New(info.Env(), textureUsage));

        const auto scaleFactor = init.Has("scaleFactor") ? init.Get("scaleFactor").As<Napi::Number>().DoubleValue() : 1.0;
        if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0)
        {
            throw std::runtime_error{"XRGPUBinding.createProjectionLayer scaleFactor must be a finite positive number."};
        }
        resolved.Set("scaleFactor", Napi::Number::New(info.Env(), scaleFactor));
        resolved.Set("ignoreDepthValues", Napi::Boolean::New(info.Env(), true));

        return XRProjectionLayer::New(info.Env(), m_sessionReference.Value(), resolved, m_deviceReference.Value());
    }

#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
    Napi::Value XRGPUBinding::CreateShapedLayer(const Napi::CallbackInfo& info)
    {
        auto env = info.Env();
        auto platform = m_session.PlatformSession();
        if (!platform) return RejectSubImage(env,"InvalidStateError","The XRSession has ended.");
        if (!m_session.IsLayersFeatureEnabled()) return RejectSubImage(env,"NotSupportedError","The layers feature is required.");
        if (!info[0].IsObject()) throw Napi::TypeError::New(env,"Expected an XRGPULayerInit dictionary");
        auto init = info[0].As<Napi::Object>();
        if (init.Get("colorFormat").ToString().Utf8Value() != m_preferredColorFormat)
            return RejectSubImage(env,"NotSupportedError","Unsupported XR layer color format");
        const auto depth = init.Get("depthStencilFormat");
        if (!depth.IsUndefined() && depth.ToString().Utf8Value() != "depth24plus" && depth.ToString().Utf8Value() != SUPPORTED_DEPTH_STENCIL_FORMAT)
            return RejectSubImage(env,"NotSupportedError","Unsupported XR layer depth format");
        for (const auto& [name, shape] : XRShapedLayer::Types)
        {
            if (std::string_view{name} != static_cast<const char*>(info.Data())) continue;
            if (!platform->SupportsLayer(shape)) return RejectSubImage(env,"NotSupportedError","The runtime does not expose this OpenXR layer type.");
            return env.Global().Get(name).As<Napi::Function>().New({Napi::External<xr::LayerShape>::New(env,const_cast<xr::LayerShape*>(&shape)),
                m_sessionReference.Value(),m_deviceReference.Value(),init});
        }
        throw std::logic_error{"Unknown composition layer factory"};
    }
#endif
    Napi::Value XRGPUBinding::GetSubImage(const Napi::CallbackInfo& info)
    {
        auto env = info.Env();
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
        if (auto* layer = XRShapedLayer::From(info[0]))
        {
            if (layer->IsMedia()) throw Napi::TypeError::New(env,"A media layer has no WebGPU subimage.");
            if (!info[1].IsObject() || !layer->Valid(m_session) || !layer->UsesDevice(m_deviceReference.Value()) ||
                !m_session.IsCurrentAnimationFrame(info[1].As<Napi::Object>()))
                return RejectSubImage(env,"InvalidStateError","The layer, device and active frame must belong to the same session.");
            if (!m_session.IsCompositionLayerActive(info[0].As<Napi::Object>()))
                throw Napi::TypeError::New(env,"The layer is not in the active render state.");
            return layer->SubImage(m_session, info[2].IsUndefined() ? "none" : info[2].ToString().Utf8Value());
        }
#endif
        if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject() ||
            !IsInstanceOfGlobalClass(info[0].As<Napi::Object>(), "XRProjectionLayer") ||
            !IsInstanceOfGlobalClass(info[1].As<Napi::Object>(), "XRFrame"))
        {
            Napi::TypeError::New(env, "getSubImage requires an XRCompositionLayer and an XRFrame.").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        if (!m_session.IsLayersFeatureEnabled())
        {
            return RejectSubImage(env, "NotSupportedError", "getSubImage requires the layers feature.");
        }
        auto* layer = XRProjectionLayer::Unwrap(info[0].As<Napi::Object>());
        if (!layer || !layer->BelongsTo(m_session) || !layer->UsesDevice(m_deviceReference.Value()) || !m_session.IsCurrentAnimationFrame(info[1].As<Napi::Object>()))
        {
            return RejectSubImage(env, "InvalidStateError", "The layer and active animation frame must belong to this session.");
        }
        Napi::TypeError::New(env, "Use getViewSubImage for an XRProjectionLayer.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Value XRGPUBinding::GetViewSubImage(const Napi::CallbackInfo& info)
    {
        if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject() ||
            !IsInstanceOfGlobalClass(info[0].As<Napi::Object>(), "XRProjectionLayer") ||
            !IsInstanceOfGlobalClass(info[1].As<Napi::Object>(), "XRView"))
        {
            Napi::TypeError::New(info.Env(), "getViewSubImage requires an XRProjectionLayer and XRView.").ThrowAsJavaScriptException();
            return info.Env().Undefined();
        }
        const auto view = info[1].As<Napi::Object>();
        if (!m_session.IsCurrentView(view))
        {
            return RejectSubImage(info.Env(), "InvalidStateError", "The XRView is not from this session's active animation frame.");
        }
        const auto eye = view.Get("eye").As<Napi::String>().Utf8Value();
        return GetSubImageForEye(info.Env(), info[0].As<Napi::Object>(), eye);
    }

    Napi::Object XRGPUBinding::GetSubImageForEye(Napi::Env env, const Napi::Object& layer, const std::string& eye)
    {
        auto* projectionLayer = XRProjectionLayer::Unwrap(layer);
        if (projectionLayer == nullptr || !projectionLayer->BelongsTo(m_session) || !projectionLayer->UsesDevice(m_deviceReference.Value()) || !m_session.IsInAnimationCallback())
        {
            RejectSubImage(env, "InvalidStateError", "The subimage requires an active XR animation callback and the same session and GPUDevice.");
            return {};
        }
        if (!m_session.IsProjectionLayerActive(*projectionLayer))
        {
            Napi::TypeError::New(env, "The projection layer is not active in XRSession.renderState.layers.").ThrowAsJavaScriptException();
            return {};
        }

        const auto viewIndex = eye == XREye::NONE ? 0 : XREye::EyeToIndex(eye);
        const auto subImage = m_session.GetWebGPUSubImage(viewIndex);
        if (!subImage)
        {
            RejectSubImage(env, "InvalidStateError", "The subimage is available only during its XR animation frame.");
            return {};
        }
        return XRGPUSubImage::New(env, *subImage);
    }
}
