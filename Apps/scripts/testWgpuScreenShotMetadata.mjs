import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const read = path => readFileSync(new URL(`../../${path}`, import.meta.url), "utf8");
const native = read("Core/GraphicsWgpu/Source/WgpuNative.cpp");
const device = read("Core/GraphicsWgpu/Source/DeviceImpl.cpp");
const testUtils = read("Plugins/TestUtils/Source/TestUtils.cpp");

// Source-contract checks complement the native capture/resize tests without a GPU or build.
test("WGPU copy retains the native capture metadata with its pixels", () => {
    const copy = native.slice(native.indexOf("ScreenShotData WgpuNative::CopyScreenShot()"), native.indexOf("WgpuBootstrapInfo WgpuNative::GetInfo()"));
    assert.match(copy, /babylon_wgpu_get_screenshot_info\(m_context, &result\.Width, &result\.Height, &byteLen\)/);
    assert.match(copy, /babylon_wgpu_copy_screenshot\(m_context, result\.Pixels\.data\(\), result\.Pixels\.size\(\)\)/);
    assert.match(copy, /return result;/);
    assert.doesNotMatch(copy, /GetWidth|GetHeight|HardwareScaling/);
});

test("WGPU screenshot queue and blank fallback preserve structured dimensions", () => {
    for (const path of [
        "Core/GraphicsWgpu/InternalInclude/Babylon/Graphics/DeviceContext.h",
        "Core/GraphicsWgpu/Source/DeviceContext.cpp",
        "Core/GraphicsWgpu/Source/DeviceImpl.h",
        "Core/GraphicsWgpu/Source/DeviceImpl.cpp",
    ]) {
        assert.match(read(path), /RequestScreenShot\(std::function<void\(ScreenShotData\)> callback\)/);
        assert.doesNotMatch(read(path), /std::function<void\(std::vector<uint8_t>\)>/);
    }
    assert.match(device, /frame\.Width = static_cast<uint32_t>\(renderWidth\);/);
    assert.match(device, /frame\.Height = static_cast<uint32_t>\(renderHeight\);/);
    assert.match(device, /frame\.Pixels\.resize\(renderWidth \* renderHeight \* 4u, 0\);/);
    assert.match(device, /pendingCallbacks\.front\(\)\(frame\);/);
    assert.match(device, /pendingScreenShots\.front\(\)\(\{\}\);/);
});

test("TestUtils dispatches captured WGPU dimensions without querying live viewport state", () => {
    const start = testUtils.indexOf("void TestUtils::GetFrameBufferData(");
    const end = testUtils.indexOf("#else", start);
    const callback = testUtils.slice(testUtils.indexOf("#if defined(BABYLON_NATIVE_TESTUTILS_WGPU)", start), end);
    assert.match(callback, /Graphics::ScreenShotData frame/);
    assert.match(callback, /frame\{ std::move\(frame\) \}/);
    assert.match(callback, /gsl::span<uint8_t>\{frame\.Pixels\}/);
    assert.match(callback, /pixels = std::move\(frame\.Pixels\)/);
    assert.match(callback, /Call\(\{\s*typedArray,\s*Napi::Number::New\(env, static_cast<double>\(frame\.Width\)\),\s*Napi::Number::New\(env, static_cast<double>\(frame\.Height\)\)/);
    assert.doesNotMatch(callback, /GetWidth|GetHeight|HardwareScaling/);
});
