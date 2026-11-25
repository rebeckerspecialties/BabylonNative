#pragma once

#include <napi/env.h>
#include <array>
#include <cstring>
#include <cmath>

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

    inline xr::Vector4f MultiplyPoseQuaternions(const xr::Vector4f& lhs, const xr::Vector4f& rhs)
    {
        return {
            (lhs.W * rhs.X) + (lhs.X * rhs.W) + (lhs.Y * rhs.Z) - (lhs.Z * rhs.Y),
            (lhs.W * rhs.Y) - (lhs.X * rhs.Z) + (lhs.Y * rhs.W) + (lhs.Z * rhs.X),
            (lhs.W * rhs.Z) + (lhs.X * rhs.Y) - (lhs.Y * rhs.X) + (lhs.Z * rhs.W),
            (lhs.W * rhs.W) - (lhs.X * rhs.X) - (lhs.Y * rhs.Y) - (lhs.Z * rhs.Z)};
    }

    inline xr::Vector3f RotatePoseVector(const xr::Vector4f& quaternion, const xr::Vector3f& vector)
    {
        const xr::Vector4f vectorAsQuaternion{vector.X, vector.Y, vector.Z, 0.f};
        const xr::Vector4f inverseQuaternion{-quaternion.X, -quaternion.Y, -quaternion.Z, quaternion.W};
        const auto rotated = MultiplyPoseQuaternions(
            MultiplyPoseQuaternions(quaternion, vectorAsQuaternion),
            inverseQuaternion);
        return {rotated.X, rotated.Y, rotated.Z};
    }

    inline xr::Pose ComposePoses(const xr::Pose& basePose, const xr::Pose& offsetPose)
    {
        const auto rotatedOffset = RotatePoseVector(basePose.Orientation, offsetPose.Position);
        return {
            {
                basePose.Position.X + rotatedOffset.X,
                basePose.Position.Y + rotatedOffset.Y,
                basePose.Position.Z + rotatedOffset.Z,
            },
            MultiplyPoseQuaternions(basePose.Orientation, offsetPose.Orientation)};
    }

    inline xr::Pose InvertPose(const xr::Pose& pose)
    {
        const xr::Vector4f inverseOrientation{
            -pose.Orientation.X,
            -pose.Orientation.Y,
            -pose.Orientation.Z,
            pose.Orientation.W};
        const xr::Vector3f inversePosition = RotatePoseVector(
            inverseOrientation,
            {-pose.Position.X, -pose.Position.Y, -pose.Position.Z});
        return {inversePosition, inverseOrientation};
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
                    InstanceAccessor("inverse", &XRRigidTransform::Inverse, nullptr, napi_configurable),
                });

            // A live transform keeps its inverse alive, but an unreachable pair is collectible.
            // Keep those edges outside the wrappers so inverse also works after Object.freeze.
            Napi::Eval(env, R"JS(
                proto => {
                    const cache = new WeakMap();
                    const get = cache.get.bind(cache), set = cache.set.bind(cache);
                    const create = Function.prototype.call.bind(Object.getOwnPropertyDescriptor(proto, "inverse").get);
                    Object.defineProperty(proto, "inverse", { configurable: false, get() {
                        const cached = get(this);
                        if (cached !== undefined) return cached;
                        const inverse = create(this);
                        set(this, inverse);
                        set(inverse, this);
                        return inverse;
                    }});
                }
            )JS", "babylon-native://plugins/NativeXr/transformInverse.js").As<Napi::Function>().Call({func.Get("prototype")});
            env.Global().Set(JS_CLASS_NAME, func);
        }

        static Napi::Object New(const Napi::Env& env)
        {
            return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({});
        }

        XRRigidTransform(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<XRRigidTransform>{info}
        {
            const auto readPoint = [](const Napi::Object& input) {
                std::array<double, 4> point{0, 0, 0, 1};
                constexpr std::array keys{"x", "y", "z", "w"};
                for (size_t i = 0; i < keys.size(); ++i)
                {
                    const auto member = input.Get(keys[i]);
                    if (!member.IsUndefined())
                        point[i] = member.ToNumber().DoubleValue();
                }
                return point;
            };
            if (info[0].IsObject())
            {
                m_positionValues = readPoint(info[0].As<Napi::Object>());
                if (m_positionValues[3] != 1)
                    throw Napi::TypeError::New(info.Env(), "XRRigidTransform position.w must equal 1");
            }
            if (info[1].IsObject())
            {
                m_orientationValues = readPoint(info[1].As<Napi::Object>());
                double norm{};
                for (const auto n : m_orientationValues)
                    norm += n * n;
                if (!(norm > 0) || !std::isfinite(norm))
                    throw Napi::TypeError::New(info.Env(), "Invalid orientation quaternion");
                const auto length = std::sqrt(norm);
                for (auto& n : m_orientationValues)
                    n /= length;
            }
        }

        void Update(XRRigidTransform* transform)
        {
            Update({transform->GetNativePose()}, false);
        }

        void Update(const xr::Space& space, bool inverse)
        {
            UpdateValues(space, inverse);
            if (!m_inverse.IsEmpty())
            {
                const auto cached = m_inverse.Value();
                if (!cached.IsEmpty())
                    Unwrap(cached)->UpdateValues(xr::Space{InvertPose(space.Pose)}, inverse);
            }
        }

    private:
        void UpdateValues(const xr::Space& space, bool inverse)
        {
            m_positionValues = {space.Pose.Position.X, space.Pose.Position.Y, space.Pose.Position.Z, 1};
            m_orientationValues = {space.Pose.Orientation.X, space.Pose.Orientation.Y, space.Pose.Orientation.Z, space.Pose.Orientation.W};
            m_inverseMatrix = inverse;
            if (!m_position.IsEmpty())
                WritePoint(m_position.Value(), m_positionValues);
            if (!m_orientation.IsEmpty())
                WritePoint(m_orientation.Value(), m_orientationValues);
            if (!m_matrix.IsEmpty())
                WriteMatrix();
        }

        static void WritePoint(const Napi::Object& point, const std::array<double, 4>& values)
        {
            point.Set("x", values[0]);
            point.Set("y", values[1]);
            point.Set("z", values[2]);
            point.Set("w", values[3]);
        }

        void WriteMatrix()
        {
            auto output = m_matrix.Value();
            if (output.ByteLength() == MATRIX_SIZE * sizeof(float))
            {
                const auto matrix = CreateTransformMatrix(xr::Space{GetNativePose()}, m_inverseMatrix);
                std::memcpy(output.Data(), matrix.data(), sizeof(matrix));
            }
        }

    public:
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

        xr::Pose GetNativePose() const
        {
            return {
                {static_cast<float>(m_positionValues[0]), static_cast<float>(m_positionValues[1]), static_cast<float>(m_positionValues[2])},
                {static_cast<float>(m_orientationValues[0]), static_cast<float>(m_orientationValues[1]), static_cast<float>(m_orientationValues[2]), static_cast<float>(m_orientationValues[3])}};
        }

    private:
        std::array<double, 4> m_positionValues{0, 0, 0, 1};
        std::array<double, 4> m_orientationValues{0, 0, 0, 1};
        bool m_inverseMatrix{};
        Napi::ObjectReference m_position{};
        Napi::ObjectReference m_orientation{};
        Napi::Reference<Napi::Float32Array> m_matrix{};
        Napi::ObjectReference m_inverse{};

        Napi::Value Inverse(const Napi::CallbackInfo& info)
        {
            auto result = New(info.Env());
            auto* inverse = Unwrap(result);
            inverse->Update(InvertPose(GetNativePose()));
            auto self = Value();
            m_inverse = Napi::Weak(result);
            inverse->m_inverse = Napi::Weak(self);
            return result;
        }

        Napi::Value Position(const Napi::CallbackInfo& info)
        {
            if (m_position.IsEmpty())
            {
                auto point = Napi::Object::New(info.Env());
                WritePoint(point, m_positionValues);
                m_position = Napi::Persistent(point);
            }
            return m_position.Value();
        }

        Napi::Value Orientation(const Napi::CallbackInfo& info)
        {
            if (m_orientation.IsEmpty())
            {
                auto point = Napi::Object::New(info.Env());
                WritePoint(point, m_orientationValues);
                m_orientation = Napi::Persistent(point);
            }
            return m_orientation.Value();
        }

        Napi::Value Matrix(const Napi::CallbackInfo& info)
        {
            if (m_matrix.IsEmpty())
            {
                m_matrix = Napi::Persistent(Napi::Float32Array::New(info.Env(), MATRIX_SIZE));
                WriteMatrix();
            }
            return m_matrix.Value();
        }
    };
} // Babylon
