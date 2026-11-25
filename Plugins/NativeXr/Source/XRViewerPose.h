#pragma once

#include "XRView.h"
#include <Babylon/JsRuntime.h>
#include <napi/env.h>
#include <optional>

namespace Babylon
{
    class XRViewerPose final
    {
        static constexpr auto JS_CLASS_NAME = "XRViewerPose";

    public:
        static void Initialize(Napi::Env env)
        {
            const auto constructors = Napi::Eval(env, R"JS(
                (() => {
                    const key = {};
                    class XRViewerPose {
                        #views;
                        #transform;
                        #emulated;
                        constructor(token = null, views, transform, emulated) {
                            this.#views = Object.freeze(token === key ? views : []);
                            this.#transform = token === key ? transform : new XRRigidTransform();
                            this.#emulated = token === key ? emulated : true;
                        }
                        get views() { return this.#views; }
                        get transform() { return this.#transform; }
                        get emulatedPosition() { return this.#emulated; }
                    }
                    return [XRViewerPose, XRViewerPose.bind(null, key)];
                })()
            )JS", "babylon-native://plugins/NativeXr/XRViewerPose.js").As<Napi::Array>();
            env.Global().Set(JS_CLASS_NAME, constructors.Get(uint32_t{0}));
            JsRuntime::NativeObject::GetFromJavaScript(env).Set("NativeXRViewerPose", constructors.Get(uint32_t{1}));
        }

        static Napi::Object New(const Napi::CallbackInfo& info,
            const std::shared_ptr<const xr::System::Session::Frame>& frame, const std::optional<xr::Pose>& referenceSpaceInverse)
        {
            const auto env = info.Env();
            const auto adjust = [&](const xr::Space& space) {
                return referenceSpaceInverse ? xr::Space{ComposePoses(*referenceSpaceInverse, space.Pose)} : space;
            };
            auto transform = XRRigidTransform::New(env);
            if (frame->ViewerSpace || !frame->Views.empty())
            {
                const auto& origin = frame->ViewerSpace ? *frame->ViewerSpace : frame->Views.front().Space;
                XRRigidTransform::Unwrap(transform)->Update(adjust(origin), false);
            }
            auto views = Napi::Array::New(env, frame->Views.size());
            for (uint32_t i = 0; i < frame->Views.size(); ++i)
            {
                const auto& source = frame->Views[i];
                auto view = XRView::New(info);
                XRView::Unwrap(view)->Update(i, source.ProjectionMatrix, adjust(source.Space), source.IsFirstPersonObserver, frame);
                views.Set(i, view);
            }
            // Views retain their native frame brand; only JS owns the edges of this snapshot.
            return JsRuntime::NativeObject::GetFromJavaScript(env).Get("NativeXRViewerPose").As<Napi::Function>()
                .New({views, transform, Napi::Boolean::New(env, !frame->IsTracking)});
        }
    };
}
