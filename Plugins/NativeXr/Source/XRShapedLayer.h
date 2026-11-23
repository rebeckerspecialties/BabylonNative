#pragma once

#include "JsInstanceOf.h"
#include "XRSession.h"
#include "XRRigidTransform.h"
#include "XRReferenceSpace.h"
#include "AndroidAssetVideo.h"
#include <cmath>

namespace Babylon
{
    class XRShapedLayer final : public Napi::ObjectWrap<XRShapedLayer>
    {
        using Shape = xr::LayerShape;
        using Layout = xr::LayerLayout;
        std::weak_ptr<xr::System::Session> m_session;
        Napi::ObjectReference m_device;
        uint64_t m_id{};
        uint32_t m_width{}, m_height{}, m_usage{};
        Shape m_shape{};
        Layout m_layout{};
        std::string m_format, m_depth;
        bool m_static{}, m_drawn{}, m_media{};

        void Retire()
        {
            if (m_id)
            {
                if (auto s = m_session.lock())
                    s->RetireLayer(m_id);
                m_id = 0;
            }
        }
        void Destroy(const Napi::CallbackInfo&) { Retire(); }
        Napi::Value NeedsRedraw(const Napi::CallbackInfo& info)
        {
            return Napi::Boolean::New(info.Env(), m_id && (!m_static || !m_drawn));
        }
        static float Number(Napi::Object obj, const char* name, float fallback)
        {
            const auto v = obj.Get(name);
            if (v.IsUndefined())
                return fallback;
            const auto f = v.ToNumber().FloatValue();
            if (!std::isfinite(f))
                throw Napi::TypeError::New(obj.Env(), std::string{"Invalid layer "} + name);
            return f;
        }

    public:
        static constexpr std::pair<const char*, Shape> Types[] = {
            {"XRQuadLayer", Shape::Quad}, {"XRCylinderLayer", Shape::Cylinder},
            {"XREquirectLayer", Shape::Equirect}, {"XRCubeLayer", Shape::Cube}};
        static void Initialize(Napi::Env env)
        {
            for (auto [name, shape] : Types)
                SetGlobalClass(env, name, DefineClass(env, name, {InstanceMethod("destroy", &XRShapedLayer::Destroy), InstanceAccessor("needsRedraw", &XRShapedLayer::NeedsRedraw, nullptr)}));
        }
        static XRShapedLayer* From(Napi::Value value)
        {
            if (value.IsObject())
                for (auto [name, shape] : Types)
                    if (IsInstanceOfGlobalClass(value.As<Napi::Object>(), name))
                        return Unwrap(value.As<Napi::Object>());
            return nullptr;
        }
        bool Valid(const Plugins::XRSession& session) const
        {
            const auto platform = m_session.lock();
            return m_id && platform && platform == session.PlatformSession();
        }
        bool UsesDevice(Napi::Object device) const { return !m_device.IsEmpty() && m_device.Value().StrictEquals(device); }
        uint32_t Eyes() const { return m_layout == Layout::Mono ? 1 : 2; }
        uint64_t Id() const { return m_id; }
        bool IsMedia() const { return m_media; }
        XRShapedLayer(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<XRShapedLayer>{info}
        {
            if ((info.Length() != 4 && info.Length() != 5) || !info[0].IsExternal())
                throw Napi::TypeError::New(info.Env(), "Illegal layer constructor");
            m_media = info.Length() == 5;
            m_shape = *info[0].As<Napi::External<Shape>>().Data();
            auto& session = *Plugins::XRSession::Unwrap(info[1].As<Napi::Object>());
            m_session = session.PlatformSession();
            if (!m_media)
                m_device = Napi::Persistent(info[2].As<Napi::Object>());
            auto init = info[3].As<Napi::Object>();
            auto obj = Value();
            const auto space = init.Get("space");
            if (!space.IsObject() || !IsInstanceOfGlobalClass(space.As<Napi::Object>(), "XRReferenceSpace"))
                throw Napi::TypeError::New(info.Env(), "Layer requires an XRReferenceSpace");
            obj.Set("space", space);
            const auto layout = init.Has("layout") ? init.Get("layout").ToString().Utf8Value() : "mono";
            if (layout == "mono")
                m_layout = Layout::Mono;
            else if (layout == "stereo")
                m_layout = Layout::Array;
            else if (m_shape != Shape::Cube && layout == "stereo-left-right")
                m_layout = Layout::LeftRight;
            else if (m_shape != Shape::Cube && layout == "stereo-top-bottom")
                m_layout = Layout::TopBottom;
            else
                throw Napi::TypeError::New(info.Env(), "Invalid composition layer layout");
            obj.Set("layout", layout);
            const auto size = [&](const char* key) {
                const double n = init.Get(key).ToNumber().DoubleValue();
                if (!(n >= 1 && n <= 0x3fffffff && std::floor(n) == n))
                    throw Napi::TypeError::New(info.Env(), key);
                return static_cast<uint32_t>(n);
            };
            m_width = size("viewPixelWidth");
            m_height = size("viewPixelHeight");
            if (m_shape == Shape::Cube && m_width != m_height)
                throw Napi::TypeError::New(info.Env(), "Cube faces must be square");
            m_format = init.Get("colorFormat").ToString().Utf8Value();
            if (init.Has("depthStencilFormat"))
                m_depth = init.Get("depthStencilFormat").ToString().Utf8Value();
            m_usage = init.Has("textureUsage") ? init.Get("textureUsage").As<Napi::Number>().Uint32Value() : 0x10;
            m_static = init.Get("isStatic").ToBoolean().Value();
            obj.Set("isStatic", m_static);
            obj.Set("blendTextureSourceAlpha", true);
            obj.Set("transform", init.Has("transform") ? init.Get("transform") : Napi::Value{XRRigidTransform::New(info.Env())});
            for (const char* key : {"width", "height", "radius", "aspectRatio", "centralAngle", "centralHorizontalAngle", "lowerVerticalAngle", "upperVerticalAngle", "orientation"})
                if (init.Has(key))
                    obj.Set(key, init.Get(key));
            Placement();
            try
            {
                m_id = m_media ? session.PlatformSession()->CreateMediaLayer(m_shape, m_width, m_height, m_layout,
                                     AndroidAssetVideo::Unwrap(info[4].As<Napi::Object>())->Player)
                               : session.PlatformSession()->CreateLayer(m_shape, m_width, m_height, m_layout);
            }
            catch (const std::runtime_error& error)
            {
                throw Napi::Error::New(info.Env(), error.what());
            }
        }
        ~XRShapedLayer() { Retire(); }
        xr::LayerPlacement Placement() const
        {
            auto obj = Value();
            xr::LayerPlacement p;
            p.Id = m_id;
            auto space = obj.Get("space"), transform = obj.Get("transform");
            if (!space.IsObject() || !IsInstanceOfGlobalClass(space.As<Napi::Object>(), "XRReferenceSpace") ||
                !transform.IsObject() || !IsInstanceOfGlobalClass(transform.As<Napi::Object>(), "XRRigidTransform"))
                throw Napi::TypeError::New(obj.Env(), "Invalid layer space or transform");
            auto pose = XRRigidTransform::Unwrap(transform.As<Napi::Object>())->GetNativePose();
            if (m_shape == Shape::Cube && obj.Get("orientation").IsObject())
            {
                const auto q = obj.Get("orientation").As<Napi::Object>();
                pose.Orientation = {Number(q, "x", 0), Number(q, "y", 0), Number(q, "z", 0), Number(q, "w", 1)};
            }
            auto* reference = XRReferenceSpace::Unwrap(space.As<Napi::Object>());
            p.Transform = reference->GetPoseInNativeSpace(pose);
            p.ViewerRelative = reference->IsViewer();
            p.Width = Number(obj, "width", 1);
            p.Height = Number(obj, "height", 1);
            p.Radius = Number(obj, "radius", m_shape == Shape::Cylinder ? 2.f : 0.f);
            p.Aspect = Number(obj, "aspectRatio", 2);
            p.Angle = m_shape == Shape::Cylinder ? Number(obj, "centralAngle", 0.78539816f) : Number(obj, "centralHorizontalAngle", 6.2831853f);
            p.Lower = Number(obj, "lowerVerticalAngle", -1.5707963f);
            p.Upper = Number(obj, "upperVerticalAngle", 1.5707963f);
            p.BlendAlpha = obj.Get("blendTextureSourceAlpha").ToBoolean().Value();
            if (p.Width <= 0 || p.Height <= 0 || p.Radius < 0 || p.Aspect <= 0 || p.Angle <= 0 || p.Angle > 6.283186f || p.Lower >= p.Upper)
                throw Napi::TypeError::New(obj.Env(), "Invalid composition layer geometry");
            return p;
        }
        Napi::Object SubImage(Plugins::XRSession& session, const std::string& eye)
        {
            if ((eye != "none" && eye != "left" && eye != "right") || (Eyes() == 2 && eye == "none"))
                throw Napi::TypeError::New(Env(), "Invalid eye for composition layer layout");
            const uint32_t i = eye == "right" ? 1 : 0, faces = m_shape == Shape::Cube ? 6 : 1;
            auto sub = session.AcquireLayerTexture(m_id, m_width * (m_layout == Layout::LeftRight ? 2 : 1),
                m_height * (m_layout == Layout::TopBottom ? 2 : 1), faces * (m_layout == Layout::Array ? 2 : 1), m_format, m_usage, m_depth);
            sub.ArrayLayer = m_layout == Layout::Array ? i * faces : 0;
            sub.X = m_layout == Layout::LeftRight ? i * m_width : 0;
            sub.Y = m_layout == Layout::TopBottom ? i * m_height : 0;
            sub.ViewWidth = m_width;
            sub.ViewHeight = m_height;
            m_drawn = true;
            return XRGPUSubImage::New(Env(), sub);
        }
    };

    class XRMediaBinding final : public Napi::ObjectWrap<XRMediaBinding>
    {
        Napi::ObjectReference m_session;

    public:
        static void Initialize(Napi::Env env)
        {
            AndroidAssetVideo::Initialize(env);
            SetGlobalClass(env, "XRMediaBinding", DefineClass(env, "XRMediaBinding", {InstanceMethod("createQuadLayer", &XRMediaBinding::Create, napi_default, const_cast<char*>("XRQuadLayer")), InstanceMethod("createCylinderLayer", &XRMediaBinding::Create, napi_default, const_cast<char*>("XRCylinderLayer")), InstanceMethod("createEquirectLayer", &XRMediaBinding::Create, napi_default, const_cast<char*>("XREquirectLayer"))}));
        }
        XRMediaBinding(const Napi::CallbackInfo& i)
            : Napi::ObjectWrap<XRMediaBinding>{i}
        {
            if (!i[0].IsObject() || !IsInstanceOfGlobalClass(i[0].As<Napi::Object>(), "XRSession"))
                throw Napi::TypeError::New(i.Env(), "Expected an XRSession");
            m_session = Napi::Persistent(i[0].As<Napi::Object>());
        }

    private:
        Napi::Value Create(const Napi::CallbackInfo& i)
        {
            auto env = i.Env();
            auto& session = *Plugins::XRSession::Unwrap(m_session.Value());
            auto platform = session.PlatformSession();
            if (!i[0].IsObject() || !IsInstanceOfGlobalClass(i[0].As<Napi::Object>(), "NativeAssetVideo") || !i[1].IsObject())
                throw Napi::TypeError::New(env, "Expected a video and media layer options");
            auto video = i[0].As<Napi::Object>();
            for (const auto& [name, shape] : XRShapedLayer::Types)
            {
                if (std::string_view{name} != static_cast<const char*>(i.Data()))
                    continue;
                if (!platform || !session.IsLayersFeatureEnabled() || !platform->SupportsMediaLayers() || !platform->SupportsLayer(shape))
                {
                    auto error = Napi::Error::New(env, "OpenXR media layer unavailable for this session");
                    error.Value().Set("name", "NotSupportedError");
                    throw error;
                }
                auto object = env.Global().Get("Object").As<Napi::Object>();
                auto init = object.Get("assign").As<Napi::Function>().Call(object, {Napi::Object::New(env), i[1]}).As<Napi::Object>();
                auto layout = init.Get("layout").ToString().Utf8Value();
                uint32_t w = video.Get("videoWidth").ToNumber().Uint32Value(), h = video.Get("videoHeight").ToNumber().Uint32Value();
                init.Set("viewPixelWidth", layout == "stereo-left-right" ? w / 2 : w);
                init.Set("viewPixelHeight", layout == "stereo-top-bottom" ? h / 2 : h);
                return env.Global().Get(name).As<Napi::Function>().New({Napi::External<xr::LayerShape>::New(env, const_cast<xr::LayerShape*>(&shape)), m_session.Value(), env.Null(), init, video});
            }
            throw std::logic_error{"Unknown media layer factory"};
        }
    };
}
