#pragma once

#include <stdexcept>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Babylon
{
    // Implementation of the XRReferenceSpace interface: https://immersive-web.github.io/webxr/#xrreferencespace-interface
    class XRReferenceSpace : public Napi::ObjectWrap<XRReferenceSpace>
    {
        static constexpr auto JS_CLASS_NAME = "XRReferenceSpace";

    public:
        static void Initialize(Napi::Env env)
        {
            Napi::HandleScope scope{env};

            Napi::Function func = DefineClass(
                env,
                JS_CLASS_NAME,
                {
                    InstanceMethod("getOffsetReferenceSpace", &XRReferenceSpace::GetOffsetReferenceSpace),
                });

            env.Global().Set(JS_CLASS_NAME, func);
        }

        static Napi::Object New(const Napi::CallbackInfo& info)
        {
            return info.Env().Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({info[0]});
        }

        static Napi::Object New(const Napi::Env env, Napi::Object napiTransform)
        {
            return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({napiTransform});
        }

        static Napi::Object FromType(Napi::Env env, Napi::String type)
        {
            return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({type});
        }

        static Napi::Promise Request(const Napi::CallbackInfo& info, const std::vector<std::string>& enabledFeatures, const std::shared_ptr<float>& localFloorHeight = {})
        {
            auto deferred = Napi::Promise::Deferred::New(info.Env());
            try
            {
                const auto typeValue = info[0].ToString();
                const auto type = typeValue.Utf8Value();
                if (type != XRReferenceSpaceType::VIEWER && type != XRReferenceSpaceType::LOCAL &&
                    type != XRReferenceSpaceType::LOCAL_FLOOR && type != XRReferenceSpaceType::UNBOUNDED && type != "bounded-floor")
                {
                    deferred.Reject(Napi::TypeError::New(info.Env(), "Invalid XRReferenceSpaceType: " + type).Value());
                }
                else if (type == "bounded-floor" || std::find(enabledFeatures.begin(), enabledFeatures.end(), type) == enabledFeatures.end())
                {
                    const auto message = "Reference space is not supported by this session: " + type;
                    auto ctor = info.Env().Global().Get("DOMException");
                    if (ctor.IsFunction())
                    {
                        deferred.Reject(ctor.As<Napi::Function>().New({Napi::String::New(info.Env(), message), Napi::String::New(info.Env(), "NotSupportedError")}));
                    }
                    else
                    {
                        auto error = Napi::Error::New(info.Env(), message).Value();
                        error.Set("name", "NotSupportedError");
                        deferred.Reject(error);
                    }
                }
                else
                {
                    auto space = XRReferenceSpace::FromType(info.Env(), typeValue);
                    if (type == XRReferenceSpaceType::LOCAL_FLOOR && localFloorHeight)
                    {
                        auto* reference = Unwrap(space);
                        reference->m_jsTransform.Reset();
                        reference->m_floorHeight = localFloorHeight;
                    }
                    deferred.Resolve(space);
                }
            }
            catch (const Napi::Error& error)
            {
                deferred.Reject(error.Value());
            }
            return deferred.Promise();
        }

        XRReferenceSpace(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<XRReferenceSpace>{info}
        {
            if (info.Length() > 0)
            {
                if (info[0].IsString())
                {
                    const auto referenceSpaceType = info[0].As<Napi::String>().Utf8Value();
                    m_viewer = referenceSpaceType == XRReferenceSpaceType::VIEWER;
                    if (referenceSpaceType == XRReferenceSpaceType::LOCAL_FLOOR)
                    {
#ifdef __ANDROID__
                        // Our OpenXR tracking origin is LOCAL. Emulate standing height
                        // when the binding does not provide a calibrated floor offset.
                        auto transform = XRRigidTransform::New(info.Env());
                        XRRigidTransform::Unwrap(transform)->Update(xr::Pose{{0.f, -1.6f, 0.f}, {0.f, 0.f, 0.f, 1.f}});
                        SetTransform(transform);
#endif
                    }
                    else if (referenceSpaceType != XRReferenceSpaceType::LOCAL &&
                        referenceSpaceType != XRReferenceSpaceType::VIEWER && referenceSpaceType != XRReferenceSpaceType::UNBOUNDED)
                    {
                        throw Napi::TypeError::New(info.Env(), "Unsupported XRReferenceSpace type: " + referenceSpaceType);
                    }
                }
                else
                {
                    m_jsTransform = Napi::Persistent(info[0].As<Napi::Object>());
                }
            }
        }

        void SetTransform(Napi::Object transformObj)
        {
            m_jsTransform = Napi::Persistent(transformObj);
        }

        bool IsViewer() const { return m_viewer; }
        bool HasOffset() { return m_floorHeight || GetTransform(); }

        xr::Pose OriginInNativeFrame(const xr::Pose& viewer)
        {
            const auto offset = GetPoseInNativeSpace({});
            return m_viewer ? ComposePoses(viewer, offset) : offset;
        }

        XRRigidTransform* GetTransform()
        {
            return m_jsTransform.IsEmpty() ? nullptr : XRRigidTransform::Unwrap(m_jsTransform.Value());
        }

        xr::Pose GetPoseInReferenceSpace(const xr::Pose& nativePose)
        {
            auto* transform = GetTransform();
            auto pose = nativePose;
            if (m_floorHeight) pose.Position.Y -= *m_floorHeight;
            return transform == nullptr ? pose : ComposePoses(InvertPose(transform->GetNativePose()), pose);
        }

        xr::Pose GetPoseInNativeSpace(const xr::Pose& referencePose)
        {
            auto* transform = GetTransform();
            auto pose = transform == nullptr ? referencePose : ComposePoses(transform->GetNativePose(), referencePose);
            if (m_floorHeight) pose.Position.Y += *m_floorHeight;
            return pose;
        }

    private:
        Napi::Value GetOffsetReferenceSpace(const Napi::CallbackInfo& info)
        {
            // TODO: Handle XRBoundedReferenceSpace case
            // https://immersive-web.github.io/webxr/#dom-xrreferencespace-getoffsetreferencespace

            auto* offset = XRRigidTransform::Unwrap(info[0].As<Napi::Object>());
            if (offset == nullptr)
            {
                throw std::runtime_error{"XRReferenceSpace.getOffsetReferenceSpace requires an XRRigidTransform."};
            }
            if (auto* current = GetTransform())
            {
                auto composedTransform = XRRigidTransform::New(info.Env());
                XRRigidTransform::Unwrap(composedTransform)->Update(
                    ComposePoses(current->GetNativePose(), offset->GetNativePose()));
                auto space = XRReferenceSpace::New(info.Env(), composedTransform);
                Unwrap(space)->m_viewer = m_viewer;
                Unwrap(space)->m_floorHeight = m_floorHeight;
                return space;
            }
            auto space = XRReferenceSpace::New(info);
            Unwrap(space)->m_viewer = m_viewer;
            Unwrap(space)->m_floorHeight = m_floorHeight;
            return space;
        }

        Napi::ObjectReference m_jsTransform{};
        std::shared_ptr<float> m_floorHeight;
        bool m_viewer{};
    };
} // Babylon
