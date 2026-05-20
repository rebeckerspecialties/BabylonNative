#pragma once

#include <array>
#include <cstring>

namespace Babylon
{
    // clang-format off
    constexpr std::array<float, 16> IDENTITY_MATRIX{
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    // clang-format on

    inline std::array<float, 16> InvertRigidTransformMatrix(const std::array<float, 16>& matrix)
    {
        auto inverse{ IDENTITY_MATRIX };

        inverse[0] = matrix[0];
        inverse[1] = matrix[4];
        inverse[2] = matrix[8];

        inverse[4] = matrix[1];
        inverse[5] = matrix[5];
        inverse[6] = matrix[9];

        inverse[8] = matrix[2];
        inverse[9] = matrix[6];
        inverse[10] = matrix[10];

        const auto tx = matrix[12];
        const auto ty = matrix[13];
        const auto tz = matrix[14];
        inverse[12] = -((matrix[0] * tx) + (matrix[1] * ty) + (matrix[2] * tz));
        inverse[13] = -((matrix[4] * tx) + (matrix[5] * ty) + (matrix[6] * tz));
        inverse[14] = -((matrix[8] * tx) + (matrix[9] * ty) + (matrix[10] * tz));

        return inverse;
    }

    inline std::array<float, 16> CreateTransformMatrix(const xr::Space& space, bool inverse = false)
    {
        auto& quat = space.Pose.Orientation;
        auto& pos = space.Pose.Position;

        // Quaternion to matrix from https://github.com/BabylonJS/Babylon.js/blob/v4.0.0/src/Maths/math.ts#L6245-L6283
        const float xx{ quat.X * quat.X };
        const float yy{ quat.Y * quat.Y };
        const float zz{ quat.Z * quat.Z };
        const float xy{ quat.X * quat.Y };
        const float zw{ quat.Z * quat.W };
        const float zx{ quat.Z * quat.X };
        const float yw{ quat.Y * quat.W };
        const float yz{ quat.Y * quat.Z };
        const float xw{ quat.X * quat.W };

        auto worldSpaceTransform{ IDENTITY_MATRIX };

        worldSpaceTransform[0] = 1.f - (2.f * (yy + zz));
        worldSpaceTransform[1] = 2.f * (xy + zw);
        worldSpaceTransform[2] = 2.f * (zx - yw);
        worldSpaceTransform[3] = 0.f;

        worldSpaceTransform[4] = 2.f * (xy - zw);
        worldSpaceTransform[5] = 1.f - (2.f * (zz + xx));
        worldSpaceTransform[6] = 2.f * (yz + xw);
        worldSpaceTransform[7] = 0.f;

        worldSpaceTransform[8] = 2.f * (zx + yw);
        worldSpaceTransform[9] = 2.f * (yz - xw);
        worldSpaceTransform[10] = 1.f - (2.f * (yy + xx));
        worldSpaceTransform[11] = 0.f;

        // Insert position into rotation matrix.
        worldSpaceTransform[12] = pos.X;
        worldSpaceTransform[13] = pos.Y;
        worldSpaceTransform[14] = pos.Z;
        worldSpaceTransform[15] = 1.f;

        if (inverse)
        {
            return InvertRigidTransformMatrix(worldSpaceTransform);
        }
        else
        {
            return worldSpaceTransform;
        }
    }

    class XRRigidTransform : public Napi::ObjectWrap<XRRigidTransform>
    {
        static constexpr auto JS_CLASS_NAME = "XRRigidTransform";
        // static constexpr size_t VECTOR_SIZE = 4;
        static constexpr size_t MATRIX_SIZE = 16;

    public:
        static void Initialize(Napi::Env env)
        {
            Napi::HandleScope scope{env};

            Napi::Function func = DefineClass(
                env,
                JS_CLASS_NAME,
                {
                    InstanceAccessor("position", &XRRigidTransform::Position, nullptr),
                    InstanceAccessor("orientation", &XRRigidTransform::Orientation, nullptr),
                    InstanceAccessor("matrix", &XRRigidTransform::Matrix, nullptr),
                });

            env.Global().Set(JS_CLASS_NAME, func);
        }

        static Napi::Object New(const Napi::Env& env)
        {
            return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({});
        }

        XRRigidTransform(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<XRRigidTransform>{info}
            , m_matrix{Napi::Persistent(Napi::Float32Array::New(info.Env(), MATRIX_SIZE))}
        {
            if (info.Length() == 2)
            {
                m_position = Napi::Persistent(info[0].As<Napi::Object>());
                m_orientation = Napi::Persistent(info[1].As<Napi::Object>());
            }
            else
            {
                auto position{Napi::Object::New(info.Env())};
                position.Set("x", 0.f);
                position.Set("y", 0.f);
                position.Set("z", 0.f);
                position.Set("w", 1.f);
                m_position = Napi::Persistent(position);

                auto orientation{Napi::Object::New(info.Env())};
                orientation.Set("x", 0.f);
                orientation.Set("y", 0.f);
                orientation.Set("z", 0.f);
                orientation.Set("w", 1.f);
                m_orientation = Napi::Persistent(orientation);
            }
        }

        void Update(XRRigidTransform* transform)
        {
            Update({transform->GetNativePose()}, false);
        }

        void Update(const xr::Space& space, bool inverse)
        {
            auto position = m_position.Value();
            position.Set("x", space.Pose.Position.X);
            position.Set("y", space.Pose.Position.Y);
            position.Set("z", space.Pose.Position.Z);
            position.Set("w", 1.f);

            auto orientation = m_orientation.Value();
            orientation.Set("x", space.Pose.Orientation.X);
            orientation.Set("y", space.Pose.Orientation.Y);
            orientation.Set("z", space.Pose.Orientation.Z);
            orientation.Set("w", space.Pose.Orientation.W);

            std::memcpy(m_matrix.Value().Data(), CreateTransformMatrix(space, inverse).data(), m_matrix.Value().ByteLength());
        }

        void Update(const xr::Space& space, Napi::ArrayBuffer& outVectorData, Napi::ArrayBuffer& outMatrixData, bool inverse)
        {
            float posAndOrientationData[8];
            posAndOrientationData[0] = space.Pose.Position.X;
            posAndOrientationData[1] = space.Pose.Position.Y;
            posAndOrientationData[2] = space.Pose.Position.Z;
            posAndOrientationData[3] = 1.f;
            posAndOrientationData[4] = space.Pose.Orientation.X;
            posAndOrientationData[5] = space.Pose.Orientation.Y;
            posAndOrientationData[6] = space.Pose.Orientation.Z;
            posAndOrientationData[7] = space.Pose.Orientation.W;

            std::memcpy(outVectorData.Data(), posAndOrientationData, sizeof(float) * 8);
            std::memcpy(outMatrixData.Data(), CreateTransformMatrix(space, inverse).data(), sizeof(float) * 16);
        }

        void Update(const xr::Pose& pose)
        {
            xr::Space space{{pose}};
            Update(space, false);
        }

        xr::Pose GetNativePose()
        {
            auto position = m_position.Value();
            auto orientation = m_orientation.Value();
            return {
                {position.Get("x").ToNumber().FloatValue(), position.Get("y").ToNumber().FloatValue(), position.Get("z").ToNumber().FloatValue()},
                {orientation.Get("x").ToNumber().FloatValue(), orientation.Get("y").ToNumber().FloatValue(), orientation.Get("z").ToNumber().FloatValue(), orientation.Get("w").ToNumber().FloatValue()}};
        }

    private:
        Napi::ObjectReference m_position{};
        Napi::ObjectReference m_orientation{};
        Napi::Reference<Napi::Float32Array> m_matrix{};

        Napi::Value Position(const Napi::CallbackInfo&)
        {
            return m_position.Value();
        }

        Napi::Value Orientation(const Napi::CallbackInfo&)
        {
            return m_orientation.Value();
        }

        Napi::Value Matrix(const Napi::CallbackInfo&)
        {
            return m_matrix.Value();
        }
    };
} // Babylon
