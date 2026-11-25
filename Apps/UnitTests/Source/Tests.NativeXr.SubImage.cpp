#include "../../../Plugins/NativeXr/Source/XRGPUSubImage.h"
#include <Babylon/AppRuntime.h>
#include <napi/env.h>
#include <gtest/gtest.h>
#include <future>

TEST(NativeXrSubImage, OptionalAttachmentsAndEyeSlices)
{
    Babylon::AppRuntime runtime{};
    std::promise<void> done;
    auto result = done.get_future();
    runtime.Dispatch([&](Napi::Env env) {
        try
        {
            Babylon::XRGPUSubImage::Initialize(env);
            const auto texture = Napi::Eval(env, "({width: 128, height: 64, createView() {}})", "texture.js").As<Napi::Object>();
            env.Global().Set("color", texture);
            for (uint32_t eye = 0; eye < 2; ++eye)
            {
                auto depth = eye ? Napi::Value{texture} : env.Null();
                auto sub = Babylon::XRGPUSubImage::New(env, {texture, depth, 128, 64, eye});
                env.Global().Set("image", sub);
                env.Global().Set("eye", eye);
                Napi::Eval(env, R"JS(
                    if (image.colorTexture !== color || image.depthStencilTexture !== (eye ? color : null) ||
                        image.motionVectorTexture !== null || image.getViewDescriptor().baseArrayLayer !== eye ||
                        image.depthStencilTextureWidth !== (eye ? 128 : 0) || image.depthStencilTextureHeight !== (eye ? 64 : 0))
                        throw Error("Wrong optional attachment or array slice for eye " + eye);
                )JS", "subImage.js");
                env.Global().Set(eye ? "right" : "left", sub);
            }
            Napi::Eval(env, R"JS(
                const viewport = left.viewport;
                Object.freeze(left);
                const changed = left.getViewDescriptor();
                changed.baseArrayLayer = 17;
                if (!(left instanceof XRGPUSubImage) || left === right ||
                    left.viewport !== viewport || right.viewport === viewport ||
                    left.getViewDescriptor().baseArrayLayer !== 0 || right.getViewDescriptor().baseArrayLayer !== 1 ||
                    right.depthStencilTexture !== color || left.depthStencilTexture !== null)
                    throw Error("Retained stereo subimages must remain independent");
            )JS", "retainedSubImages.js");
            done.set_value();
        }
        catch (...)
        {
            done.set_exception(std::current_exception());
        }
    });
    EXPECT_NO_THROW(result.get());
}
