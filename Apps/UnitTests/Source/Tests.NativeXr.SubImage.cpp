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
            const auto texture = Napi::Eval(env, "({createView() {}})", "texture.js").As<Napi::Object>();
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
            }
            done.set_value();
        }
        catch (...)
        {
            done.set_exception(std::current_exception());
        }
    });
    EXPECT_NO_THROW(result.get());
}
