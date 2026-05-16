#pragma once

#include <Babylon/Polyfills/Canvas.h>
#include <cstdint>
#include <map>
#include "nanovg/nanovg.h"

struct NVGcontext;

namespace Babylon::Polyfills::Internal
{
    class CanvasGradient final : public Napi::ObjectWrap<CanvasGradient>
    {
    public:
        static void Initialize(Napi::Env);
        static Napi::Object CreateLinear(Napi::Env env, const std::shared_ptr<NVGcontext*>& context, float x0, float y0, float x1, float y1);
        static Napi::Object CreateRadial(Napi::Env env, const std::shared_ptr<NVGcontext*>& context, float x0, float y0, float r0, float x1, float y1, float r1);

        explicit CanvasGradient(const Napi::CallbackInfo& info);
        virtual ~CanvasGradient();

        NVGpaint Paint(uint32_t canvasWidth, uint32_t canvasHeight);
        NVGcolor SampleColor(float x, float y) const;
        void Dispose();

    protected:
        float x0, y0, x1, y1;
        float r0, r1;
        std::map<float, NVGcolor> colors;
        int cachedPaintHandle{-1};
        NVGpaint cachedPaint{};
        uint32_t cachedCanvasWidth{};
        uint32_t cachedCanvasHeight{};
        std::weak_ptr< NVGcontext*> context;
        bool dirty{true};
        enum class GradientType
        {
            Linear,
            Radial
        };
        GradientType gradientType;
        void AddColorStop(const Napi::CallbackInfo& info);
        void UpdateCache(uint32_t canvasWidth, uint32_t canvasHeight);
    };
}
