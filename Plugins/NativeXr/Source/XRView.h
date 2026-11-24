#pragma once

#include <algorithm>

namespace Babylon
{
    class XRView : public Napi::ObjectWrap<XRView>
    {
        static constexpr auto JS_CLASS_NAME = "XRView";
        static constexpr size_t MATRIX_SIZE = 16;

    public:
        static void Initialize(Napi::Env env)
        {
            Napi::HandleScope scope{env};

            Napi::Function func = DefineClass(
                env,
                JS_CLASS_NAME,
                {
                    InstanceAccessor("eye", &XRView::GetEye, nullptr),
                    InstanceAccessor("projectionMatrix", &XRView::GetProjectionMatrix, nullptr),
                    InstanceAccessor("transform", &XRView::GetTransform, nullptr),
                    InstanceAccessor("isFirstPersonObserver", &XRView::IsFirstPersonObserver, nullptr),
                });

            env.Global().Set(JS_CLASS_NAME, func);
        }

        static Napi::Object New(const Napi::CallbackInfo& info)
        {
            return info.Env().Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({});
        }

        XRView(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<XRView>{info}
            , m_eyeIdx{0}
            , m_eye{XREye::IndexToEye(m_eyeIdx)}
            , m_isFirstPersonObserver{false}
        {
        }

        bool BelongsTo(const std::shared_ptr<const xr::System::Session::Frame>& frame) const
        {
            return frame && m_frame.lock() == frame;
        }

        void Update(size_t eyeIdx, gsl::span<const float, 16> projectionMatrix, const xr::Space& space, bool isFirstPersonObserver,
            const std::shared_ptr<const xr::System::Session::Frame>& frame)
        {
            m_frame = frame;
            if (eyeIdx != m_eyeIdx)
            {
                m_eyeIdx = eyeIdx;
                m_eye = XREye::IndexToEye(m_eyeIdx);
            }

            // Capture by value: script can retain the view beyond the device frame.
            std::copy(projectionMatrix.begin(), projectionMatrix.end(), m_projection.begin());
            m_space = space;
            if (!m_projectionMatrix.IsEmpty())
                std::memcpy(m_projectionMatrix.Value().Data(), m_projection.data(), sizeof(m_projection));
            if (!m_rigidTransform.IsEmpty())
                XRRigidTransform::Unwrap(m_rigidTransform.Value())->Update(m_space, false);

            m_isFirstPersonObserver = isFirstPersonObserver;
        }

    private:
        size_t m_eyeIdx{};
        gsl::czstring m_eye{};
        std::array<float, MATRIX_SIZE> m_projection{};
        xr::Space m_space{};
        Napi::Reference<Napi::Float32Array> m_projectionMatrix{};
        Napi::ObjectReference m_rigidTransform{};
        bool m_isFirstPersonObserver{};
        std::weak_ptr<const xr::System::Session::Frame> m_frame{};

        Napi::Value GetEye(const Napi::CallbackInfo& info)
        {
            return Napi::String::From(info.Env(), m_eye);
        }

        Napi::Value GetProjectionMatrix(const Napi::CallbackInfo& info)
        {
            if (m_projectionMatrix.IsEmpty())
            {
                auto matrix = Napi::Float32Array::New(info.Env(), MATRIX_SIZE);
                std::memcpy(matrix.Data(), m_projection.data(), sizeof(m_projection));
                m_projectionMatrix = Napi::Persistent(matrix);
            }
            return m_projectionMatrix.Value();
        }

        Napi::Value GetTransform(const Napi::CallbackInfo& info)
        {
            if (m_rigidTransform.IsEmpty())
            {
                auto transform = XRRigidTransform::New(info.Env());
                XRRigidTransform::Unwrap(transform)->Update(m_space, false);
                m_rigidTransform = Napi::Persistent(transform);
            }
            return m_rigidTransform.Value();
        }

        Napi::Value IsFirstPersonObserver(const Napi::CallbackInfo& info)
        {
            return Napi::Boolean::From(info.Env(), m_isFirstPersonObserver);
        }
    };
} // Babylon
