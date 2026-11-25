#pragma once

#include "XRView.h"
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
            env.Global().Set(JS_CLASS_NAME, Napi::Eval(env, R"JS(
                (class XRViewerPose {
                    #views;
                    #transform;
                    #emulated;
                    constructor(views = [], transform = new XRRigidTransform(), emulated = true) {
                        this.#views = Object.freeze(views);
                        this.#transform = transform;
                        this.#emulated = emulated;
                    }
                    get views() { return this.#views; }
                    get transform() { return this.#transform; }
                    get emulatedPosition() { return this.#emulated; }
                })
            )JS", "babylon-native://plugins/NativeXr/XRViewerPose.js"));
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
            return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({views, transform, Napi::Boolean::New(env, !frame->IsTracking)});
        }
    };
}
