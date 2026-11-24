#include <Babylon/Plugins/NativeXr.h>

#include <Babylon/JsRuntimeScheduler.h>

#include <XR.h>
#include <Babylon/Graphics/DeviceContext.h>

#include "JsInstanceOf.h"
#include "Constants.h"
#include "PointerEvent.h"
#include "XRWebGLBinding.h"
#include "XRWebGLLayer.h"
#include "XRProjectionLayer.h"
#include "XRRigidTransform.h"
#include "XRView.h"
#include "XRViewerPose.h"
#include "XRPose.h"
#include "XRRay.h"
#include "XRReferenceSpace.h"
#include "XRAnchor.h"
#include "XRHitTestSource.h"
#include "XRHitTestResult.h"
#include "XRPlane.h"
#include "XRMesh.h"
#include "XRHand.h"
#include "NativeXrImpl.h"
#include "XRSession.h"
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
#include "XRShapedLayer.h"
#include <Babylon/Graphics/WgpuInterop.h>
#endif
#include "XRFrame.h"
#include "ImageTracking.h"

#include <cstdio>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace Babylon
{
    namespace
    {
#if defined(__ANDROID__)
        void LogSessionStage(const char* stage)
        {
            __android_log_print(ANDROID_LOG_INFO, "BabylonNative", "NativeXR requestSession: %s", stage);
        }

        void LogSessionFailure(const std::exception_ptr& exception)
        {
            try
            {
                std::rethrow_exception(exception);
            }
            catch (const std::exception& error)
            {
                __android_log_print(
                    ANDROID_LOG_ERROR,
                    "BabylonNative",
                    "NativeXR requestSession failed: %s",
                    error.what());
            }
            catch (...)
            {
                __android_log_print(
                    ANDROID_LOG_ERROR,
                    "BabylonNative",
                    "NativeXR requestSession failed with an unknown exception.");
            }
        }
#else
        void LogSessionStage(const char*)
        {
        }

        void LogSessionFailure(const std::exception_ptr&)
        {
        }
#endif

        void SetXRGamepadObjectData(Napi::Object& jsInputSource, Napi::Object& jsGamepadObject, xr::System::Session::Frame::InputSource& inputSource)
        {
            auto env = jsInputSource.Env();
            //Set Gamepad Object
            auto gamepadButtons = Napi::Array::New(env, inputSource.GamepadObject.Buttons.size());
            for (size_t i = 0; i < inputSource.GamepadObject.Buttons.size(); i++)
            {
                auto gamepadButton = Napi::Object::New(env);
                auto napiGamepadPressed = Napi::Boolean::New(env, inputSource.GamepadObject.Buttons[i].Pressed);
                auto napiGamepadTouched = Napi::Boolean::New(env, inputSource.GamepadObject.Buttons[i].Touched);
                auto napiGamepadValue = Napi::Number::New(env, inputSource.GamepadObject.Buttons[i].Value);
                gamepadButton.Set("pressed", napiGamepadPressed);
                gamepadButton.Set("touched", napiGamepadTouched);
                gamepadButton.Set("value", napiGamepadValue);
                gamepadButtons.Set(static_cast<int>(i), gamepadButton);
            }
            jsGamepadObject.Set("buttons", gamepadButtons);

            auto gamepadAxes = Napi::Array::New(env, inputSource.GamepadObject.Axes.size());
            for (size_t i = 0; i < inputSource.GamepadObject.Axes.size(); i++)
            {
                auto napiGamepadAxesValue = Napi::Number::New(env, inputSource.GamepadObject.Axes[i]);
                gamepadAxes.Set(static_cast<int>(i), napiGamepadAxesValue);
            }
            jsGamepadObject.Set("axes", gamepadAxes);
            jsInputSource.Set("gamepad", jsGamepadObject);
        }

        void CreateXRGamepadObject(Napi::Object& jsInputSource, xr::System::Session::Frame::InputSource& inputSource)
        {
            auto env = jsInputSource.Env();
            auto jsGamepadObject = Napi::Object::New(env);
            SetXRGamepadObjectData(jsInputSource, jsGamepadObject, inputSource);
        }

        void PopulateDetectionBoundary(const Napi::Object& object, xr::DetectionBoundary& detectionBoundary)
        {
            if (object.Has("type"))
            {
                const std::map<std::string, xr::DetectionBoundaryType> detectionBoundaryTypeMap{
                    {"box", xr::DetectionBoundaryType::Box},
                    {"frustum", xr::DetectionBoundaryType::Frustum},
                    {"sphere", xr::DetectionBoundaryType::Sphere} };
                detectionBoundary.Type = detectionBoundaryTypeMap.at(object.Get("type").As<Napi::String>());
            }

            switch (detectionBoundary.Type)
            {
            case xr::DetectionBoundaryType::Box:
                if (object.Has("extent"))
                {
                    const auto& vector = object.Get("extent").As<Napi::Object>();
                    xr::Vector3f boxDimensions{};
                    boxDimensions.X = vector.Get("x").As<Napi::Number>();
                    boxDimensions.Y = vector.Get("y").As<Napi::Number>();
                    boxDimensions.Z = vector.Get("z").As<Napi::Number>();
                    detectionBoundary.Data = boxDimensions;
                }
                break;
            case xr::DetectionBoundaryType::Frustum:
                if (object.Has("frustum"))
                {
                    const auto& frustum = object.Get("frustum").As<Napi::Object>();
                    xr::Frustum frustumData{};
                    frustumData.FarDistance = frustum.Get("farDistance").As<Napi::Number>();

                    const auto& vector = frustum.Get("position").As<Napi::Object>();
                    frustumData.Pose.Position.X = vector.Get("x").As<Napi::Number>();
                    frustumData.Pose.Position.Y = vector.Get("y").As<Napi::Number>();
                    frustumData.Pose.Position.Z = vector.Get("z").As<Napi::Number>();

                    const auto& quaternion = frustum.Get("orientation").As<Napi::Object>();
                    frustumData.Pose.Orientation.X = quaternion.Get("x").As<Napi::Number>();
                    frustumData.Pose.Orientation.Y = quaternion.Get("y").As<Napi::Number>();
                    frustumData.Pose.Orientation.Z = quaternion.Get("z").As<Napi::Number>();
                    frustumData.Pose.Orientation.W = quaternion.Get("w").As<Napi::Number>();

                    const auto& fov = frustum.Get("fieldOfView").As<Napi::Object>();
                    frustumData.FOV.AngleLeft = fov.Get("angleLeft").As<Napi::Number>();
                    frustumData.FOV.AngleRight = fov.Get("angleRight").As<Napi::Number>();
                    frustumData.FOV.AngleUp = fov.Get("angleUp").As<Napi::Number>();
                    frustumData.FOV.AngleDown = fov.Get("angleDown").As<Napi::Number>();

                    frustumData.FarDistance = frustum.Get("farDistance").As<Napi::Number>();

                    detectionBoundary.Data = frustumData;
                }
                break;
            case xr::DetectionBoundaryType::Sphere:
                if (object.Has("radius"))
                {
                    detectionBoundary.Data = object.Get("radius").As<Napi::Number>();
                }
                break;
            }
        }

        xr::GeometryDetectorOptions CreateDetectorOptions(const Napi::Object& object)
        {
            xr::GeometryDetectorOptions options{};
            if (object.Has("updateInterval"))
            {
                options.UpdateInterval = object.Get("updateInterval").As<Napi::Number>();
            }

            if (object.Has("detectionBoundary"))
            {
                const auto& detectionBoundary = object.Get("detectionBoundary").As<Napi::Object>();
                PopulateDetectionBoundary(detectionBoundary, options.DetectionBoundary);
            }

            return options;
        }

        void SetXRInputSourceData(Napi::Object& jsInputSource, xr::System::Session::Frame::InputSource& inputSource)
        {
            auto env = jsInputSource.Env();
            jsInputSource.Set("targetRaySpace", Napi::External<xr::Space>::New(env, &inputSource.AimSpace));
            jsInputSource.Set("gripSpace", Napi::External<xr::Space>::New(env, &inputSource.GripSpace));

            // Don't set hands up unless hand data is supported/available
            if (inputSource.HandTrackedThisFrame || inputSource.JointsTrackedThisFrame)
            {
                auto profiles = Napi::Array::New(env, 2);
                profiles.Set(uint32_t{ 0 }, Napi::String::New(env, "generic-hand-select-grasp"));
                profiles.Set(uint32_t{ 1 }, Napi::String::New(env, "generic-hand-select"));
                jsInputSource.Set("profiles", profiles);

                if (inputSource.JointsTrackedThisFrame)
                {
                    const auto shouldInitHand = !jsInputSource.Has("hand");
                    auto handJointCollection = shouldInitHand ? Napi::Array::New(env, HAND_JOINT_NAMES.size()) : jsInputSource.Get("hand").As<Napi::Array>();
                    if (shouldInitHand)
                    {
                        auto jointGetter = [handJointCollection](const Napi::CallbackInfo& info) -> Napi::Value {
                            return handJointCollection.Get(info[0].As<Napi::String>());
                        };

                        handJointCollection.Set("get", Napi::Function::New(env, jointGetter, "get"));
                        handJointCollection.Set("size", static_cast<int>(HAND_JOINT_NAMES.size()));

                        jsInputSource.Set("hand", handJointCollection);
                    }

                    for (size_t i = 0; i < HAND_JOINT_NAMES.size(); i++)
                    {
                        auto napiJoint = Napi::External<xr::System::Session::Frame::JointSpace>::New(env, &inputSource.HandJoints[i]);
                        handJointCollection.Set(HAND_JOINT_NAMES[i], napiJoint);
                    }
                }
                else
                {
                    // If hand joints aren't available on a hand input, send a null hand object
                    jsInputSource.Set("hand", env.Null());
                }
            }
        }

        Napi::ObjectReference CreateXRInputSource(xr::System::Session::Frame::InputSource& inputSource, Napi::Env& env)
        {
            constexpr std::array<const char*, 2> HANDEDNESS_STRINGS{
                "left",
                "right" };
            constexpr const char* TARGET_RAY_MODE{ "tracked-pointer" };

            auto jsInputSource = Napi::Object::New(env);

            jsInputSource.Set("handedness", Napi::String::New(env, HANDEDNESS_STRINGS[static_cast<size_t>(inputSource.Handedness)]));
            jsInputSource.Set("targetRayMode", TARGET_RAY_MODE);

            auto profiles = Napi::Array::New(env);
            Napi::Value string = Napi::String::New(env, "generic-trigger-squeeze-touchpad-thumbstick");
            uint32_t profileIndex{};
            if (!inputSource.InteractionProfileName.empty())
            {
                profiles.Set(profileIndex++, Napi::String::New(env, inputSource.InteractionProfileName));
            }
            profiles.Set(profileIndex, string);
            jsInputSource.Set("profiles", profiles);

            SetXRInputSourceData(jsInputSource, inputSource);

            return Napi::Persistent(jsInputSource);
        }
    }

    // Using Plugins namespace warning C5046: 'Babylon::`anonymous-namespace'::XRFrame::xxx': Symbol involving type with internal linkage not defined
    namespace Plugins
    {
        void XRSession::Initialize(Napi::Env env)
        {
            Napi::Function func = DefineClass(
                env,
                JS_CLASS_NAME,
                {
                    InstanceAccessor("inputSources", &XRSession::GetInputSources, nullptr),
                    InstanceAccessor("frameRate", &XRSession::GetFrameRate, nullptr),
                    InstanceAccessor("supportedFrameRates", &XRSession::GetSupportedFrameRates, nullptr),
                    InstanceMethod("updateTargetFrameRate", &XRSession::UpdateTargetFrameRate),
                    InstanceMethod("addEventListener", &XRSession::AddEventListener),
                    InstanceMethod("removeEventListener", &XRSession::RemoveEventListener),
                    InstanceMethod("requestReferenceSpace", &XRSession::RequestReferenceSpace),
                    InstanceMethod("updateRenderState", &XRSession::UpdateRenderState),
                    InstanceMethod("requestAnimationFrame", &XRSession::RequestAnimationFrame),
                    InstanceMethod("end", &XRSession::End),
                    InstanceMethod("requestHitTestSource", &XRSession::RequestHitTestSource),
                    InstanceMethod("updateWorldTrackingState", &XRSession::UpdateWorldTrackingState),
                    InstanceMethod("trySetFeaturePointCloudEnabled", &XRSession::TrySetFeaturePointCloudEnabled),
                    InstanceMethod("trySetPreferredPlaneDetectorOptions", &XRSession::TrySetPreferredPlaneDetectorOptions),
                    InstanceMethod("trySetMeshDetectorEnabled", &XRSession::TrySetMeshDetectorEnabled),
                    InstanceMethod("trySetPreferredMeshDetectorOptions", &XRSession::TrySetPreferredMeshDetectorOptions),
                    InstanceMethod("getTrackedImageScores", &XRSession::GetTrackedImageScores),
                });

            env.Global().Set(JS_CLASS_NAME, func);
        }

        Napi::Promise XRSession::CreateAsync(const Napi::CallbackInfo& info, std::shared_ptr<Plugins::NativeXr::Impl> nativeXr)
        {
            LogSessionStage("creating JavaScript session");
            auto sessionInit = info.Length() > 1 && info[1].IsObject()
                ? info[1].As<Napi::Object>()
                : Napi::Object::New(info.Env());
            auto deferred{ Napi::Promise::Deferred::New(info.Env()) };
            Napi::ObjectReference jsSession;
            try
            {
                jsSession = Napi::Persistent(info.Env().Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({info[0], sessionInit}));
            }
            catch (const Napi::Error& error)
            {
                deferred.Reject(error.Value());
                return deferred.Promise();
            }
            auto& session{ *XRSession::Unwrap(jsSession.Value()) };
            session.m_xr = std::move(nativeXr);

            if (sessionInit.Has("trackedImages"))
            {
                const auto napiTrackedImages{ sessionInit.Get("trackedImages").As<Napi::Array>() };
                session.m_imageTrackingRequests.resize(napiTrackedImages.Length());

                // Create the tracked image buffer.
                for (uint32_t idx = 0; idx < napiTrackedImages.Length(); idx++)
                {
                    const auto napiImageRequest{ napiTrackedImages.Get(idx).As<Napi::Object>() };
                    session.m_imageTrackingRequests[idx] = CreateImageTrackingRequest(napiImageRequest);
                }
            }

            LogSessionStage("starting platform session");
            session.m_xr->BeginSessionAsync()
                .then(session.m_runtimeScheduler, arcana::cancellation::none(),
                    [deferred, jsSession{ std::move(jsSession) }, immersive = info[0].ToString().Utf8Value() != "inline", env{ info.Env() }](const arcana::expected<void, std::exception_ptr>& result) {
                if (result.has_error())
                {
                    LogSessionFailure(result.error());
                    deferred.Reject(Napi::Error::New(env, result.error()).Value());
                }
                else
                {
                    LogSessionStage("platform session started");
                    auto& session = *XRSession::Unwrap(jsSession.Value());
                    if (immersive)
                    {
                        session.m_rateSession = session.m_xr->GetPlatformSession();
                        session.m_xr->SetDepthsNarFar(session.m_depthNear, session.m_depthFar);
                        if (const auto platform = session.m_rateSession.lock(); platform && platform->GetDisplayRateControl())
                        {
                            session.m_reportedFrameRate = platform->GetDisplayRateControl()->Nominal.load();
                        }
                    }
                    jsSession.Value().Set("onframeratechange", env.Null());
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                    if (session.m_warpRequested)
                    {
                        const auto platform = session.PlatformSession();
                        session.m_warpEnabled = session.m_webGPUFeatureEnabled && platform &&
                            (platform->SupportsWarpDepth(babylon_wgpu_native_openxr_vk_format("depth24plus")) ||
                             platform->SupportsWarpDepth(babylon_wgpu_native_openxr_vk_format("depth32float")));
                        if (session.m_warpEnabled)
                        {
                            session.m_enabledFeatures.emplace_back("space-warp");
                            auto enabled = jsSession.Value().Get("enabledFeatures").As<Napi::Array>();
                            enabled.Set(enabled.Length(), "space-warp");
                        }
                        else if (session.m_warpRequired)
                        {
                            session.m_xr->EndSessionAsync().then(session.m_runtimeScheduler, arcana::cancellation::none(),
                                [deferred, env](const arcana::expected<void, std::exception_ptr>&) {
                                    auto error = Napi::Error::New(env, "Required XR session feature unavailable: space-warp");
                                    error.Value().Set("name", "NotSupportedError");
                                    deferred.Reject(error.Value());
                                });
                            return;
                        }
                    }
#endif
                    deferred.Resolve(jsSession.Value());
                }
            });
            return deferred.Promise();
        }

        XRSession::XRSession(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<XRSession>{ info }
            , m_runtimeScheduler{ JsRuntime::GetFromJavaScript(info.Env()) }
            , m_xrFrame{ *Plugins::XRFrame::Unwrap(Plugins::XRFrame::New(info)) }
            , m_jsXRFrame{ Napi::Weak(Plugins::XRFrame::WrapForScript(m_xrFrame.Value())) }
            , m_renderState{ Napi::Persistent(Napi::Object::New(info.Env())) }
            , m_jsInputSources{ Napi::Persistent(Napi::Array::New(info.Env())) }
        {
            // Currently only immersive VR and immersive AR are supported.
            const auto sessionMode = ParseXRSessionMode(info[0].As<Napi::String>().Utf8Value());
            if (!sessionMode) throw Napi::TypeError::New(info.Env(), "Unknown XR session mode");
            if (sessionMode == XRSessionMode::Inline
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                || sessionMode != XRSessionMode::ImmersiveVr
#endif
            )
            {
                auto error = Napi::Error::New(info.Env(), "XR session mode unavailable on this backend");
                error.Value().Set("name", "NotSupportedError");
                throw error;
            }
            const bool immersiveAr = sessionMode == XRSessionMode::ImmersiveAr;

            auto performance = info.Env().Global().Get("performance");
            if (performance.IsObject())
            {
                auto now = performance.As<Napi::Object>().Get("now");
                if (now.IsFunction())
                {
                    m_originMilliseconds = now.As<Napi::Function>().Call(performance, {}).ToNumber().DoubleValue();
                    m_clockOrigin = std::chrono::steady_clock::now();
                }
            }

            m_renderState.Set("depthNear", Napi::Number::New(info.Env(), m_depthNear));
            m_renderState.Set("depthFar", Napi::Number::New(info.Env(), m_depthFar));
            m_renderState.Set("baseLayer", info.Env().Null());
            m_renderState.Set("layers", Napi::Array::New(info.Env()));

            auto session = info.This().As<Napi::Object>();
            // Keep the ownership cycle in JS, where it is collectible. A pending animation callback roots its session.
            const auto frame = m_xrFrame.Value();
            session.DefineProperty(Napi::PropertyDescriptor::Value(Napi::Symbol::New(info.Env(), "XRFrame"), frame));
            const auto scriptFrame = m_jsXRFrame.Value();
            if (!frame.StrictEquals(scriptFrame))
            {
                session.DefineProperty(Napi::PropertyDescriptor::Value(Napi::Symbol::New(info.Env(), "XRFrame wrapper"), scriptFrame));
            }
            frame.DefineProperty(Napi::PropertyDescriptor::Value("session", session, napi_enumerable));
            session.Set("renderState", m_renderState.Value());
            session.Set("environmentBlendMode", immersiveAr ? "alpha-blend" : "opaque");
            session.Set("interactionMode", immersiveAr ? "screen-space" : "world-space");
            session.Set("visibilityState", "visible");

            m_enabledFeatures = {XRReferenceSpaceType::VIEWER, XRReferenceSpaceType::LOCAL};
            if (info.Length() > 1 && info[1].IsObject())
            {
                const auto sessionInit = info[1].As<Napi::Object>();
                for (const auto* featureListName : {"requiredFeatures", "optionalFeatures"})
                {
                    if (!sessionInit.Has(featureListName) || !sessionInit.Get(featureListName).IsArray())
                    {
                        continue;
                    }

                    const auto featureList = sessionInit.Get(featureListName).As<Napi::Array>();
                    for (uint32_t index = 0; index < featureList.Length(); ++index)
                    {
                        if (!featureList.Get(index).IsString())
                        {
                            continue;
                        }

                        const auto feature = featureList.Get(index).As<Napi::String>().Utf8Value();
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                        if (feature == "space-warp")
                        {
                            m_warpRequested = true;
                            m_warpRequired |= std::string_view{featureListName} == "requiredFeatures";
                            continue;
                        }
#endif
                        bool supported = IsXRFeatureDescriptor(feature);
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                        // Only grant features delivered by this OpenXR backend.
                        // An advertised runtime extension alone is insufficient.
                        supported = feature == "viewer" || feature == "local" || feature == "local-floor" ||
                            feature == "layers" || feature == "webgpu";
#else
                        supported = supported || (immersiveAr && feature == "image-tracking");
#endif
                        if (!supported)
                        {
                            if (std::string_view{featureListName} == "requiredFeatures")
                            {
                                auto error = Napi::Error::New(info.Env(), "Required XR session feature unavailable: " + feature);
                                error.Value().Set("name", "NotSupportedError");
                                throw error;
                            }
                            continue;
                        }

                        if (std::find(m_enabledFeatures.begin(), m_enabledFeatures.end(), feature) == m_enabledFeatures.end())
                        {
                            m_enabledFeatures.push_back(feature);
                        }
                        m_webGPUFeatureEnabled = m_webGPUFeatureEnabled || feature == "webgpu";
                    }
                }
            }
            auto enabledFeatures = Napi::Array::New(info.Env(), m_enabledFeatures.size());
            for (uint32_t index = 0; index < m_enabledFeatures.size(); ++index)
            {
                enabledFeatures.Set(index, m_enabledFeatures[index]);
            }
            session.Set("enabledFeatures", enabledFeatures);
        }

        bool XRSession::IsWebGPUFeatureEnabled() const
        {
            return m_webGPUFeatureEnabled;
        }

        bool XRSession::IsLayersFeatureEnabled() const
        {
            return std::find(m_enabledFeatures.begin(), m_enabledFeatures.end(), "layers") != m_enabledFeatures.end();
        }

        bool XRSession::IsCurrentAnimationFrame(const Napi::Object& frame) const
        {
            return m_inAnimationCallback && m_jsXRFrame.Value().StrictEquals(frame);
        }

        bool XRSession::IsCurrentView(const Napi::Object& view) const
        {
            auto* nativeView = XRView::Unwrap(view);
            return m_inAnimationCallback && nativeView && nativeView->BelongsTo(m_renderStateFrame.lock());
        }

        bool XRSession::IsProjectionLayerActive(const Babylon::XRProjectionLayer& layer) const
        {
            return m_activeProjectionLayer == &layer;
        }

#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
        void XRSession::UpdateProjectionFoveation(const Babylon::XRProjectionLayer& layer)
        {
            const auto* next = m_pendingRenderState.IsEmpty() ? m_activeProjectionLayer :
                m_pendingProjectionLayer.IsEmpty() ? nullptr : Babylon::XRProjectionLayer::Unwrap(m_pendingProjectionLayer.Value());
            if (next == &layer)
            {
                if (auto platform = PlatformSession()) platform->SetFoveation(layer.Foveation());
            }
        }
#endif

        std::optional<XRGPUSubImageData> XRSession::GetWebGPUSubImage(uint32_t viewIndex) const
        {
            auto image = m_xr->GetWebGPUSubImage(viewIndex);
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
            if (image && m_warpEnabled && m_activeProjectionLayer && !m_activeProjectionLayer->DepthStencilFormat().empty())
                m_xr->AcquireWarpAttachments(*image, m_activeProjectionLayer->DepthStencilFormat());
#endif
            return image;
        }

        const char* XRSession::GetWebGPUColorFormat() const
        {
            return m_xr->GetWebGPUColorFormat();
        }

        uint32_t XRSession::GetWebGPUTextureWidth() const
        {
            return m_xr->GetWebGPUTextureWidth();
        }

        uint32_t XRSession::GetWebGPUTextureHeight() const
        {
            return m_xr->GetWebGPUTextureHeight();
        }

        uint32_t XRSession::GetWebGPUTextureArrayLength() const
        {
            return m_xr->GetWebGPUTextureArrayLength();
        }

        void XRSession::InitializeXrLayer(Napi::Object layer)
        {
            // NOTE: We currently only support rendering to the entire frame. Because the following values
            // are only used in the context of each other, width and hight as used here don't need to have
            // anything to do with actual pixel widths. This behavior is permitted by the draft WebXR spec,
            // which states that the, "exact interpretation of the viewport values depends on the conventions
            // of the graphics API the viewport is associated with." Since Babylon.js is here doing the
            // the interpretation for our graphics API, we are able to provide Babylon.js with simple values
            // that will communicate the correct behavior. In theory, for partial texture rendering, the
            // only part of this that will need to be fixed is the viewport (the layer will need one for
            // each view, not just the one that currently exists).
            // Spec reference: https://immersive-web.github.io/webxr/#dom-xrviewport-width
            constexpr size_t WIDTH = 1;
            constexpr size_t HEIGHT = 1;

            auto env = layer.Env();
            auto viewport = Napi::Object::New(env);
            viewport.Set("x", Napi::Value::From(env, 0));
            viewport.Set("y", Napi::Value::From(env, 0));
            viewport.Set("width", Napi::Value::From(env, WIDTH));
            viewport.Set("height", Napi::Value::From(env, HEIGHT));
            layer.Set("viewport", viewport);

            layer.Set("framebufferWidth", Napi::Value::From(env, WIDTH));
            layer.Set("framebufferHeight", Napi::Value::From(env, HEIGHT));
        }

        Napi::Value XRSession::GetRenderTargetForEye(const std::string& eye) const
        {
            return m_xr->GetRenderTargetForViewIndex(XREye::EyeToIndex(eye));
        }

        void XRSession::SetRenderTextureFunctions(const Napi::Function& createFunction, const Napi::Function& destroyFunction)
        {
            return m_xr->SetRenderTextureFunctions(createFunction, destroyFunction);
        }

        Napi::Value XRSession::DeclareNativeAnchor(const Napi::Env& env, xr::NativeAnchorPtr nativeAnchor)
        {
            return m_xrFrame.DeclareNativeAnchor(env, nativeAnchor);
        }

        Napi::Value XRSession::RequestReferenceSpace(const Napi::CallbackInfo& info)
        {
            return XRReferenceSpace::Request(info, m_enabledFeatures);
        }

#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
        XRGPUSubImageData XRSession::AcquireLayerTexture(uint64_t id, uint32_t width, uint32_t height, uint32_t slices,
            const std::string& format, uint32_t usage, const std::string& depth)
        {
            return m_xr->AcquireLayerTexture(id,width,height,slices,format,usage,depth);
        }
        bool XRSession::IsCompositionLayerActive(const Napi::Object& object) const
        {
            const auto layers = m_renderState.Get("layers").As<Napi::Array>();
            for (uint32_t i=0; i<layers.Length(); ++i) if (object.StrictEquals(layers.Get(i))) return true;
            return false;
        }
#endif
        Napi::Value XRSession::UpdateRenderState(const Napi::CallbackInfo& info)
        {
            if (info.Length() == 0 || !info[0].IsObject())
            {
                throw std::runtime_error{"XRSession.updateRenderState requires an XRRenderStateInit object."};
            }

            const auto update = info[0].As<Napi::Object>();
            auto depthNear = m_depthNear;
            auto depthFar = m_depthFar;
            bool depthsChanged{};
            if (update.Has("depthNear") && !update.Get("depthNear").IsUndefined())
            {
                if (!update.Get("depthNear").IsNumber())
                {
                    throw std::runtime_error{"XRSession.updateRenderState depthNear must be a number."};
                }
                depthNear = update.Get("depthNear").As<Napi::Number>().FloatValue();
                depthsChanged = true;
            }
            if (update.Has("depthFar") && !update.Get("depthFar").IsUndefined())
            {
                if (!update.Get("depthFar").IsNumber())
                {
                    throw std::runtime_error{"XRSession.updateRenderState depthFar must be a number."};
                }
                depthFar = update.Get("depthFar").As<Napi::Number>().FloatValue();
                depthsChanged = true;
            }
            if (depthNear <= 0.0f || depthFar <= depthNear)
            {
                throw std::runtime_error{"XRSession.updateRenderState requires 0 < depthNear < depthFar."};
            }

            const auto hasLayers = update.Has("layers") && !update.Get("layers").IsUndefined();
            Babylon::XRProjectionLayer* projectionLayerCandidate{};
            uint32_t projectionIndex{};
            if (hasLayers)
            {
                if (!update.Get("layers").IsArray())
                {
                    throw std::runtime_error{"XRSession.updateRenderState layers must be an array."};
                }

                const auto layers = update.Get("layers").As<Napi::Array>();
                uint32_t headerCount{};
                for (uint32_t i=0; i<layers.Length(); ++i)
                {
                    const auto layerValue = layers.Get(i);
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                    if (auto* shaped = XRShapedLayer::From(layerValue))
                    {
                        if (!shaped->Valid(*this))
                        {
                            auto error = Napi::Error::New(info.Env(),"Destroyed or foreign composition layer");
                            error.Value().Set("name","InvalidStateError");
                            throw error;
                        }
                        for (uint32_t j=0; j<i; ++j)
                            if (layerValue.StrictEquals(layers.Get(j))) throw Napi::TypeError::New(info.Env(),"Duplicate layer");
                        headerCount += shaped->Eyes();
                        continue;
                    }
#endif
                    if (projectionLayerCandidate) throw std::runtime_error{"Only one projection layer is supported."};
                    ++headerCount;
                    projectionIndex = i;
                    if (!layerValue.IsObject() || !IsInstanceOfGlobalClass(layerValue.As<Napi::Object>(), "XRProjectionLayer"))
                    {
                        throw std::runtime_error{"NativeXR renderState.layers only supports XRProjectionLayer objects."};
                    }

                    projectionLayerCandidate = Babylon::XRProjectionLayer::Unwrap(layerValue.As<Napi::Object>());
                    if (projectionLayerCandidate == nullptr || !projectionLayerCandidate->BelongsTo(*this))
                    {
                        throw std::runtime_error{"XRSession.updateRenderState received a projection layer from another XRSession."};
                    }
                }
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                if (auto platform = PlatformSession(); platform && headerCount > platform->MaxLayerCount())
                    throw std::runtime_error{"OpenXR runtime layer count exceeded"};
#endif
            }

            if (m_pendingRenderState.IsEmpty())
            {
                m_pendingRenderState = Napi::Persistent(Napi::Object::New(info.Env()));
                for (const char* key : {"layers", "baseLayer", "depthNear", "depthFar", "inlineVerticalFieldOfView"})
                {
                    if (m_renderState.Value().Has(key))
                    {
                        m_pendingRenderState.Set(key, m_renderState.Get(key));
                    }
                }
                if (!m_currentProjectionLayer.IsEmpty())
                {
                    m_pendingProjectionLayer = Napi::Persistent(m_currentProjectionLayer.Value());
                }
            }
            if (depthsChanged)
            {
                m_xr->SetDepthsNarFar(depthNear, depthFar);
                m_depthNear = depthNear;
                m_depthFar = depthFar;
                m_pendingRenderState.Set("depthNear", Napi::Number::New(info.Env(), m_depthNear));
                m_pendingRenderState.Set("depthFar", Napi::Number::New(info.Env(), m_depthFar));
            }

            if (hasLayers)
            {
                const auto layers = update.Get("layers").As<Napi::Array>();
                auto snapshot = Napi::Array::New(info.Env(), layers.Length());
                m_pendingProjectionLayer.Reset();
                for (uint32_t i=0; i<layers.Length(); ++i) snapshot.Set(i,layers.Get(i));
                auto object = info.Env().Global().Get("Object").As<Napi::Object>();
                object.Get("freeze").As<Napi::Function>().Call(object,{snapshot});
                if (projectionLayerCandidate)
                {
                    auto layer = snapshot.Get(projectionIndex).As<Napi::Object>();
                    m_pendingProjectionLayer = Napi::Persistent(layer);
                }
                // The compositor supplies its own motion-sized depth image for warp.
                m_xr->SetProjectionAttachments(projectionLayerCandidate ? std::optional{m_warpEnabled ? std::string{} : projectionLayerCandidate->DepthStencilFormat()} : std::nullopt,
                    projectionLayerCandidate ? projectionLayerCandidate->TextureUsage() : 0);
                m_pendingRenderState.Set("layers", snapshot);
                m_pendingRenderState.Set("baseLayer", info.Env().Null());
            }
            else if (update.Has("baseLayer"))
            {
                const auto baseLayer = update.Get("baseLayer");
                m_pendingRenderState.Set("baseLayer", baseLayer.IsUndefined() ? info.Env().Null() : baseLayer);
                m_pendingRenderState.Set("layers", Napi::Array::New(info.Env()));
                m_pendingProjectionLayer.Reset();
                m_xr->SetProjectionAttachments(std::nullopt, 0);
            }

            if (update.Has("inlineVerticalFieldOfView"))
            {
                m_pendingRenderState.Set("inlineVerticalFieldOfView", update.Get("inlineVerticalFieldOfView"));
            }
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
            if (hasLayers || update.Has("baseLayer"))
            {
                if (auto session = m_xr->GetPlatformSession().lock())
                {
                    session->SetProjectionScale(projectionLayerCandidate ? projectionLayerCandidate->ScaleFactor() : 1.0);
                    session->SetFoveation(projectionLayerCandidate ? projectionLayerCandidate->Foveation() : 0);
                }
            }
#endif

            return info.Env().Undefined();
        }

        void XRSession::ProcessEyeInputSource(const xr::System::Session::Frame& frame, Napi::Env env)
        {
            if (frame.EyeTrackerSpace.has_value() && m_jsEyeTrackedSource.IsEmpty())
            {
                m_jsEyeTrackedSource = Napi::Persistent(Napi::Object::New(env));
                m_jsEyeTrackedSource.Set("gazeSpace", Napi::External<xr::Space>::New(env, &frame.EyeTrackerSpace.value()));

                for (const auto& [name, callback] : m_eventNamesAndCallbacks)
                {
                    if (name == JS_EVENT_NAME_EYE_TRACKING_START)
                    {
                        Napi::Object obj = m_jsEyeTrackedSource.Value();
                        callback.Call({ obj });
                    }
                }
            }
            else if (!frame.EyeTrackerSpace.has_value() && !m_jsEyeTrackedSource.IsEmpty())
            {
                for (const auto& [name, callback] : m_eventNamesAndCallbacks)
                {
                    if (name == JS_EVENT_NAME_EYE_TRACKING_END)
                    {
                        callback.Call({});
                    }
                }

                m_jsEyeTrackedSource.Reset();
            }
        }

        void XRSession::ProcessControllerInputSources(const xr::System::Session::Frame& frame, Napi::Env env)
        {
            // Figure out the new state.
            std::set<xr::System::Session::Frame::InputSource::Identifier> added{};
            std::set<xr::System::Session::Frame::InputSource::Identifier> current{};
            std::set<xr::System::Session::Frame::InputSource::Identifier> removed{};

            std::vector<xr::System::Session::Frame::InputSource::Identifier> selectStarts{};
            std::vector<xr::System::Session::Frame::InputSource::Identifier> selectEnds{};
            std::vector<xr::System::Session::Frame::InputSource::Identifier> squeezeStarts{};
            std::vector<xr::System::Session::Frame::InputSource::Identifier> squeezeEnds{};

            // Process the controller-based input sources
            for (auto& inputSource : frame.InputSources)
            {
                if (!inputSource.TrackedThisFrame)
                {
                    continue;
                }

                current.insert(inputSource.ID);

                auto inputSourceFound = m_idToInputSource.find(inputSource.ID);
                if (inputSourceFound == m_idToInputSource.end())
                {
                    // Create the new input source, which will have the correct spaces associated with it.
                    m_idToInputSource.insert({ inputSource.ID, CreateXRInputSource(inputSource, env) });

                    // Now that input Source is created, create a gamepad object if enabled for the input source. Hand data also uses the gamepad component.
                    inputSourceFound = m_idToInputSource.find(inputSource.ID);
                    if (inputSource.GamepadTrackedThisFrame || inputSource.HandTrackedThisFrame)
                    {
                        auto inputSourceVal = inputSourceFound->second.Value();
                        CreateXRGamepadObject(inputSourceVal, inputSource);
                    }

                    added.insert(inputSource.ID);
                }
                else
                {
                    // Ensure the correct spaces are associated with the existing input source.
                    auto inputSourceVal = inputSourceFound->second.Value();
                    SetXRInputSourceData(inputSourceVal, inputSource);

                    // inputSource already exists, find the corresponding gamepad object if enabled and set to correct values
                    if (inputSourceVal.Has("gamepad"))
                    {
                        auto gamepadObject = inputSourceVal.Get("gamepad").As<Napi::Object>();
                        SetXRGamepadObjectData(inputSourceVal, gamepadObject, inputSource);
                    }
                }

                // Handle gestures. Sources that do not support a gesture will always return false
                const bool isSelected = inputSource.GamepadObject.Buttons[0].Pressed;
                const bool isSqueezed = inputSource.GamepadObject.Buttons[1].Pressed;
                const bool wasSelected = std::find(m_activeSelects.begin(), m_activeSelects.end(), inputSource.ID) != m_activeSelects.end();
                const bool wasSqueezed = std::find(m_activeSqueezes.begin(), m_activeSqueezes.end(), inputSource.ID) != m_activeSqueezes.end();

                if (isSelected && !wasSelected)
                {
                    selectStarts.push_back(inputSource.ID);
                }
                else if (!isSelected && wasSelected)
                {
                    selectEnds.push_back(inputSource.ID);
                }

                if (isSqueezed && !wasSqueezed)
                {
                    squeezeStarts.push_back(inputSource.ID);
                }
                else if (!isSqueezed && wasSqueezed)
                {
                    squeezeEnds.push_back(inputSource.ID);
                }
            }
            for (const auto& [id, ref] : m_idToInputSource)
            {
                if (current.find(id) == current.end())
                {
                    // Process select and squeeze for lost sources before we send the source lost event
                    auto inputSourceVal = ref.Value();
                    Napi::Object inputSourceEvent = GenerateXRInputSourceEvent(inputSourceVal, env);
                    const auto& activeSelectIter = std::find(m_activeSelects.begin(), m_activeSelects.end(), id);
                    const auto& activeSqueezeIter = std::find(m_activeSqueezes.begin(), m_activeSqueezes.end(), id);

                    if (activeSelectIter != m_activeSelects.end())
                    {
                        FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SELECT_END);
                        m_activeSelects.erase(activeSelectIter);
                    }

                    if (activeSqueezeIter != m_activeSqueezes.end())
                    {
                        FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SQUEEZE_END);
                        m_activeSqueezes.erase(activeSqueezeIter);
                    }
                    // Do not update space association since said spaces no longer exist.
                    removed.insert(id);
                }
            }

            // Only need to do more if there's been a change. Note that this block of code assumes
            // that ALL known input sources -- including ones added AND REMOVED this frame -- are
            // currently up-to-date and accessible though m_idToInputSource.
            if (added.size() > 0 || removed.size() > 0)
            {
                // Update the input sources array.
                auto jsCurrent = Napi::Array::New(env);
                for (const auto id : current)
                {
                    jsCurrent.Set(jsCurrent.Length(), m_idToInputSource[id].Value());
                }
                m_jsInputSources = Napi::Persistent(jsCurrent);

                // Create and send the sources changed event.
                Napi::Array jsAdded = Napi::Array::New(env);
                for (const auto id : added)
                {
                    jsAdded.Set(jsAdded.Length(), m_idToInputSource[id].Value());
                }
                Napi::Array jsRemoved = Napi::Array::New(env);
                for (const auto id : removed)
                {
                    jsRemoved.Set(jsRemoved.Length(), m_idToInputSource[id].Value());
                }
                auto sourcesChangeEvent = Napi::Object::New(env);
                sourcesChangeEvent.Set("added", jsAdded);
                sourcesChangeEvent.Set("removed", jsRemoved);
                for (const auto& [name, callback] : m_eventNamesAndCallbacks)
                {
                    if (name == JS_EVENT_NAME_INPUT_SOURCES_CHANGE)
                    {
                        callback.Call({ sourcesChangeEvent });
                    }
                }

                // Finally, remove the removed.
                for (const auto id : removed)
                {
                    m_idToInputSource.erase(id);
                }
            }

            // Process active selects after firing off any new source added events
            UpdateInputSourceEventValues(selectStarts, selectEnds, squeezeStarts, squeezeEnds, env);
        }

        Napi::Value XRSession::RequestAnimationFrame(const Napi::CallbackInfo& info)
        {
            Napi::Function callback{ info[0].As<Napi::Function>() };

            {
                // Mirrors the NativeXrImpl trace: this is the JS end of the XR
                // frame loop, so a run that stops here never asked for a frame.
                static const bool trace = std::getenv("BABYLON_NATIVE_XR_TRACE") != nullptr;
                if (trace)
                {
                    std::fputs("XRTRACE XRSession::RequestAnimationFrame\n", stderr);
                    std::fflush(stderr);
                }
            }

            m_xr->ScheduleFrame([
                this,
                callbackPtr{ std::make_shared<Napi::FunctionReference>(Napi::Persistent(callback)) },
                sessionPtr{ std::make_shared<Napi::ObjectReference>(Napi::Persistent(info.This().As<Napi::Object>())) }](const std::shared_ptr<const xr::System::Session::Frame>& frame) {
                // Multiple callbacks in one device frame must all observe the same render state.
                if (m_renderStateFrame.lock() != frame)
                {
                    m_renderStateFrame = frame;
                    const auto displayTime = frame->PredictedDisplayTime.value_or(std::chrono::steady_clock::now());
                    m_frameMilliseconds = m_originMilliseconds + std::chrono::duration<double, std::milli>(displayTime - m_clockOrigin).count();
                    if (!m_pendingRenderState.IsEmpty())
                    {
                        m_renderState = std::move(m_pendingRenderState);
                        m_currentProjectionLayer = std::move(m_pendingProjectionLayer);
                        m_activeProjectionLayer = m_currentProjectionLayer.IsEmpty() ? nullptr : Babylon::XRProjectionLayer::Unwrap(m_currentProjectionLayer.Value());
                        sessionPtr->Value().Set("renderState", m_renderState.Value());
                    }
                }
                NotifyFrameRateChange(sessionPtr->Value());
                m_xrFrame.Update(Env(), frame, m_frameMilliseconds);
                m_xrFrame.SetPoseAccess(true);
                const auto endPoseAccess = gsl::finally([this] { m_xrFrame.SetPoseAccess(false); });
                ProcessEyeInputSource(*frame.get(), Env());
                ProcessControllerInputSources(*frame.get(), Env());

                if (m_imageTrackingRequests.size() > 0)
                {
                    // Kick off creation of the augmented image database.
                    m_xr->CreateAugmentedImageDatabase(m_imageTrackingRequests);

                    // Clean up image tracking requests
                    m_imageTrackingRequests.clear();
                }

                m_inAnimationCallback = true;
                const auto resetCallback = gsl::finally([this] { m_inAnimationCallback = false; });
                callbackPtr->Value().Call({ Napi::Number::New(Env(), m_frameMilliseconds), m_jsXRFrame.Value() });
#if defined(__ANDROID__) && defined(BABYLON_NATIVE_XR_OPENXR)
                if (m_warpEnabled && m_activeProjectionLayer)
                {
                    m_xr->SetWarpDelta(m_activeProjectionLayer->DeltaPose());
                }
                if (auto platform = PlatformSession(); platform && m_renderState.Get("layers").IsArray())
                {
                    auto layers = m_renderState.Get("layers").As<Napi::Array>();
                    std::vector<xr::LayerPlacement> order;
                    for (uint32_t i=0; i<layers.Length(); ++i)
                    {
                        if (auto* shaped = XRShapedLayer::From(layers.Get(i)))
                        {
                            if (shaped->Id()) order.push_back(shaped->Placement());
                        }
                        else if (m_activeProjectionLayer)
                        {
                            order.push_back(xr::LayerPlacement::Projection(m_activeProjectionLayer->BlendsSourceAlpha()));
                        }
                    }
                    if (!m_renderState.Get("baseLayer").IsNull() && layers.Length() == 0) order.push_back(xr::LayerPlacement::Projection(false));
                    platform->SetLayerOrder(std::move(order));
                }
#endif
            });

            // The return value should be a request ID to allow for requesting cancellation, this is unused in Babylon.js currently.
            return Napi::Value::From(info.Env(), m_nextAnimationHandle++);
        }

        void XRSession::UpdateWorldTrackingState(const Napi::CallbackInfo& info)
        {
            auto optionsObj = info[0].As<Napi::Object>();
            if (optionsObj.Has("planeDetectionState"))
            {
                bool planeDetectionEnabled = optionsObj.Get("planeDetectionState").As<Napi::Object>().Get("enabled").ToBoolean().Value();
                m_xr->SetPlaneDetectionEnabled(planeDetectionEnabled);
            }
        }

        Napi::Object XRSession::GenerateXRInputSourceEvent(Napi::Object& inputSource, Napi::Env env)
        {
            auto inputSourceEvent = Napi::Object::New(env);
            inputSourceEvent.Set("frame", m_jsXRFrame.Value());
            inputSourceEvent.Set("inputSource", inputSource);

            return inputSourceEvent;
        }

        void XRSession::FireInputSourceEvent(Napi::Object& inputSourceEvent, std::string eventName)
        {
            for (const auto& [name, callback] : m_eventNamesAndCallbacks)
            {
                if (name == eventName)
                {
                    callback.Call({ inputSourceEvent });
                }
            }
        }

        void XRSession::UpdateInputSourceEventValues(
            const std::vector<xr::System::Session::Frame::InputSource::Identifier>& selectStarts,
            const std::vector<xr::System::Session::Frame::InputSource::Identifier>& selectEnds,
            const std::vector<xr::System::Session::Frame::InputSource::Identifier>& squeezeStarts,
            const std::vector<xr::System::Session::Frame::InputSource::Identifier>& squeezeEnds,
            Napi::Env env)
        {
            for (const auto& id : selectStarts)
            {
                auto inputSourceIter = m_idToInputSource.find(id);
                if (inputSourceIter != m_idToInputSource.end())
                {
                    auto inputSourceVal = inputSourceIter->second.Value();
                    Napi::Object inputSourceEvent = GenerateXRInputSourceEvent(inputSourceVal, env);
                    FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SELECT_START);

                    m_activeSelects.push_back(id);
                }
            }

            for (const auto& id : squeezeStarts)
            {
                auto inputSourceIter = m_idToInputSource.find(id);
                if (inputSourceIter != m_idToInputSource.end())
                {
                    auto inputSourceVal = inputSourceIter->second.Value();
                    Napi::Object inputSourceEvent = GenerateXRInputSourceEvent(inputSourceVal, env);
                    FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SQUEEZE_START);

                    m_activeSqueezes.push_back(id);
                }
            }

            for (const auto& id : selectEnds)
            {
                auto inputSourceIter = m_idToInputSource.find(id);
                if (inputSourceIter != m_idToInputSource.end())
                {
                    auto inputSourceVal = inputSourceIter->second.Value();
                    Napi::Object inputSourceEvent = GenerateXRInputSourceEvent(inputSourceVal, env);

                    // WebXR API dictates the select event fires before the select end
                    FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SELECT);
                    FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SELECT_END);

                    m_activeSelects.erase(std::find(m_activeSelects.begin(), m_activeSelects.end(), id));
                }
            }

            for (const auto& id : squeezeEnds)
            {
                auto inputSourceIter = m_idToInputSource.find(id);
                if (inputSourceIter != m_idToInputSource.end())
                {
                    auto inputSourceVal = inputSourceIter->second.Value();
                    Napi::Object inputSourceEvent = GenerateXRInputSourceEvent(inputSourceVal, env);

                    // WebXR API dictates the squeeze event fires before the squeeze end
                    FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SQUEEZE);
                    FireInputSourceEvent(inputSourceEvent, JS_EVENT_NAME_SQUEEZE_END);

                    m_activeSqueezes.erase(std::find(m_activeSqueezes.begin(), m_activeSqueezes.end(), id));
                }
            }
        }

        Napi::Value XRSession::TrySetFeaturePointCloudEnabled(const Napi::CallbackInfo& info)
        {
            bool featurePointCloudEnabled = info[0].ToBoolean();
            bool enabled = m_xr->TrySetFeaturePointCloudEnabled(featurePointCloudEnabled);

            return Napi::Value::From(info.Env(), enabled);
        }

        Napi::Value XRSession::GetFrameRate(const Napi::CallbackInfo& info)
        {
            auto session = m_rateSession.lock();
            if (session && session->GetDisplayRateControl())
            {
                return Napi::Number::New(info.Env(), session->GetDisplayRateControl()->Nominal.load());
            }
            return info.Env().Null();
        }

        Napi::Value XRSession::GetSupportedFrameRates(const Napi::CallbackInfo& info)
        {
            auto session = m_rateSession.lock();
            if (!session || !session->GetDisplayRateControl()) return info.Env().Null();
            const auto& supported = session->GetDisplayRateControl()->Supported;
            auto rates = Napi::Float32Array::New(info.Env(), supported.size());
            std::copy(supported.begin(), supported.end(), rates.Data());
            return rates;
        }

        Napi::Value XRSession::UpdateTargetFrameRate(const Napi::CallbackInfo& info)
        {
            const auto env = info.Env();
            const auto deferred = Napi::Promise::Deferred::New(env);
            auto reject = [deferred](Napi::Env env, const char* name, const char* message) {
                auto ctor = env.Global().Get("DOMException");
                if (ctor.IsFunction())
                {
                    deferred.Reject(ctor.As<Napi::Function>().New({Napi::String::New(env, message), Napi::String::New(env, name)}));
                }
                else
                {
                    auto error = Napi::Error::New(env, message).Value();
                    error.Set("name", name);
                    deferred.Reject(error);
                }
            };
            auto session = m_rateSession.lock();
            if (!m_endPromise.IsEmpty() || !session || !session->GetDisplayRateControl())
            {
                reject(env, "InvalidStateError", "The XR session has no active display rate control.");
                return deferred.Promise();
            }
            float rate;
            try
            {
                rate = info[0].ToNumber().FloatValue();
            }
            catch (const Napi::Error& error)
            {
                deferred.Reject(error.Value());
                return deferred.Promise();
            }
            const auto& rates = session->GetDisplayRateControl()->Supported;
            if (std::find(rates.begin(), rates.end(), rate) == rates.end())
            {
                reject(env, "TypeError", "The requested rate is not in XRSession.supportedFrameRates.");
                return deferred.Promise();
            }
            auto keepAlive = std::make_shared<Napi::ObjectReference>(Napi::Persistent(info.This().As<Napi::Object>()));
            JsRuntime::GetFromJavaScript(env).Dispatch([this, deferred, reject, rate, keepAlive](Napi::Env env) {
                (void)keepAlive;
                auto session = m_rateSession.lock();
                const auto* control = session ? session->GetDisplayRateControl() : nullptr;
                if (!m_endPromise.IsEmpty() || !control || !control->Request(rate))
                {
                    reject(env, "InvalidStateError", "The XR runtime could not accept the display rate request.");
                    return;
                }
                deferred.Resolve(env.Undefined());
            });
            return deferred.Promise();
        }

        void XRSession::NotifyFrameRateChange(const Napi::Object& session)
        {
            const auto platform = m_rateSession.lock();
            if (!m_endPromise.IsEmpty() || !platform || !platform->GetDisplayRateControl()) return;
            const float rate = platform->GetDisplayRateControl()->Nominal.load();
            if (rate == m_reportedFrameRate) return;
            m_reportedFrameRate = rate;
            auto event = Napi::Object::New(Env());
            event.Set("type", "frameratechange");
            event.Set("session", session);
            event.Set("target", session);
            std::vector<Napi::Function> callbacks;
            for (const auto& [name, listener] : m_eventNamesAndCallbacks)
            {
                if (name == "frameratechange") callbacks.emplace_back(listener.Value());
            }
            const auto onChange = session.Get("onframeratechange");
            if (onChange.IsFunction()) callbacks.emplace_back(onChange.As<Napi::Function>());
            for (const auto& callback : callbacks) callback.Call(session, {event});
        }

        Napi::Value XRSession::End(const Napi::CallbackInfo& info)
        {
            if (!m_endPromise.IsEmpty())
            {
                return m_endPromise.Value();
            }

            auto deferred{ Napi::Promise::Deferred::New(info.Env()) };
            m_endPromise = Napi::Persistent(deferred.Promise().As<Napi::Object>());
            m_xr->EndSessionAsync().then(m_runtimeScheduler, arcana::cancellation::none(),
                [this, deferred, jsSession{Napi::Persistent(info.This().As<Napi::Object>())}](const arcana::expected<void, std::exception_ptr>& result) {
                    if (result.has_error())
                    {
                        deferred.Reject(Napi::Error::New(Env(), result.error()).Value());
                        return;
                    }

                    auto session = jsSession.Value();
                    auto event = Napi::Object::New(Env());
                    event.Set("type", JS_EVENT_NAME_END);
                    event.Set("session", session);
                    event.Set("target", session);
                    event.Set("currentTarget", session);
                    std::vector<Napi::Function> callbacks;
                    for (const auto& [name, callback] : m_eventNamesAndCallbacks)
                    {
                        if (name == JS_EVENT_NAME_END)
                        {
                            callbacks.push_back(callback.Value());
                        }
                    }
                    auto onEnd = session.Get("onend");
                    if (onEnd.IsFunction()) callbacks.push_back(onEnd.As<Napi::Function>());
                    deferred.Resolve(Env().Undefined());
                    for (const auto& callback : callbacks) callback.Call(session, {event});
                });

            return deferred.Promise();
        }

        Napi::Value XRSession::RequestHitTestSource(const Napi::CallbackInfo& info)
        {
            auto deferred = Napi::Promise::Deferred::New(info.Env());
            deferred.Resolve(XRHitTestSource::New(info));
            return deferred.Promise();
        }

        Napi::Value XRSession::TrySetPreferredPlaneDetectorOptions(const Napi::CallbackInfo& info)
        {
            if (info.Length() != 1 ||
                !info[0].IsObject())
            {
                throw std::exception(/*invalid arguments*/);
            }

            const auto options = CreateDetectorOptions(info[0].As<Napi::Object>());
            const auto result = m_xr->TrySetPreferredPlaneDetectorOptions(options);
            return Napi::Value::From(info.Env(), result);
        }

        Napi::Value XRSession::TrySetMeshDetectorEnabled(const Napi::CallbackInfo& info)
        {
            if (info.Length() != 1 ||
                !info[0].IsBoolean())
            {
                throw std::exception(/*invalid arguments*/);
            }

            const auto enabled = info[0].As<Napi::Boolean>();
            const auto result = m_xr->TrySetMeshDetectorEnabled(enabled);
            return Napi::Value::From(info.Env(), result);
        }

        Napi::Value XRSession::TrySetPreferredMeshDetectorOptions(const Napi::CallbackInfo& info)
        {
            if (info.Length() != 1 ||
                !info[0].IsObject())
            {
                throw std::exception(/*invalid arguments*/);
            }

            const auto options = CreateDetectorOptions(info[0].As<Napi::Object>());
            const auto result = m_xr->TrySetPreferredMeshDetectorOptions(options);
            return Napi::Value::From(info.Env(), result);
        }

        Napi::Value XRSession::GetTrackedImageScores(const Napi::CallbackInfo& info)
        {
            std::vector<xr::ImageTrackingScore>* imageTrackingScores{ m_xr->GetImageTrackingScores() };
            if (imageTrackingScores == nullptr)
            {
                return info.Env().Undefined();
            }

            auto results{ Napi::Array::New(info.Env(), imageTrackingScores->size()) };
            uint32_t index{ 0 };

            // Loop over the list of tracked image tracking results, and add them to the array.
            for (const auto& score : *imageTrackingScores)
            {
                const std::string scoreString = score == xr::ImageTrackingScore::TRACKABLE ? XRImageTrackingScore::TRACKABLE : XRImageTrackingScore::UNTRACKABLE;
                results.Set(index++, Napi::Value::From(info.Env(), scoreString));
            }

            return results;
        }
     } // Plugins
} // Babylon
