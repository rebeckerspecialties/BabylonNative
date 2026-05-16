#include "Canvas.h"
#include "Context.h"
#include "Gradient.h"
#include "Colors.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

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
    namespace
    {
        NVGpaint SolidPaint(NVGcolor color)
        {
            NVGpaint paint{};
            paint.image = -1;
            paint.alpha = color.a;
            paint.kind = 0;
            paint.innerColor = color;
            paint.outerColor = color;
            return paint;
        }

        float LinearOffset(float px, float py, float x0, float y0, float x1, float y1)
        {
            const auto dx = x1 - x0;
            const auto dy = y1 - y0;
            const auto denominator = dx * dx + dy * dy;
            if (denominator <= std::numeric_limits<float>::epsilon())
            {
                return 0.0f;
            }

            return std::clamp(((px - x0) * dx + (py - y0) * dy) / denominator, 0.0f, 1.0f);
        }

        float RadialOffset(float px, float py, float x0, float y0, float r0, float x1, float y1, float r1)
        {
            const auto dx = x1 - x0;
            const auto dy = y1 - y0;
            const auto dr = r1 - r0;
            const auto fx = x0 - px;
            const auto fy = y0 - py;

            const auto a = dx * dx + dy * dy - dr * dr;
            const auto b = 2.0f * (fx * dx + fy * dy - r0 * dr);
            const auto c = fx * fx + fy * fy - r0 * r0;
            auto best = std::numeric_limits<float>::quiet_NaN();

            if (std::abs(a) <= std::numeric_limits<float>::epsilon())
            {
                if (std::abs(b) > std::numeric_limits<float>::epsilon())
                {
                    best = -c / b;
                }
            }
            else
            {
                const auto discriminant = b * b - 4.0f * a * c;
                if (discriminant >= 0.0f)
                {
                    const auto root = std::sqrt(discriminant);
                    const float candidates[] = {
                        (-b - root) / (2.0f * a),
                        (-b + root) / (2.0f * a),
                    };
                    for (const auto candidate : candidates)
                    {
                        if (std::isfinite(candidate) && candidate >= 0.0f)
                        {
                            best = std::isfinite(best) ? std::min(best, candidate) : candidate;
                        }
                    }
                }
            }

            if (!std::isfinite(best))
            {
                best = c <= 0.0f ? 0.0f : 1.0f;
            }
            return std::clamp(best, 0.0f, 1.0f);
        }

        NVGcolor LerpColor(NVGcolor start, NVGcolor end, float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            const auto alpha = start.a + (end.a - start.a) * t;
            if (alpha <= std::numeric_limits<float>::epsilon())
            {
                return nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f);
            }

            const auto premultipliedR = start.r * start.a + (end.r * end.a - start.r * start.a) * t;
            const auto premultipliedG = start.g * start.a + (end.g * end.a - start.g * start.a) * t;
            const auto premultipliedB = start.b * start.a + (end.b * end.a - start.b * start.a) * t;
            return nvgRGBAf(
                std::clamp(premultipliedR / alpha, 0.0f, 1.0f),
                std::clamp(premultipliedG / alpha, 0.0f, 1.0f),
                std::clamp(premultipliedB / alpha, 0.0f, 1.0f),
                std::clamp(alpha, 0.0f, 1.0f));
        }
    }

    static constexpr auto JS_CANVAS_GRADIENT_CONSTRUCTOR_NAME = "CanvasGradient";

    void CanvasGradient::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{ env };

        Napi::Function func = DefineClass(
            env,
            JS_CANVAS_GRADIENT_CONSTRUCTOR_NAME,
            {
                InstanceMethod("addColorStop", &CanvasGradient::AddColorStop),
                
            });
        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_CANVAS_GRADIENT_CONSTRUCTOR_NAME, func);
    }

    Napi::Object CanvasGradient::CreateLinear(Napi::Env env, const std::shared_ptr<NVGcontext*>& context, float x0, float y0, float x1, float y1)
    {
        Napi::HandleScope scope{ env };

        auto func = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_CANVAS_GRADIENT_CONSTRUCTOR_NAME).As<Napi::Function>();
        auto gradientValue = func.New({ Napi::Value::From(env, x0), Napi::Value::From(env, y0), Napi::Value::From(env, x1), Napi::Value::From(env, y1) });
        CanvasGradient::Unwrap(gradientValue)->context = context;
        return gradientValue;
    }

    Napi::Object CanvasGradient::CreateRadial(Napi::Env env, const std::shared_ptr<NVGcontext*>& context, float x0, float y0, float r0, float x1, float y1, float r1)
    {
        Napi::HandleScope scope{ env };

        auto func = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_CANVAS_GRADIENT_CONSTRUCTOR_NAME).As<Napi::Function>();
        auto gradientValue = func.New({ Napi::Value::From(env, x0), Napi::Value::From(env, y0), Napi::Value::From(env, x1), Napi::Value::From(env, y1), Napi::Value::From(env, r0), Napi::Value::From(env, r1) });
        CanvasGradient::Unwrap(gradientValue)->context = context;
        return gradientValue;
    }

    CanvasGradient::CanvasGradient(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<CanvasGradient>{ info }
        , x0{ info[0].As<Napi::Number>().FloatValue() }
        , y0{ info[1].As<Napi::Number>().FloatValue() }
        , x1{ info[2].As<Napi::Number>().FloatValue() }
        , y1{ info[3].As<Napi::Number>().FloatValue() }
    {
        gradientType = (info.Length() == 4) ? GradientType::Linear : GradientType::Radial;
        if (gradientType == GradientType::Radial)
        {
            r0 = info[4].As<Napi::Number>().FloatValue();
            r1 = info[5].As<Napi::Number>().FloatValue();
        }
    }

    CanvasGradient::~CanvasGradient()
    {
        Dispose();
    }

    void CanvasGradient::Dispose()
    {
        if (cachedPaintHandle >= 0)
        {
            if (auto nvgContext = context.lock())
            {
                nvgDeletePaint(*nvgContext, cachedPaintHandle);
            }
            cachedPaintHandle = -1;
        }
        cachedPaint = SolidPaint(TRANSPARENT_BLACK);
        dirty = true;
    }

    void CanvasGradient::AddColorStop(const Napi::CallbackInfo& info)
    {
        const auto offset = info[0].As<Napi::Number>().FloatValue();
        if (!std::isfinite(offset) || offset < 0.0f || offset > 1.0f)
        {
            throw Napi::RangeError::New(info.Env(), "CanvasGradient.addColorStop offset must be a finite number between 0 and 1.");
        }

        std::string colorString{ info[1].As<Napi::String>() };
        const auto color = StringToColor(info.Env(), colorString);
        colors.insert(std::make_pair(offset, color));
        dirty = true;
    }

    NVGpaint CanvasGradient::Paint(uint32_t canvasWidth, uint32_t canvasHeight)
    {
        UpdateCache(canvasWidth, canvasHeight);
        return cachedPaint;
    }

    NVGcolor CanvasGradient::SampleColor(float x, float y) const
    {
        if (colors.empty())
        {
            return TRANSPARENT_BLACK;
        }

        const auto offset = gradientType == GradientType::Linear ?
            LinearOffset(x + 0.5f, y + 0.5f, x0, y0, x1, y1) :
            RadialOffset(x + 0.5f, y + 0.5f, x0, y0, std::max(r0, 0.0f), x1, y1, std::max(r1, 0.0f));

        auto next = colors.lower_bound(offset);
        if (next == colors.begin())
        {
            return next->second;
        }
        if (next == colors.end())
        {
            return colors.rbegin()->second;
        }

        const auto previous = std::prev(next);
        const auto span = std::max(next->first - previous->first, std::numeric_limits<float>::epsilon());
        return LerpColor(previous->second, next->second, (offset - previous->first) / span);
    }

    void CanvasGradient::UpdateCache(uint32_t canvasWidth, uint32_t canvasHeight)
    {
        if (!dirty && cachedCanvasWidth == canvasWidth && cachedCanvasHeight == canvasHeight)
        {
            return;
        }

        if (cachedPaintHandle >= 0)
        {
            if (auto nvgContext = context.lock())
            {
                nvgDeletePaint(*nvgContext, cachedPaintHandle);
            }
            cachedPaintHandle = -1;
        }

        auto nvgContext = context.lock();
        if (!nvgContext || colors.empty())
        {
            cachedPaint = SolidPaint(TRANSPARENT_BLACK);
            cachedCanvasWidth = canvasWidth;
            cachedCanvasHeight = canvasHeight;
            dirty = false;
            return;
        }

        std::vector<NVGgradientStop> stops;
        stops.reserve(colors.size());
        for (const auto& [offset, color] : colors)
        {
            stops.push_back({ offset, color });
        }

        cachedPaint = gradientType == GradientType::Linear ?
            nvgLinearGradientStops(*nvgContext, static_cast<int>(canvasWidth), static_cast<int>(canvasHeight), x0, y0, x1, y1, stops.data(), stops.size()) :
            nvgRadialGradientStops(*nvgContext, static_cast<int>(canvasWidth), static_cast<int>(canvasHeight), x0, y0, r0, x1, y1, r1, stops.data(), stops.size());
        cachedPaintHandle = cachedPaint.kind == 2 ? cachedPaint.image : -1;
        cachedCanvasWidth = canvasWidth;
        cachedCanvasHeight = canvasHeight;
        dirty = false;
    }
}
