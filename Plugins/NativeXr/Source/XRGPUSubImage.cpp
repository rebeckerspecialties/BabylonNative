#include "XRGPUSubImage.h"
#include <napi/env.h>

namespace Babylon
{
    namespace
    {
        constexpr auto JS_CLASS_NAME{"XRGPUSubImage"};
    }

    void XRGPUSubImage::Initialize(Napi::Env env)
    {
        // Only the textures own native resources. The per-eye snapshot and its
        // references can be collected by the JS engine without native finalizers.
        env.Global().Set(JS_CLASS_NAME, Napi::Eval(env, R"JS(
            (() => {
                const snapshots = new WeakMap();
                function texture(value, name) {
                    if (!value || typeof value.createView !== "function")
                        throw new TypeError("XRGPUSubImage " + name + " must be a GPUTexture.");
                    return value;
                }
                function number(value, name) {
                    if (typeof value !== "number")
                        throw new TypeError("XRGPUSubImage " + name + " must be a number.");
                    return value >>> 0;
                }
                return class XRGPUSubImage {
                    constructor(color, depth, width, height, layer, x, y, viewWidth, viewHeight, motion) {
                        color = texture(color, "colorTexture");
                        width = number(width, "width");
                        height = number(height, "height");
                        layer = number(layer, "array layer");
                        if (depth !== null) depth = texture(depth, "depthStencilTexture");
                        motion = arguments.length < 10 || motion === null ? null : texture(motion, "motionVectorTexture");
                        const viewport = arguments.length < 9 ? {x: 0, y: 0, width, height} : {x, y, width: viewWidth, height: viewHeight};
                        snapshots.set(this, {color, depth, motion, width, height, layer, viewport});
                    }
                    get colorTexture() { return snapshots.get(this).color; }
                    get depthStencilTexture() { return snapshots.get(this).depth; }
                    get motionVectorTexture() { return snapshots.get(this).motion; }
                    get colorTextureWidth() { return snapshots.get(this).width; }
                    get colorTextureHeight() { return snapshots.get(this).height; }
                    get depthStencilTextureWidth() { const d = snapshots.get(this).depth; return d === null ? 0 : d.width; }
                    get depthStencilTextureHeight() { const d = snapshots.get(this).depth; return d === null ? 0 : d.height; }
                    get viewport() { return snapshots.get(this).viewport; }
                    getViewDescriptor() {
                        return {dimension: "2d", baseMipLevel: 0, mipLevelCount: 1, baseArrayLayer: snapshots.get(this).layer,
                            arrayLayerCount: 1, aspect: "all"};
                    }
                };
            })()
        )JS", "babylon-native://plugins/NativeXr/XRGPUSubImage.js"));
    }

    Napi::Object XRGPUSubImage::New(Napi::Env env, const XRGPUSubImageData& data)
    {
        return env.Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({
            data.ColorTexture, data.DepthStencilTexture,
            Napi::Number::New(env, data.Width), Napi::Number::New(env, data.Height),
            Napi::Number::New(env, data.ArrayLayer), Napi::Number::New(env, data.X), Napi::Number::New(env, data.Y),
            Napi::Number::New(env, data.ViewWidth ? data.ViewWidth : data.Width),
            Napi::Number::New(env, data.ViewHeight ? data.ViewHeight : data.Height),
            data.MotionVectorTexture.IsEmpty() ? env.Null() : data.MotionVectorTexture,
        });
    }
}
