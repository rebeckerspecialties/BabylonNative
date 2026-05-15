#include <map>
#include <algorithm>
#include <cassert>
#include <regex>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100) // unreferenced formal parameter (napi callback signatures)
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

// Compatibility ABI header: these nvg* symbols are implemented by CanvasWgpu
// Rust/femtovg exports, not by upstream NanoVG.
#include "nanovg/nanovg.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "Canvas.h"
#include "Context.h"
#include "MeasureText.h"
#include "Image.h"
#include "ImageData.h"
#include "Path2D.h"
#include "Colors.h"
#include "LineCaps.h"
#include "Gradient.h"

namespace Babylon::Polyfills::Internal
{
    static constexpr auto JS_CONTEXT_CONSTRUCTOR_NAME = "Context";

    void Context::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};

        Napi::Function func = DefineClass(
            env,
            JS_CONTEXT_CONSTRUCTOR_NAME,
            {
                InstanceMethod("clearRect", &Context::ClearRect),
                InstanceMethod("save", &Context::Save),
                InstanceMethod("restore", &Context::Restore),
                InstanceMethod("fillRect", &Context::FillRect),
                InstanceMethod("scale", &Context::Scale),
                InstanceMethod("rotate", &Context::Rotate),
                InstanceMethod("translate", &Context::Translate),
                InstanceMethod("strokeRect", &Context::StrokeRect),
                InstanceMethod("rect", &Context::Rect),
                InstanceMethod("roundRect", &Context::RoundRect),
                InstanceMethod("clip", &Context::Clip),
                InstanceMethod("putImageData", &Context::PutImageData),
                InstanceMethod("arc", &Context::Arc),
                InstanceMethod("beginPath", &Context::BeginPath),
                InstanceMethod("closePath", &Context::ClosePath),
                InstanceMethod("moveTo", &Context::MoveTo),
                InstanceMethod("lineTo", &Context::LineTo),
                InstanceMethod("quadraticCurveTo", &Context::QuadraticCurveTo),
                InstanceMethod("measureText", &Context::MeasureText),
                InstanceMethod("stroke", &Context::Stroke),
                InstanceMethod("fill", &Context::Fill),
                InstanceMethod("drawImage", &Context::DrawImage),
                InstanceMethod("getImageData", &Context::GetImageData),
                InstanceMethod("setLineDash", &Context::SetLineDash),
                InstanceMethod("fillText", &Context::FillText),
                InstanceMethod("strokeText", &Context::StrokeText),
                InstanceMethod("createLinearGradient", &Context::CreateLinearGradient),
                InstanceMethod("createRadialGradient", &Context::CreateRadialGradient),
                InstanceMethod("getTransform", &Context::GetTransform),
                InstanceMethod("setTransform", &Context::SetTransform),
                InstanceMethod("transform", &Context::Transform),
                InstanceMethod("dispose", &Context::Dispose),
                InstanceMethod("destroy", &Context::Dispose),
                InstanceMethod("flush", &Context::Flush),
                InstanceAccessor("lineCap", &Context::GetLineCap, &Context::SetLineCap),
                InstanceAccessor("lineJoin", &Context::GetLineJoin, &Context::SetLineJoin),
                InstanceAccessor("miterLimit", &Context::GetMiterLimit, &Context::SetMiterLimit),
                InstanceAccessor("filter", &Context::GetFilter, &Context::SetFilter),
                InstanceAccessor("direction", &Context::GetDirection, &Context::SetDirection),
                InstanceAccessor("font", &Context::GetFont, &Context::SetFont),
                InstanceAccessor("letterSpacing", &Context::GetLetterSpacing, &Context::SetLetterSpacing),
                InstanceAccessor("strokeStyle", &Context::GetStrokeStyle, &Context::SetStrokeStyle),
                InstanceAccessor("fillStyle", &Context::GetFillStyle, &Context::SetFillStyle),
                InstanceAccessor("globalAlpha", nullptr, &Context::SetGlobalAlpha),
                InstanceAccessor("shadowColor", &Context::GetShadowColor, &Context::SetShadowColor),
                InstanceAccessor("shadowBlur", &Context::GetShadowBlur, &Context::SetShadowBlur),
                InstanceAccessor("shadowOffsetX", &Context::GetShadowOffsetX, &Context::SetShadowOffsetX),
                InstanceAccessor("shadowOffsetY", &Context::GetShadowOffsetY, &Context::SetShadowOffsetY),
                InstanceAccessor("lineWidth", &Context::GetLineWidth, &Context::SetLineWidth),
            });
        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_CONTEXT_CONSTRUCTOR_NAME, func);
    }

    Napi::Value Context::CreateInstance(Napi::Env env, Napi::Value canvas)
    {
        auto func = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_CONTEXT_CONSTRUCTOR_NAME).As<Napi::Function>();
        return func.New({canvas});
    }

    Context::Context(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<Context>{info}
        , Polyfills::Canvas::Impl::MonitoredResource{Polyfills::Canvas::Impl::GetFromJavaScript(info.Env())}
        , m_canvas{NativeCanvas::Unwrap(info[0].As<Napi::Object>())}
        , m_nvg{std::make_shared<NVGcontext*>(nvgCreate(1))}
        , m_cancellationSource{std::make_shared<arcana::cancellation_source>()}
        , m_runtimeScheduler{Babylon::JsRuntime::GetFromJavaScript(info.Env())}
    {
        // TODO: commented code doesn't compile with napi-jsi. Using non read-only property for now
        //info.This().ToObject().DefineProperty(Napi::PropertyDescriptor::Value("canvas", info[0], napi_enumerable));
        info.This().ToObject().Set("canvas", info[0]);

        for (auto& font : NativeCanvas::fontsInfos)
        {
            // TODO: update nvgCreateFontMem safely when old font buffer invalidated
            m_fonts[font.first] = nvgCreateFontMem(*m_nvg, font.first.c_str(), font.second.data(), static_cast<int>(font.second.size()), 0);
        }
    }

    Context::~Context()
    {
        Dispose();
        m_cancellationSource->cancel();
    }

    void* Context::GetNativeRenderTexture() const
    {
        return const_cast<void*>(nvgGetRenderTexture(*m_nvg));
    }

    void Context::Dispose(const Napi::CallbackInfo&)
    {
        Dispose();
    }

    void Context::FlushGraphicResources()
    {
        Dispose();
    }

    void Context::Dispose()
    {
        if (m_nvg)
        {
            if (m_frameActive)
            {
                nvgEndFrame(*m_nvg);
                m_frameActive = false;
            }

            for (auto& image : m_nvgImageIndices)
            {
                nvgDeleteImage(*m_nvg, image.second);
            }
            for (auto& image : m_canvasTextureImageIndices)
            {
                nvgDeleteImage(*m_nvg, image.second.imageIndex);
            }
            nvgDelete(*m_nvg);
            m_nvg = nullptr;
        }

        m_nvgImageIndices.clear();
        m_canvasTextureImageIndices.clear();
        m_fonts.clear();
        m_currentFontId = -1;
        m_frameActive = false;
        m_isClipped = false;
    }

    void Context::EnsureFrame()
    {
        if (m_frameActive || !m_nvg)
        {
            return;
        }

        const bool needClear = m_canvas->UpdateRenderTarget();
        const auto width = m_canvas->GetWidth();
        const auto height = m_canvas->GetHeight();

        nvgBeginFrame(*m_nvg, float(width), float(height), 1.0f);
        m_frameActive = true;

        if (needClear)
        {
            nvgSave(*m_nvg);
            nvgGlobalCompositeOperation(*m_nvg, NVG_COPY);
            nvgBeginPath(*m_nvg);
            nvgRect(*m_nvg, 0.f, 0.f, static_cast<float>(width), static_cast<float>(height));
            nvgFillColor(*m_nvg, TRANSPARENT_BLACK);
            nvgFill(*m_nvg);
            nvgRestore(*m_nvg);
        }
    }

    void Context::BindFillStyle(const Napi::CallbackInfo& info, float left, float, float width, float height)
    {
        EnsureFrame();

        if (std::holds_alternative<std::string>(m_fillStyle))
        {
            const auto color = StringToColor(info.Env(), std::get<std::string>(m_fillStyle));
            nvgFillColor(*m_nvg, color);
        }
        else if (std::holds_alternative<CanvasGradient*>(m_fillStyle))
        {
            CanvasGradient* gradient = std::get<CanvasGradient*>(m_fillStyle);
            gradient->UpdateCache();
            // TODO: replace left/lop/width/height by context bounds
            NVGpaint imagePaint = nvgImagePattern(*m_nvg, 0.f, 0.f, width + left, height, 0.f, gradient->CachedImage(), 1.f);
            nvgFillPaint(*m_nvg, imagePaint);
        }
        else
        {
            throw Napi::Error::New(info.Env(), "Fillstyle is not a color string or a gradient.");
        }
    }

    void Context::BindStrokeStyle(const Napi::CallbackInfo& info, float left, float, float width, float height)
    {
        EnsureFrame();

        if (std::holds_alternative<std::string>(m_strokeStyle))
        {
            const auto color = StringToColor(info.Env(), std::get<std::string>(m_strokeStyle));
            nvgStrokeColor(*m_nvg, color);
        }
        else if (std::holds_alternative<CanvasGradient*>(m_strokeStyle))
        {
            CanvasGradient* gradient = std::get<CanvasGradient*>(m_strokeStyle);
            gradient->UpdateCache();
            NVGpaint imagePaint = nvgImagePattern(*m_nvg, 0.f, 0.f, width + left, height, 0.f, gradient->CachedImage(), 1.f);
            nvgStrokePaint(*m_nvg, imagePaint);
        }
        else
        {
            throw Napi::Error::New(info.Env(), "Strokestyle is not a color string or a gradient.");
        }
    }

    void Context::SetFilterStack()
    {
        EnsureFrame();

        nanovg_filterstack filterStack;
        filterStack.ParseString(m_filter);
        nvgSetFilterBlur(*m_nvg, filterStack.BlurSigma());
    }

    void Context::FillRect(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        auto left = info[0].As<Napi::Number>().FloatValue();
        auto top = info[1].As<Napi::Number>().FloatValue();
        auto width = info[2].As<Napi::Number>().FloatValue();
        auto height = info[3].As<Napi::Number>().FloatValue();

        if (!m_isClipped)
        {
            nvgBeginPath(*m_nvg);
        }

        nvgRect(*m_nvg, left, top, width, height);

        BindFillStyle(info, left, top, width, height);

        SetFilterStack();
        nvgFill(*m_nvg);
    }

    Napi::Value Context::GetFillStyle(const Napi::CallbackInfo&)
    {
        if (std::holds_alternative<std::string>(m_fillStyle))
        {
            return Napi::Value::From(Env(), std::get<std::string>(m_fillStyle));
        }
        else
        {
            return Napi::External<CanvasGradient>::New(Env(), std::get<CanvasGradient*>(m_fillStyle));
        }
    }

    void Context::SetFillStyle(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        if (value.IsString())
        {
            EnsureFrame();

            auto string = value.As<Napi::String>().Utf8Value();
            const auto color = StringToColor(info.Env(), string);
            m_fillStyle = std::move(string);
            nvgFillColor(*m_nvg, color);
        }
        else
        {
            CanvasGradient* canvasGradient = CanvasGradient::Unwrap(info[0].As<Napi::Object>());
            m_fillStyle = canvasGradient;
        }
    }

    Napi::Value Context::GetStrokeStyle(const Napi::CallbackInfo&)
    {
        if (std::holds_alternative<std::string>(m_strokeStyle))
        {
            return Napi::Value::From(Env(), std::get<std::string>(m_strokeStyle));
        }
        else
        {
            return Napi::External<CanvasGradient>::New(Env(), std::get<CanvasGradient*>(m_strokeStyle));
        }
    }

    void Context::SetStrokeStyle(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        if (value.IsString())
        {
            EnsureFrame();

            auto string = value.As<Napi::String>().Utf8Value();
            const auto color = StringToColor(info.Env(), string);
            m_strokeStyle = std::move(string);
            nvgStrokeColor(*m_nvg, color);
        }
        else if (value.IsObject())
        {
            CanvasGradient* canvasGradient = CanvasGradient::Unwrap(value.As<Napi::Object>());
            m_strokeStyle = canvasGradient;
        }
        else
        {
            throw Napi::TypeError::New(info.Env(), "Context2D.strokeStyle must be a color string or CanvasGradient.");
        }
    }

    Napi::Value Context::GetLineWidth(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_lineWidth);
    }

    void Context::SetLineWidth(const Napi::CallbackInfo&, const Napi::Value& value)
    {
        EnsureFrame();

        m_lineWidth = value.As<Napi::Number>().FloatValue();
        nvgStrokeWidth(*m_nvg, m_lineWidth);
    }

    void Context::Fill(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        SetFilterStack();

        const NativeCanvasPath2D* path = info.Length() >= 1 && info[0].IsObject()
            ? NativeCanvasPath2D::Unwrap(info[0].As<Napi::Object>())
            : nullptr;
        // TODO: handle fillRule: nonzero, evenodd

        // draw Path2D if exists
        if (path != nullptr)
        {
            PlayPath2D(path);
        }

        nvgFill(*m_nvg);
    }

    void Context::Save(const Napi::CallbackInfo&)
    {
        EnsureFrame();

        nvgSave(*m_nvg);
    }

    void Context::Restore(const Napi::CallbackInfo&)
    {
        EnsureFrame();

        nvgRestore(*m_nvg);
        m_isClipped = false;
    }

    void Context::ClearRect(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const float x = info[0].As<Napi::Number>().FloatValue();
        const float y = info[1].As<Napi::Number>().FloatValue();
        const float width = info[2].As<Napi::Number>().FloatValue();
        const float height = info[3].As<Napi::Number>().FloatValue();

        nvgSave(*m_nvg);
        nvgGlobalCompositeOperation(*m_nvg, NVG_COPY);

        if (!m_isClipped)
        {
            nvgBeginPath(*m_nvg);
        }

        nvgRect(*m_nvg, x, y, width, height);

        if (!m_isClipped)
        {
            nvgClosePath(*m_nvg);
        }

        nvgFillColor(*m_nvg, TRANSPARENT_BLACK);
        nvgFill(*m_nvg);
        nvgRestore(*m_nvg);
    }

    void Context::Translate(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();
        nvgTranslate(*m_nvg, x, y);
    }

    void Context::Rotate(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto angle = info[0].As<Napi::Number>().FloatValue();
        nvgRotate(*m_nvg, angle);
    }

    void Context::Scale(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();
        nvgScale(*m_nvg, x, y);
    }

    void Context::BeginPath(const Napi::CallbackInfo&)
    {
        EnsureFrame();

        nvgBeginPath(*m_nvg);
    }

    void Context::ClosePath(const Napi::CallbackInfo&)
    {
        EnsureFrame();

        nvgClosePath(*m_nvg);
    }

    void Context::Rect(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto left = info[0].As<Napi::Number>().FloatValue();
        const auto top = info[1].As<Napi::Number>().FloatValue();
        const auto width = info[2].As<Napi::Number>().FloatValue();
        const auto height = info[3].As<Napi::Number>().FloatValue();

        nvgRect(*m_nvg, left, top, width, height);
        m_rectangleClipping = {left, top, width, height};
    }

    void Context::RoundRect(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();
        const auto width = info[2].As<Napi::Number>().FloatValue();
        const auto height = info[3].As<Napi::Number>().FloatValue();
        const auto radii = info[4];

        if (radii.IsNumber())
        {
            const auto radius = radii.As<Napi::Number>().FloatValue();
            nvgRoundedRect(*m_nvg, x, y, width, height, radius);
        }
        else if (radii.IsArray())
        {
            const auto radiiArray = radii.As<Napi::Array>();
            const auto radiiArrayLength = radiiArray.Length();
            if (radiiArrayLength == 1)
            {
                const auto radius = radiiArray[0u].As<Napi::Number>().FloatValue();
                nvgRoundedRect(*m_nvg, x, y, width, height, radius);
            }
            else if (radiiArrayLength == 2)
            {
                const auto topLeftBottomRight = radiiArray[0u].As<Napi::Number>().FloatValue();
                const auto topRightBottomLeft = radiiArray[1u].As<Napi::Number>().FloatValue();

                nvgRoundedRectVarying(*m_nvg, x, y, width, height, topLeftBottomRight, topRightBottomLeft, topLeftBottomRight, topRightBottomLeft);
            }
            else if (radiiArrayLength == 3)
            {
                const auto topLeft = radiiArray[0u].As<Napi::Number>().FloatValue();
                const auto topRightBottomLeft = radiiArray[1u].As<Napi::Number>().FloatValue();
                const auto bottomRight = radiiArray[2u].As<Napi::Number>().FloatValue();

                nvgRoundedRectVarying(*m_nvg, x, y, width, height, topLeft, topRightBottomLeft, bottomRight, topRightBottomLeft);
            }
            else if (radiiArrayLength == 4)
            {
                const auto topLeft = radiiArray[0u].As<Napi::Number>().FloatValue();
                const auto topRight = radiiArray[1u].As<Napi::Number>().FloatValue();
                const auto bottomRight = radiiArray[2u].As<Napi::Number>().FloatValue();
                const auto bottomLeft = radiiArray[3u].As<Napi::Number>().FloatValue();

                nvgRoundedRectVarying(*m_nvg, x, y, width, height, topLeft, topRight, bottomRight, bottomLeft);
            }
            else
            {
                throw Napi::Error::New(info.Env(), "Invalid number of parameters for radii");
            }
        }
        // DOMPoint
        // TODO: move duplicate Path2D & Context args parsing into a utils.cpp
        else if (radii.IsObject())
        {
            const auto dompoint = radii.As<Napi::Object>();
            const auto dpx = dompoint.Get("x").As<Napi::Number>().FloatValue();
            const auto dpy = dompoint.Get("y").As<Napi::Number>().FloatValue();
            nvgRoundedRectElliptic(*m_nvg, x, y, width, height, dpx, dpy, dpx, dpy, dpx, dpy, dpx, dpy);
        }
        else
        {
            throw Napi::Error::New(info.Env(), "Invalid radii parameter");
        }

        m_rectangleClipping = {x, y, width, height};
    }

    void Context::Clip(const Napi::CallbackInfo& /*info*/)
    {
        EnsureFrame();

        m_isClipped = true;

        //By default m_rectangleClipping is not set, in this case we use the canvas width and height.
        auto w = m_rectangleClipping.width != 0 ? m_rectangleClipping.width : m_canvas->GetWidth();
        auto h = m_rectangleClipping.height != 0 ? m_rectangleClipping.height : m_canvas->GetHeight();

        // expand clipping 1pix in each direction because nanovg AA gets cut a bit short.
        nvgScissor(*m_nvg, m_rectangleClipping.left - 1, m_rectangleClipping.top - 1, w + 1, h + 1);
    }

    void Context::StrokeRect(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto left = info[0].As<Napi::Number>().FloatValue();
        const auto top = info[1].As<Napi::Number>().FloatValue();
        const auto width = info[2].As<Napi::Number>().FloatValue();
        const auto height = info[3].As<Napi::Number>().FloatValue();

        if (!m_isClipped)
        {
            nvgBeginPath(*m_nvg);
        }

        nvgRect(*m_nvg, left, top, width, height);
        BindStrokeStyle(info, left, top, width, height);
        SetFilterStack();
        nvgStroke(*m_nvg);
    }

    void Context::PlayPath2D(const NativeCanvasPath2D* path)
    {
        EnsureFrame();

        nvgBeginPath(*m_nvg);
        for (const auto& command : *path)
        {
            const auto args = command.args;
            switch (command.type)
            {
                case P2D_CLOSE:
                    nvgClosePath(*m_nvg);
                    break;
                case P2D_MOVETO:
                    nvgMoveTo(*m_nvg, args.moveTo.x, args.moveTo.y);
                    break;
                case P2D_LINETO:
                    nvgLineTo(*m_nvg, args.lineTo.x, args.lineTo.y);
                    break;
                case P2D_BEZIERTO:
                    nvgBezierTo(*m_nvg, args.bezierTo.cp1x, args.bezierTo.cp1y,
                        args.bezierTo.cp2x, args.bezierTo.cp2y,
                        args.bezierTo.x, args.bezierTo.y);
                    break;
                case P2D_QUADTO:
                    nvgQuadTo(*m_nvg, args.quadTo.cpx, args.quadTo.cpy,
                        args.quadTo.x, args.quadTo.y);
                    break;
                case P2D_ARC:
                    nvgArc(*m_nvg, args.arc.x, args.arc.y, args.arc.radius,
                        args.arc.startAngle, args.arc.endAngle,
                        args.arc.counterclockwise ? NVG_CCW : NVG_CW);
                    break;
                case P2D_ARCTO:
                    nvgArcTo(*m_nvg, args.arcTo.x1, args.arcTo.y1,
                        args.arcTo.x2, args.arcTo.y2,
                        args.arcTo.radius);
                    break;
                case P2D_ELLIPSE:
                    // TODO: handle clockwise for nvgElipse (args.ellipse.counterclockwise)
                    nvgEllipse(*m_nvg, args.ellipse.x, args.ellipse.y,
                        args.ellipse.radiusX, args.ellipse.radiusY);
                    break;
                case P2D_RECT:
                    nvgRect(*m_nvg, args.rect.x, args.rect.y,
                        args.rect.width, args.rect.height);
                    break;
                case P2D_ROUNDRECT:
                    nvgRoundedRect(*m_nvg, args.roundRect.x, args.roundRect.y,
                        args.roundRect.width, args.roundRect.height,
                        args.roundRect.radii);
                    break;
                case P2D_ROUNDRECTVARYING:
                    nvgRoundedRectVarying(*m_nvg, args.roundRectVarying.x, args.roundRectVarying.y,
                        args.roundRectVarying.width, args.roundRectVarying.height,
                        args.roundRectVarying.topLeft, args.roundRectVarying.topRight,
                        args.roundRectVarying.bottomRight, args.roundRectVarying.bottomLeft);
                    break;
                case P2D_ROUNDRECTELLIPTIC:
                    nvgRoundedRectElliptic(*m_nvg, args.roundRectElliptic.x, args.roundRectElliptic.y,
                        args.roundRectElliptic.width, args.roundRectElliptic.height,
                        args.roundRectElliptic.topLeftX, args.roundRectElliptic.topLeftY,
                        args.roundRectElliptic.topRightX, args.roundRectElliptic.topRightY,
                        args.roundRectElliptic.bottomRightX, args.roundRectElliptic.bottomRightY,
                        args.roundRectElliptic.bottomLeftX, args.roundRectElliptic.bottomLeftY);
                    break;
                case P2D_TRANSFORM:
                    nvgTransform(*m_nvg,
                        args.transform.a, args.transform.b, args.transform.c,
                        args.transform.d, args.transform.e, args.transform.f);
                    break;
                default:
                    break;
            }
        }
    }

    void Context::Stroke(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        // draw Path2D if exists
        const NativeCanvasPath2D* path = info.Length() == 1 ? NativeCanvasPath2D::Unwrap(info[0].As<Napi::Object>()) : nullptr;
        if (path != nullptr)
        {
            PlayPath2D(path);
        }

        BindStrokeStyle(info, 0.f, 0.f, static_cast<float>(m_canvas->GetWidth()), static_cast<float>(m_canvas->GetHeight()));
        SetFilterStack();
        nvgStroke(*m_nvg);
    }

    void Context::MoveTo(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();

        nvgMoveTo(*m_nvg, x, y);
    }

    void Context::LineTo(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto x = info[0].As<Napi::Number>().FloatValue();
        const auto y = info[1].As<Napi::Number>().FloatValue();

        nvgLineTo(*m_nvg, x, y);
    }

    void Context::QuadraticCurveTo(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto cx = info[0].As<Napi::Number>().FloatValue();
        const auto cy = info[1].As<Napi::Number>().FloatValue();
        const auto x = info[2].As<Napi::Number>().FloatValue();
        const auto y = info[3].As<Napi::Number>().FloatValue();

        nvgBezierTo(*m_nvg, cx, cy, cx, cy, x, y);
    }

    Napi::Value Context::MeasureText(const Napi::CallbackInfo& info)
    {
        EnsureFrame();
        SetFontFaceId();

        std::string text{info[0].As<Napi::String>()};
        return MeasureText::CreateInstance(info.Env(), this, text);
    }

    void Context::EnsureLoadedFonts()
    {
        if (!m_nvg)
        {
            return;
        }

        for (auto& font : NativeCanvas::fontsInfos)
        {
            if (m_fonts.find(font.first) != m_fonts.end())
            {
                continue;
            }

            auto fontId = nvgCreateFontMem(*m_nvg, font.first.c_str(), font.second.data(), static_cast<int>(font.second.size()), 0);
            if (fontId >= 0)
            {
                m_fonts[font.first] = fontId;
            }
        }
    }

    bool Context::SetFontFaceId()
    {
        EnsureFrame();
        EnsureLoadedFonts();

        if (m_fonts.empty())
        {
            return false;
        }
        else if (m_currentFontId >= 0)
        {
            nvgFontFaceId(*m_nvg, m_currentFontId);
        }
        else
        {
            nvgFontFaceId(*m_nvg, m_fonts.begin()->second);
        }
        return true;
    }

    void Context::FillText(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        std::string text = info[0].As<Napi::String>().Utf8Value();
        auto x = info[1].As<Napi::Number>().FloatValue();
        auto y = info[2].As<Napi::Number>().FloatValue();

        if (m_direction == Direction::RTL) {
            // TODO(bidi): std::reverse on a UTF-8 byte string is incorrect for
            // multi-byte codepoints. Proper RTL/bidi support requires a Unicode
            // bidi algorithm (e.g. ICU ubidi or HarfBuzz).
            std::reverse(text.begin(), text.end());
        }

        if (SetFontFaceId())
        {
            BindFillStyle(info, 0.f, 0.f, x, y);
            nvgText(*m_nvg, x, y, text.c_str(), nullptr);
        }
    }

    void Context::Flush(const Napi::CallbackInfo&)
    {
        Flush();
    }

    void Context::Flush()
    {
        EnsureFrame();
        if (m_frameActive)
        {
            nvgEndFrame(*m_nvg);
            m_frameActive = false;
        }
    }

    void Context::PutImageData(const Napi::CallbackInfo& info)
    {
        if (info.Length() < 3 || !info[0].IsObject())
        {
            throw Napi::TypeError::New(info.Env(), "Context2D.putImageData requires ImageData, dx, and dy.");
        }

        const auto imageData = info[0].As<Napi::Object>();
        const uint32_t width = imageData.Get("width").As<Napi::Number>().Uint32Value();
        const uint32_t height = imageData.Get("height").As<Napi::Number>().Uint32Value();
        const auto dataValue = imageData.Get("data");
        if (width == 0 || height == 0 || !dataValue.IsTypedArray())
        {
            return;
        }

        const auto data = dataValue.As<Napi::TypedArray>();
        if (data.ElementSize() != 1)
        {
            throw Napi::TypeError::New(info.Env(), "Context2D.putImageData expects 8-bit RGBA image data.");
        }

        const size_t expectedBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        if (data.ByteLength() < expectedBytes)
        {
            throw Napi::RangeError::New(info.Env(), "Context2D.putImageData image data is smaller than width * height * 4.");
        }

        const auto buffer = data.ArrayBuffer();
        const auto* bytes = static_cast<const unsigned char*>(buffer.Data()) + data.ByteOffset();
        const auto imageHandle = nvgCreateImageRGBA(*m_nvg, static_cast<int>(width), static_cast<int>(height), 0, bytes);
        if (imageHandle == -1)
        {
            throw Napi::Error::New(info.Env(), "Context2D.putImageData failed to create an intermediate image.");
        }

        const auto dx = info[1].As<Napi::Number>().FloatValue();
        const auto dy = info[2].As<Napi::Number>().FloatValue();
        EnsureFrame();
        nvgSave(*m_nvg);
        nvgGlobalCompositeOperation(*m_nvg, NVG_COPY);
        nvgBeginPath(*m_nvg);
        nvgRect(*m_nvg, dx, dy, static_cast<float>(width), static_cast<float>(height));
        const auto imagePaint = nvgImagePattern(*m_nvg, dx, dy, static_cast<float>(width), static_cast<float>(height), 0.f, imageHandle, 1.f);
        nvgFillPaint(*m_nvg, imagePaint);
        nvgFill(*m_nvg);
        nvgRestore(*m_nvg);
        nvgDeleteImage(*m_nvg, imageHandle);
    }

    void Context::Arc(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto x = static_cast<float>(info[0].As<Napi::Number>().DoubleValue());
        const auto y = static_cast<float>(info[1].As<Napi::Number>().DoubleValue());
        const auto radius = static_cast<float>(info[2].As<Napi::Number>().DoubleValue());
        const auto startAngle = static_cast<float>(info[3].As<Napi::Number>().DoubleValue());
        const auto endAngle = static_cast<float>(info[4].As<Napi::Number>().DoubleValue());
        const NVGwinding winding = (info.Length() == 6 && info[5].As<Napi::Boolean>()) ? NVGwinding::NVG_CCW : NVGwinding::NVG_CW;
        nvgArc(*m_nvg, x, y, radius, startAngle, endAngle, winding);
    }

    void Context::DrawImage(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        if (info.Length() == 0 || !info[0].IsObject())
        {
            Napi::TypeError::New(info.Env(), "Context2D.drawImage source must be a native image object.").ThrowAsJavaScriptException();
            return;
        }

        const auto sourceObject = info[0].As<Napi::Object>();
        int imageIndex{-1};
        uint32_t sourceWidth{};
        uint32_t sourceHeight{};

        if (sourceObject.Has("_getNativeImageData"))
        {
            const NativeCanvasImage* canvasImage = NativeCanvasImage::Unwrap(sourceObject);
            if (canvasImage == nullptr)
            {
                Napi::TypeError::New(info.Env(), "Context2D.drawImage source is not backed by a native image.").ThrowAsJavaScriptException();
                return;
            }

            const auto nvgImageIter = m_nvgImageIndices.find(canvasImage);
            if (nvgImageIter == m_nvgImageIndices.end())
            {
                imageIndex = canvasImage->CreateNVGImageForContext(*m_nvg);
                m_nvgImageIndices.try_emplace(canvasImage, imageIndex);
            }
            else
            {
                imageIndex = nvgImageIter->second;
            }

            sourceWidth = canvasImage->GetWidth();
            sourceHeight = canvasImage->GetHeight();
        }
        else if (sourceObject.Has("getCanvasTexture"))
        {
            const auto getCanvasTextureValue = sourceObject.Get("getCanvasTexture");
            if (!getCanvasTextureValue.IsFunction())
            {
                Napi::TypeError::New(info.Env(), "Context2D.drawImage canvas source has an invalid getCanvasTexture method.").ThrowAsJavaScriptException();
                return;
            }

            const auto payloadValue = getCanvasTextureValue.As<Napi::Function>().Call(sourceObject, {});
            if (!payloadValue.IsObject())
            {
                Napi::Error::New(info.Env(), "Context2D.drawImage canvas source did not provide a native texture payload.").ThrowAsJavaScriptException();
                return;
            }

            const auto payload = payloadValue.As<Napi::Object>();
            const auto nativeTextureValue = payload.Get("nativeTexture");
            if (!nativeTextureValue.IsExternal())
            {
                Napi::Error::New(info.Env(), "Context2D.drawImage canvas source native texture payload was missing.").ThrowAsJavaScriptException();
                return;
            }

            const auto* nativeTexture = nativeTextureValue.As<Napi::External<void>>().Data();
            sourceWidth = payload.Get("width").As<Napi::Number>().Uint32Value();
            sourceHeight = payload.Get("height").As<Napi::Number>().Uint32Value();
            const auto generationValue = payload.Get("generation");
            const auto generation = generationValue.IsNumber()
                ? static_cast<uint64_t>(generationValue.As<Napi::Number>().DoubleValue())
                : 0u;

            auto imageIter = m_canvasTextureImageIndices.find(nativeTexture);
            if (imageIter == m_canvasTextureImageIndices.end() ||
                imageIter->second.generation != generation ||
                imageIter->second.width != sourceWidth ||
                imageIter->second.height != sourceHeight)
            {
                if (imageIter != m_canvasTextureImageIndices.end())
                {
                    nvgDeleteImage(*m_nvg, imageIter->second.imageIndex);
                    m_canvasTextureImageIndices.erase(imageIter);
                }

                imageIndex = nvgCreateImageFromNativeTexture(
                    *m_nvg,
                    nativeTexture,
                    static_cast<int>(sourceWidth),
                    static_cast<int>(sourceHeight),
                    0);
                if (imageIndex == -1)
                {
                    Napi::Error::New(info.Env(), "Context2D.drawImage could not import the canvas source texture into CanvasWgpu.").ThrowAsJavaScriptException();
                    return;
                }

                m_canvasTextureImageIndices.try_emplace(
                    nativeTexture,
                    CanvasTextureImageEntry{
                        imageIndex,
                        generation,
                        sourceWidth,
                        sourceHeight});
            }
            else
            {
                imageIndex = imageIter->second.imageIndex;
            }
        }
        else
        {
            Napi::TypeError::New(info.Env(), "Context2D.drawImage source must be a native image or canvas object.").ThrowAsJavaScriptException();
            return;
        }

        if (imageIndex == -1)
        {
            Napi::Error::New(info.Env(), "Unable to create native canvas image.").ThrowAsJavaScriptException();
            return;
        }

        if (info.Length() == 3)
        {
            const auto dx = static_cast<float>(info[1].As<Napi::Number>().Int32Value());
            const auto dy = static_cast<float>(info[2].As<Napi::Number>().Int32Value());
            const auto width = static_cast<float>(sourceWidth);
            const auto height = static_cast<float>(sourceHeight);

            NVGpaint imagePaint = nvgImagePattern(*m_nvg, 0.f, 0.f, width, height, 0.f, imageIndex, 1.f);

            if (!m_isClipped)
            {
                nvgBeginPath(*m_nvg);
            }

            nvgRect(*m_nvg, dx, dy, width, height);
            nvgFillPaint(*m_nvg, imagePaint);
            SetFilterStack();
            nvgFill(*m_nvg);
        }
        else if (info.Length() == 5)
        {
            const auto dx = static_cast<float>(info[1].As<Napi::Number>().Int32Value());
            const auto dy = static_cast<float>(info[2].As<Napi::Number>().Int32Value());
            const auto dWidth = static_cast<float>(info[3].As<Napi::Number>().Uint32Value());
            const auto dHeight = static_cast<float>(info[4].As<Napi::Number>().Uint32Value());

            NVGpaint imagePaint = nvgImagePattern(*m_nvg, dx, dy, dWidth, dHeight, 0.f, imageIndex, 1.f);

            if (!m_isClipped)
            {
                nvgBeginPath(*m_nvg);
            }

            nvgRect(*m_nvg, dx, dy, dWidth, dHeight);
            nvgFillPaint(*m_nvg, imagePaint);
            SetFilterStack();
            nvgFill(*m_nvg);
        }
        else if (info.Length() == 9)
        {
            // TODO(drawImage): The 9-argument form should use the source
            // rectangle (sx, sy, sWidth, sHeight) to sample a sub-region of
            // the source image. Currently the source rect is ignored and the
            // entire source image is drawn into the destination rect. This
            // requires computing UV offset/scale for nvgImagePattern or
            // creating a cropped intermediate image.
            const auto sx = info[1].As<Napi::Number>().Int32Value();
            const auto sy = info[2].As<Napi::Number>().Int32Value();
            const auto sWidth = info[3].As<Napi::Number>().Uint32Value();
            const auto sHeight = info[4].As<Napi::Number>().Uint32Value();
            const auto dx = static_cast<float>(info[5].As<Napi::Number>().Int32Value());
            const auto dy = static_cast<float>(info[6].As<Napi::Number>().Int32Value());
            const auto dWidth = static_cast<float>(info[7].As<Napi::Number>().Uint32Value());
            const auto dHeight = static_cast<float>(info[8].As<Napi::Number>().Uint32Value());
            const auto width = static_cast<float>(sourceWidth);
            const auto height = static_cast<float>(sourceHeight);
            (void)sx;
            (void)sy;
            (void)sWidth;
            (void)sHeight;
            (void)width;
            (void)height;

            NVGpaint imagePaint = nvgImagePattern(*m_nvg, dx, dy, dWidth, dHeight, 0.f, imageIndex, 1.f);

            if (!m_isClipped)
            {
                nvgBeginPath(*m_nvg);
            }

            nvgRect(*m_nvg, dx, dy, dWidth, dHeight);
            nvgFillPaint(*m_nvg, imagePaint);
            SetFilterStack();
            nvgFill(*m_nvg);
        }
        else
        {
            Napi::Error::New(info.Env(), "Invalid number of parameters for DrawImage").ThrowAsJavaScriptException();
            return;
        }
    }

    Napi::Value Context::GetImageData(const Napi::CallbackInfo& info)
    {
        // TODO: support source x and y
        //const auto sx = info[0].As<Napi::Number>().Uint32Value();
        //const auto sy = info[1].As<Napi::Number>().Uint32Value();
        const auto sw = info[2].As<Napi::Number>().Uint32Value();
        const auto sh = info[3].As<Napi::Number>().Uint32Value();

        return ImageData::CreateInstance(info.Env(), this, sw, sh);
    }

    void Context::SetLineDash(const Napi::CallbackInfo& info)
    {
        // femtovg does not currently expose dashed stroke rendering. Accept the
        // browser API call so GUI layout/rendering paths that set a dash pattern
        // continue to execute instead of failing before visual comparison.
        (void)info;
    }

    void Context::StrokeText(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        std::string text = info[0].As<Napi::String>().Utf8Value();
        auto x = info[1].As<Napi::Number>().FloatValue();
        auto y = info[2].As<Napi::Number>().FloatValue();

        if (m_direction == Direction::RTL) {
            // TODO(bidi): std::reverse on a UTF-8 byte string is incorrect for
            // multi-byte codepoints. Proper RTL/bidi support requires a Unicode
            // bidi algorithm (e.g. ICU ubidi or HarfBuzz).
            std::reverse(text.begin(), text.end());
        }

        if (SetFontFaceId())
        {
            nvgStrokeText(*m_nvg, x, y, text.c_str(), nullptr);
        }
    }

    Napi::Value Context::CreateLinearGradient(const Napi::CallbackInfo& info)
    {
        const auto x0 = info[0].As<Napi::Number>().FloatValue();
        const auto y0 = info[1].As<Napi::Number>().FloatValue();
        const auto x1 = info[2].As<Napi::Number>().FloatValue();
        const auto y1 = info[3].As<Napi::Number>().FloatValue();

        auto gradient = CanvasGradient::CreateLinear(info.Env(), m_nvg, x0, y0, x1, y1);
        return gradient;
    }

    Napi::Value Context::CreateRadialGradient(const Napi::CallbackInfo& info)
    {
        const auto x0 = info[0].As<Napi::Number>().FloatValue();
        const auto y0 = info[1].As<Napi::Number>().FloatValue();
        const auto r0 = info[2].As<Napi::Number>().FloatValue();
        const auto x1 = info[3].As<Napi::Number>().FloatValue();
        const auto y1 = info[4].As<Napi::Number>().FloatValue();
        const auto r1 = info[5].As<Napi::Number>().FloatValue();

        auto gradient = CanvasGradient::CreateRadial(info.Env(), m_nvg, x0, y0, r0, x1, y1, r1);
        return gradient;
    }

    Napi::Value Context::GetTransform(const Napi::CallbackInfo&)
    {
        EnsureFrame();

        float xform[6];
        nvgCurrentTransform(*m_nvg, xform);

        // set DOMMatrix properties
        Napi::Object obj = Napi::Object::New(Env());
        obj.Set("a", xform[0]);
        obj.Set("b", xform[1]);
        obj.Set("c", xform[2]);
        obj.Set("d", xform[3]);
        obj.Set("e", xform[4]);
        obj.Set("f", xform[5]);
        obj.Set("m11", xform[0]);
        obj.Set("m12", xform[1]);
        obj.Set("m13", 0);
        obj.Set("m14", 0);
        obj.Set("m21", xform[2]);
        obj.Set("m22", xform[3]);
        obj.Set("m23", 0);
        obj.Set("m24", 0);
        obj.Set("m31", 0);
        obj.Set("m32", 0);
        obj.Set("m33", 1);
        obj.Set("m34", 0);
        obj.Set("m41", xform[4]);
        obj.Set("m42", xform[5]);
        obj.Set("m43", 0);
        obj.Set("m44", 1);
        obj.Set("is2D", true);
        obj.Set("isIdentity", false);
        return obj;
    }

    void Context::SetTransform(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto a = info[0].As<Napi::Number>().FloatValue();
        const auto b = info[1].As<Napi::Number>().FloatValue();
        const auto c = info[2].As<Napi::Number>().FloatValue();
        const auto d = info[3].As<Napi::Number>().FloatValue();
        const auto e = info[4].As<Napi::Number>().FloatValue();
        const auto f = info[5].As<Napi::Number>().FloatValue();
        nvgResetTransform(*m_nvg);
        nvgTransform(*m_nvg, a, b, c, d, e, f);
    }

    void Context::Transform(const Napi::CallbackInfo& info)
    {
        EnsureFrame();

        const auto a = info[0].As<Napi::Number>().FloatValue();
        const auto b = info[1].As<Napi::Number>().FloatValue();
        const auto c = info[2].As<Napi::Number>().FloatValue();
        const auto d = info[3].As<Napi::Number>().FloatValue();
        const auto e = info[4].As<Napi::Number>().FloatValue();
        const auto f = info[5].As<Napi::Number>().FloatValue();
        nvgTransform(*m_nvg, a, b, c, d, e, f);
    }

    Napi::Value Context::GetLineCap(const Napi::CallbackInfo& info)
    {
        const char* name = "butt";
        if (m_lineCap == NVG_ROUND) name = "round";
        else if (m_lineCap == NVG_SQUARE) name = "square";
        return Napi::Value::From(Env(), name);
    }

    void Context::SetLineCap(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        EnsureFrame();

        m_lineCap = StringToLineCap(info.Env(), value.As<Napi::String>().Utf8Value());
        nvgLineCap(*m_nvg, m_lineCap);
    }

    Napi::Value Context::GetLineJoin(const Napi::CallbackInfo& info)
    {
        const char* name = "miter";
        if (m_lineJoin == NVG_ROUND) name = "round";
        else if (m_lineJoin == NVG_BEVEL) name = "bevel";
        return Napi::Value::From(Env(), name);
    }

    void Context::SetLineJoin(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        EnsureFrame();

        m_lineJoin = StringToLineJoin(info.Env(), value.As<Napi::String>().Utf8Value());
        nvgLineJoin(*m_nvg, m_lineJoin);
    }

    Napi::Value Context::GetMiterLimit(const Napi::CallbackInfo& info)
    {
        return Napi::Value::From(Env(), m_miterLimit);
    }

    void Context::SetMiterLimit(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        EnsureFrame();

        m_miterLimit = value.As<Napi::Number>().FloatValue();
        nvgMiterLimit(*m_nvg, m_miterLimit);
    }

    Napi::Value Context::GetFilter(const Napi::CallbackInfo& info)
    {
        return Napi::Value::From(Env(), m_filter);
    }

    void Context::SetFilter(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        auto filterString = value.As<Napi::String>().Utf8Value();
        if (nanovg_filterstack::ValidString(filterString))
        {
            m_filter = std::move(filterString);
        }
    }

    Napi::Value Context::GetDirection(const Napi::CallbackInfo& info)
    {
        return Napi::Value::From(Env(), m_direction == Direction::RTL ? "rtl" : "ltr");
    }

    void Context::SetDirection(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        const auto direction = value.As<Napi::String>().Utf8Value();
        if (direction == "rtl")
        {
            m_direction = Direction::RTL;
        }
        else if (direction == "ltr")
        {
            m_direction = Direction::LTR;
        }
    }

    Napi::Value Context::GetFont(const Napi::CallbackInfo& info)
    {
        return Napi::Value::From(Env(), static_cast<std::string>(m_font));
    }

    void Context::SetFont(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        EnsureFrame();

        if (!value.IsString())
        {
            throw Napi::Error::New(info.Env(), "invalid argument");
        }

        auto font = Font::Parse(value.ToString());
        if (!font)
        {
            return;
        }

        nvgFontSize(*m_nvg, font->Size());
        EnsureLoadedFonts();
        if (m_fonts.find(font->Family()) == m_fonts.end())
        {
            // TODO: handle finding font face for a specific weight and style
            m_currentFontId = -1;
        }
        else
        {
            m_currentFontId = m_fonts.at(font->Family());
        }

        m_font = std::move(*font);
    }

    Napi::Value Context::GetLetterSpacing(const Napi::CallbackInfo& info)
    {
        std::string letterSpacingStr = std::to_string(m_letterSpacing);
        letterSpacingStr.erase(letterSpacingStr.find_last_not_of('0') + 1, std::string::npos);
        letterSpacingStr.erase(letterSpacingStr.find_last_not_of('.') + 1, std::string::npos);
        return Napi::Value::From(Env(), letterSpacingStr + "px");
    }

    void Context::SetLetterSpacing(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        EnsureFrame();

        const std::string letterSpacingOption = value.ToString();

        // regex the letter spacing string
        static const std::regex letterSpacingRegex("(\\d+(\\.\\d+)?)px");
        std::smatch letterSpacingMatch;
        if (std::regex_match(letterSpacingOption, letterSpacingMatch, letterSpacingRegex))
        {
            m_letterSpacing = std::stof(letterSpacingMatch[1]);
        }
        nvgTextLetterSpacing(*m_nvg, m_letterSpacing);
    }

    void Context::SetGlobalAlpha(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        (void)info;
        EnsureFrame();

        m_globalAlpha = value.As<Napi::Number>().FloatValue();
        nvgGlobalAlpha(*m_nvg, m_globalAlpha);
    }

    Napi::Value Context::GetShadowColor(const Napi::CallbackInfo& info)
    {
        return Napi::String::New(info.Env(), m_shadowColor);
    }

    void Context::SetShadowColor(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        (void)info;
        m_shadowColor = value.ToString();
    }

    Napi::Value Context::GetShadowBlur(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_shadowBlur);
    }

    void Context::SetShadowBlur(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        (void)info;
        m_shadowBlur = value.As<Napi::Number>().FloatValue();
    }

    Napi::Value Context::GetShadowOffsetX(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_shadowOffsetX);
    }

    void Context::SetShadowOffsetX(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        (void)info;
        m_shadowOffsetX = value.As<Napi::Number>().FloatValue();
    }

    Napi::Value Context::GetShadowOffsetY(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_shadowOffsetY);
    }

    void Context::SetShadowOffsetY(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        (void)info;
        m_shadowOffsetY = value.As<Napi::Number>().FloatValue();
    }
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
