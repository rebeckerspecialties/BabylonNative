#include <Babylon/AppRuntime.h>
#include <XR.h>
#include <napi/napi.h>

#include "../../../Plugins/NativeXr/Source/Constants.h"
#include "../../../Plugins/NativeXr/Source/XRRigidTransform.h"
#include "../../../Plugins/NativeXr/Source/XRReferenceSpace.h"
#include "../../../Plugins/NativeXr/Source/XRView.h"

#include <gtest/gtest.h>
#include <cmath>
#include <chrono>
#include <future>

namespace
{
    template<typename Callback>
    void RunReferenceSpaceTest(Callback callback)
    {
        Babylon::AppRuntime runtime{};
        std::promise<void> completed;
        auto future{completed.get_future()};
        runtime.Dispatch([&](Napi::Env env) {
            try
            {
                Babylon::XRRigidTransform::Initialize(env);
                Babylon::XRReferenceSpace::Initialize(env);
                callback(env);
                completed.set_value();
            }
            catch (...)
            {
                completed.set_exception(std::current_exception());
            }
        });
        future.get();
    }

    Napi::Object MakeTransform(Napi::Env env, const xr::Pose& pose)
    {
        auto transform{Babylon::XRRigidTransform::New(env)};
        Babylon::XRRigidTransform::Unwrap(transform)->Update(pose);
        return transform;
    }

    void ExpectPoseNear(const xr::Pose& actual, const xr::Pose& expected)
    {
        EXPECT_NEAR(actual.Position.X, expected.Position.X, 1e-5f);
        EXPECT_NEAR(actual.Position.Y, expected.Position.Y, 1e-5f);
        EXPECT_NEAR(actual.Position.Z, expected.Position.Z, 1e-5f);
        EXPECT_NEAR(actual.Orientation.X, expected.Orientation.X, 1e-5f);
        EXPECT_NEAR(actual.Orientation.Y, expected.Orientation.Y, 1e-5f);
        EXPECT_NEAR(actual.Orientation.Z, expected.Orientation.Z, 1e-5f);
        EXPECT_NEAR(actual.Orientation.W, expected.Orientation.W, 1e-5f);
    }
}

TEST(NativeXrReferenceSpace, MissingAndIdentityOffsetsLeaveNativePosesUnchanged)
{
    RunReferenceSpaceTest([](Napi::Env env) {
        const xr::Pose pose{{1.f, -2.f, 3.f}, {0.f, 0.f, 0.f, 1.f}};
        auto unbounded{env.Global().Get("XRReferenceSpace").As<Napi::Function>().New({Napi::String::New(env, "unbounded")})};
        auto* reference{Babylon::XRReferenceSpace::Unwrap(unbounded)};
        ExpectPoseNear(reference->GetPoseInReferenceSpace(pose), pose);
        ExpectPoseNear(reference->GetPoseInNativeSpace(pose), pose);
        reference->SetTransform(MakeTransform(env, {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}));
        ExpectPoseNear(reference->GetPoseInReferenceSpace(pose), pose);
        ExpectPoseNear(reference->GetPoseInNativeSpace(pose), pose);
    });
}

TEST(NativeXrReferenceSpace, RetainedEyeViewsMaterializeIndependentSnapshots)
{
    RunReferenceSpaceTest([](Napi::Env env) {
        Babylon::XRView::Initialize(env);
        auto ctor = env.Global().Get("XRView").As<Napi::Function>();
        auto left = ctor.New({}), right = ctor.New({});
        auto matrix = Babylon::IDENTITY_MATRIX;
        xr::Space source{{{-0.032f, 0, 0}, {}}};
        Babylon::XRView::Unwrap(left)->Update(0, matrix, source, false, {});
        source.Pose.Position.X = 0.032f;
        matrix[8] = 0.13f;
        Babylon::XRView::Unwrap(right)->Update(1, matrix, source, false, {});
        auto projection = left.Get("projectionMatrix").As<Napi::Float32Array>();
        auto transform = left.Get("transform").As<Napi::Object>();
        EXPECT_TRUE(projection.StrictEquals(left.Get("projectionMatrix")));
        EXPECT_TRUE(transform.StrictEquals(left.Get("transform")));
        EXPECT_FALSE(transform.StrictEquals(right.Get("transform")));
        EXPECT_FLOAT_EQ(projection[8], 0.f);
        EXPECT_FLOAT_EQ(right.Get("projectionMatrix").As<Napi::Float32Array>()[8], 0.13f);
        EXPECT_FLOAT_EQ(transform.Get("matrix").As<Napi::Float32Array>()[12], -0.032f);
        EXPECT_FLOAT_EQ(right.Get("transform").As<Napi::Object>().Get("matrix").As<Napi::Float32Array>()[12], 0.032f);
    });
}

TEST(NativeXrReferenceSpace, PositionOnlyLayerTransformDefaultsAndCopiesItsDictionary)
{
    RunReferenceSpaceTest([](Napi::Env env) {
        auto position = Napi::Object::New(env);
        position.Set("z", -2);
        auto transform = env.Global().Get("XRRigidTransform").As<Napi::Function>().New({position});
        position.Set("z", 4);
        ExpectPoseNear(Babylon::XRRigidTransform::Unwrap(transform)->GetNativePose(),
            {{0, 0, -2}, {0, 0, 0, 1}});
        EXPECT_FLOAT_EQ(transform.Get("matrix").As<Napi::Float32Array>()[14], -2.f);
        EXPECT_FLOAT_EQ(transform.Get("inverse").As<Napi::Object>().Get("matrix").As<Napi::Float32Array>()[14], 2.f);
    });
}

TEST(NativeXrReferenceSpace, OffsetViewerOriginTracksTheHeadButOffsetLocalOriginDoesNot)
{
    RunReferenceSpaceTest([](Napi::Env env) {
        const auto offset = MakeTransform(env, {{0, 0, -2}, {}});
        for (const bool viewer : {false, true})
        {
            auto base = Babylon::XRReferenceSpace::FromType(env, Napi::String::New(env, viewer ? "viewer" : "local"));
            auto space = base.Get("getOffsetReferenceSpace").As<Napi::Function>().Call(base, {offset}).As<Napi::Object>();
            auto* ref = Babylon::XRReferenceSpace::Unwrap(space);
            for (const float height : {1.f, 2.f})
            {
                xr::Pose head{{0, height, 0}, {0, 1, 0, 0}};
                ExpectPoseNear(ref->OriginInNativeFrame(head), viewer ? xr::Pose{{0, height, 2}, head.Orientation} : xr::Pose{{0, 0, -2}, {}});
            }
        }
    });
}

TEST(NativeXrReferenceSpace, HonorsSessionGrantsAndPreservesConversionErrors)
{
    Babylon::AppRuntime runtime{};
    auto done = std::make_shared<std::promise<std::string>>();
    auto result = done->get_future();
    runtime.Dispatch([done](Napi::Env env) {
        try
        {
            Babylon::XRRigidTransform::Initialize(env);
            Babylon::XRReferenceSpace::Initialize(env);
            env.Global().Set("request", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                return Babylon::XRReferenceSpace::Request(info, {"local", "viewer", "local-floor"});
            }));
            env.Global().Set("finish", Napi::Function::New(env, [done](const Napi::CallbackInfo& info) {
                done->set_value(info[0].As<Napi::String>().Utf8Value());
            }));
            Napi::Eval(env, R"JS(
                (async () => {
                    const check = (test, message) => { if (!test) throw Error(message); };
                    const reject = p => p.then(() => { throw Error("unexpected success"); }, e => e);
                    for (const type of ["local", "viewer", "local-floor"])
                        check(await request(type) instanceof XRReferenceSpace, type);
                    for (const type of ["unbounded", "bounded-floor"])
                        check((await reject(request(type))).name === "NotSupportedError", type);
                    check((await reject(request("floor"))) instanceof TypeError, "invalid enum");
                    check((await reject(request())) instanceof TypeError, "missing argument");
                    let conversions = 0;
                    check(await request({toString() { conversions++; return "local"; }}) instanceof XRReferenceSpace, "coercion");
                    check(conversions === 1, "double coercion");
                    const failure = Error("toString failed");
                    check(await reject(request({toString() { throw failure; }})) === failure, "lost exception");
                    check(await request("viewer") instanceof XRReferenceSpace, "recovery");
                })().then(() => finish(""), e => finish(String(e)));
            )JS", "session-reference-space.js");
        }
        catch (const std::exception& error)
        {
            done->set_value(error.what());
        }
    });
    ASSERT_EQ(result.wait_for(std::chrono::seconds{10}), std::future_status::ready);
    EXPECT_EQ(result.get(), "");
}

TEST(NativeXrReferenceSpace, TranslationIsInvertedForReadsAndAppliedForAnchorCreation)
{
    RunReferenceSpaceTest([](Napi::Env env) {
        for (const float sign : {-1.f, 1.f})
        {
            auto space{Babylon::XRReferenceSpace::New(env, MakeTransform(env, {{2.f * sign, 3.f * sign, 4.f * sign}, {0.f, 0.f, 0.f, 1.f}}))};
            auto* reference{Babylon::XRReferenceSpace::Unwrap(space)};
            const xr::Pose native{{5.f, 6.f, 7.f}, {0.f, 0.f, 0.f, 1.f}};
            const xr::Pose local{{5.f - 2.f * sign, 6.f - 3.f * sign, 7.f - 4.f * sign}, native.Orientation};
            ExpectPoseNear(reference->GetPoseInReferenceSpace(native), local);
            ExpectPoseNear(reference->GetPoseInNativeSpace(local), native);
        }
    });
}

TEST(NativeXrReferenceSpace, RotatesPositionsAndOrientationsInBothDirections)
{
    RunReferenceSpaceTest([](Napi::Env env) {
        const float halfAngle{std::sqrt(0.5f)};
        for (const float sign : {-1.f, 1.f})
        {
            auto space{Babylon::XRReferenceSpace::New(env, MakeTransform(env, {{2.f, 0.f, 0.f}, {0.f, sign * halfAngle, 0.f, halfAngle}}))};
            auto* reference{Babylon::XRReferenceSpace::Unwrap(space)};
            const xr::Pose native{{3.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
            const xr::Pose local{{0.f, 0.f, sign}, {0.f, -sign * halfAngle, 0.f, halfAngle}};
            ExpectPoseNear(reference->GetPoseInReferenceSpace(native), local);
            ExpectPoseNear(reference->GetPoseInNativeSpace(local), native);
        }
    });
}

TEST(NativeXrReferenceSpace, ChainedOffsetsComposeInOrderAndPreserveRoundTrips)
{
    RunReferenceSpaceTest([](Napi::Env env) {
        const float halfAngle{std::sqrt(0.5f)};
        auto space{Babylon::XRReferenceSpace::New(env, MakeTransform(env, {{2.f, 0.f, 0.f}, {0.f, halfAngle, 0.f, halfAngle}}))};
        auto offset{MakeTransform(env, {{1.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}})};
        auto chained{space.Get("getOffsetReferenceSpace").As<Napi::Function>().Call(space, {offset}).As<Napi::Object>()};
        auto* reference{Babylon::XRReferenceSpace::Unwrap(chained)};
        ExpectPoseNear(reference->GetPoseInNativeSpace({{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}),
            {{2.f, 0.f, -1.f}, {0.f, halfAngle, 0.f, halfAngle}});
        const xr::Pose native{{4.f, -1.f, 6.f}, {halfAngle, 0.f, 0.f, halfAngle}};
        ExpectPoseNear(reference->GetPoseInNativeSpace(reference->GetPoseInReferenceSpace(native)), native);
    });
}
