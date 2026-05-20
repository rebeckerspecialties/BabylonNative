#pragma once

#include "XRPose.h"
#include "XRFrame.h"

namespace Babylon
{
    namespace Plugins
    {
        namespace
        {
            xr::Vector4f MultiplyQuaternions(const xr::Vector4f& lhs, const xr::Vector4f& rhs)
            {
                return {
                    (lhs.W * rhs.X) + (lhs.X * rhs.W) + (lhs.Y * rhs.Z) - (lhs.Z * rhs.Y),
                    (lhs.W * rhs.Y) - (lhs.X * rhs.Z) + (lhs.Y * rhs.W) + (lhs.Z * rhs.X),
                    (lhs.W * rhs.Z) + (lhs.X * rhs.Y) - (lhs.Y * rhs.X) + (lhs.Z * rhs.W),
                    (lhs.W * rhs.W) - (lhs.X * rhs.X) - (lhs.Y * rhs.Y) - (lhs.Z * rhs.Z)};
            }

            xr::Vector3f RotateVector(const xr::Vector4f& quaternion, const xr::Vector3f& vector)
            {
                const xr::Vector4f vectorAsQuaternion{vector.X, vector.Y, vector.Z, 0.f};
                const xr::Vector4f inverseQuaternion{-quaternion.X, -quaternion.Y, -quaternion.Z, quaternion.W};
                const auto rotated{MultiplyQuaternions(MultiplyQuaternions(quaternion, vectorAsQuaternion), inverseQuaternion)};
                return {rotated.X, rotated.Y, rotated.Z};
            }

            xr::Pose ComposePose(const xr::Pose& basePose, const xr::Pose& offsetPose)
            {
                const auto rotatedOffset{RotateVector(basePose.Orientation, offsetPose.Position)};
                return {
                    {
                        basePose.Position.X + rotatedOffset.X,
                        basePose.Position.Y + rotatedOffset.Y,
                        basePose.Position.Z + rotatedOffset.Z,
                    },
                    MultiplyQuaternions(basePose.Orientation, offsetPose.Orientation)};
            }
        }

        // Implementation of the XRHitTestResult interface: https://immersive-web.github.io/hit-test/#xr-hit-test-result-interface
        class XRHitTestResult : public Napi::ObjectWrap<XRHitTestResult>
        {
            static constexpr auto JS_CLASS_NAME = "XRHitTestResult";

        public:
            static void Initialize(Napi::Env env)
            {
                Napi::HandleScope scope{env};

                Napi::Function func = DefineClass(
                    env,
                    JS_CLASS_NAME,
                    {
                        InstanceMethod("getPose", &XRHitTestResult::GetPose),
                        InstanceMethod("createAnchor", &XRHitTestResult::CreateAnchor),
                    });

                env.Global().Set(JS_CLASS_NAME, func);
            }

            static Napi::Object New(const Napi::CallbackInfo& info)
            {
                return info.Env().Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({});
            }

            XRHitTestResult(const Napi::CallbackInfo& info)
                : Napi::ObjectWrap<XRHitTestResult>{info}
            {
            }

            // Sets the value of the hit pose and native entity via struct copy.
            void SetHitResult(const xr::HitResult& hitResult)
            {
                m_hitResult = hitResult;
            }

            void SetXRFrame(Plugins::XRFrame* frame)
            {
                m_frame = frame;
            }

        private:
            // The hit hit result, which contains the pose in default AR Space, as well as the native entity.
            xr::HitResult m_hitResult{};
            Plugins::XRFrame* m_frame{};

            Napi::Value GetPose(const Napi::CallbackInfo& info)
            {
                // TODO: Once multiple reference views are supported, we need to convert the values into the passed in reference space.
                Napi::Object napiPose = XRPose::New(info);
                XRPose* pose = XRPose::Unwrap(napiPose);
                pose->Update(info, m_hitResult.Pose);

                return napiPose;
            }

            Napi::Value CreateAnchor(const Napi::CallbackInfo& info)
            {
                auto anchorPose{m_hitResult.Pose};
                if (info.Length() > 0 && info[0].IsObject())
                {
                    anchorPose = ComposePose(anchorPose, XRRigidTransform::Unwrap(info[0].As<Napi::Object>())->GetNativePose());
                }
                return m_frame->CreateNativeAnchor(info, anchorPose, m_hitResult.NativeTrackable);
            }
        };
    } // Plugins
} // Babylon
