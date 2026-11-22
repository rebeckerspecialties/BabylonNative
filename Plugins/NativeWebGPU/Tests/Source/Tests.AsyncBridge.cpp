#include <gtest/gtest.h>

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/WgpuInterop.h>
#include <Babylon/Plugins/NativeWebGPU.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/ScriptLoader.h>

#include <atomic>
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <string>

using namespace std::chrono_literals;

namespace
{
    class ScopedWgpuBackend final
    {
    public:
        ScopedWgpuBackend()
        {
            BabylonWgpuConfig config{};
            config.width = 64;
            config.height = 64;
            m_context = babylon_wgpu_create(&config);
        }

        ~ScopedWgpuBackend()
        {
            if (m_context != nullptr)
            {
                babylon_wgpu_destroy(m_context);
            }
        }

        ScopedWgpuBackend(const ScopedWgpuBackend&) = delete;
        ScopedWgpuBackend& operator=(const ScopedWgpuBackend&) = delete;

        bool IsValid() const
        {
            return m_context != nullptr;
        }

        std::string LastError() const
        {
            std::array<char, 1024> buffer{};
            if (babylon_wgpu_get_last_error(buffer.data(), buffer.size()) && buffer[0] != '\0')
            {
                return buffer.data();
            }

            return {};
        }

    private:
        void* m_context{};
    };

    void RunNativeWebGpuAsyncScript(const char* scriptSource)
    {
        ScopedWgpuBackend backend{};
        ASSERT_TRUE(backend.IsValid()) << backend.LastError();

        std::promise<std::string> completionPromise{};
        auto completionFlag = std::make_shared<std::atomic_bool>(false);

        Babylon::AppRuntime::Options options{};
        options.UnhandledExceptionHandler = [&completionPromise, completionFlag](const Napi::Error& error) {
            bool expected = false;
            if (completionFlag->compare_exchange_strong(expected, true))
            {
                completionPromise.set_value(Napi::GetErrorString(error));
            }
        };

        Babylon::AppRuntime runtime{options};
        runtime.Dispatch([&completionPromise, completionFlag](Napi::Env env) {
            Babylon::Polyfills::Window::Initialize(env);
            Babylon::Plugins::NativeWebGPU::Initialize(env);

            env.Global().Set("__nativeWebGpuTestDone", Napi::Function::New(env, [&completionPromise, completionFlag](const Napi::CallbackInfo& info) {
                const bool success = info.Length() > 0 && info[0].IsBoolean() && info[0].As<Napi::Boolean>().Value();
                const std::string details = info.Length() > 1 && info[1].IsString() ? info[1].As<Napi::String>().Utf8Value() : std::string{};

                bool expected = false;
                if (completionFlag->compare_exchange_strong(expected, true))
                {
                    completionPromise.set_value(success ? std::string{} : details);
                }
            }));
        });

        Babylon::ScriptLoader loader{runtime};
        loader.Eval(scriptSource, "nativewebgpu.async.bridge.test.js");

        auto completionFuture = completionPromise.get_future();
        ASSERT_EQ(completionFuture.wait_for(30s), std::future_status::ready) << "Async bridge test timed out.";

        const auto errorText = completionFuture.get();
        EXPECT_TRUE(errorText.empty()) << errorText;
    }
}

TEST(NativeWebGPUAsyncBridge, ResolveIsAsynchronous)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            let settledSynchronously = false;
            let stillSynchronous = true;

            const promise = navigator.gpu._testAsyncResolve("bridge-ok").then((value) => {
                if (value !== "bridge-ok") {
                    throw new Error("Unexpected resolve value: " + value);
                }
                settledSynchronously = stillSynchronous;
            });

            stillSynchronous = false;
            await promise;

            if (settledSynchronously) {
                throw new Error("Promise settled synchronously.");
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, AnimationFrameCancellationAndNestingMatchBrowserSemantics)
{
    ScopedWgpuBackend backend{};
    ASSERT_TRUE(backend.IsValid()) << backend.LastError();

    std::promise<std::string> completionPromise{};
    auto completionFlag = std::make_shared<std::atomic_bool>(false);

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [&completionPromise, completionFlag](const Napi::Error& error) {
        bool expected = false;
        if (completionFlag->compare_exchange_strong(expected, true))
        {
            completionPromise.set_value(Napi::GetErrorString(error));
        }
    };

    Babylon::AppRuntime runtime{options};
    runtime.Dispatch([&completionPromise, completionFlag](Napi::Env env) {
        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Plugins::NativeWebGPU::Initialize(env);
        env.Global().Set("__nativeWebGpuTestDone", Napi::Function::New(env, [&completionPromise, completionFlag](const Napi::CallbackInfo& info) {
            const bool success = info.Length() > 0 && info[0].IsBoolean() && info[0].As<Napi::Boolean>().Value();
            const std::string details = info.Length() > 1 && info[1].IsString() ? info[1].As<Napi::String>().Utf8Value() : std::string{};

            bool expected = false;
            if (completionFlag->compare_exchange_strong(expected, true))
            {
                completionPromise.set_value(success ? std::string{} : details);
            }
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"JS(
        (() => {
            const calls = [];
            let firstTimestamp;
            __babylonNativeWebGpuAnimationFrameState.nextId = 0x70000000;
            const canceledId = requestAnimationFrame(() => calls.push("canceled"));
            let canceledDuringFrameId;
            const firstId = requestAnimationFrame((timestamp) => {
                if (typeof timestamp !== "number" || !Number.isFinite(timestamp)) {
                    throw new Error("Animation frame timestamp is not finite: " + String(timestamp));
                }
                firstTimestamp = timestamp;
                calls.push("first");
                cancelAnimationFrame(canceledDuringFrameId);
                requestAnimationFrame((nestedTimestamp) => {
                    if (nestedTimestamp < firstTimestamp) {
                        throw new Error("Animation frame timestamps went backwards.");
                    }
                    calls.push("nested");
                    const actual = calls.join(",");
                    __nativeWebGpuTestDone(actual === "first,peer,nested", "Unexpected callback order: " + actual);
                });
                if (calls.join(",") !== "first") {
                    throw new Error("Nested callback ran in the same frame.");
                }
            });
            canceledDuringFrameId = requestAnimationFrame(() => calls.push("canceled-during-frame"));
            requestAnimationFrame((timestamp) => {
                if (timestamp !== firstTimestamp) {
                    throw new Error("Callbacks in one animation frame received different timestamps.");
                }
                calls.push("peer");
            });

            if (typeof canceledId !== "number" || typeof firstId !== "number" ||
                typeof canceledDuringFrameId !== "number" || canceledId === firstId ||
                firstId === canceledDuringFrameId) {
                throw new Error("requestAnimationFrame did not return distinct numeric IDs.");
            }
            cancelAnimationFrame(canceledId);
        })();
    )JS", "nativewebgpu.animation-frame.test.js");
    runtime.Dispatch([](Napi::Env env) {
        Babylon::Plugins::NativeWebGPU::TickAnimationFrame(env);
    });
    runtime.Dispatch([](Napi::Env env) {
        Babylon::Plugins::NativeWebGPU::TickAnimationFrame(env);
    });

    auto completionFuture = completionPromise.get_future();
    ASSERT_EQ(completionFuture.wait_for(30s), std::future_status::ready) << "Animation frame test timed out.";

    const auto errorText = completionFuture.get();
    EXPECT_TRUE(errorText.empty()) << errorText;
}

TEST(NativeWebGPUAsyncBridge, DescriptorlessArrayTextureViewUsesWebGpuDefaultDimension)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            const device = await adapter.requestDevice();
            const texture = device.createTexture({
                size: { width: 4, height: 4, depthOrArrayLayers: 4 },
                format: "depth32float",
                usage: 0x04 | 0x10
            });

            const defaultView = texture.createView();
            if (defaultView.dimension !== "2d-array") {
                throw new Error("Unexpected default array texture view dimension: " + defaultView.dimension);
            }

            const layout = device.createBindGroupLayout({
                entries: [{
                    binding: 0,
                    visibility: 0x02,
                    texture: { sampleType: "depth", viewDimension: "2d-array" }
                }]
            });
            device.createBindGroup({
                layout,
                entries: [{ binding: 0, resource: defaultView }]
            });

            const layerView = texture.createView({
                dimension: "2d",
                baseArrayLayer: 0,
                arrayLayerCount: 1
            });
            if (layerView.dimension !== "2d") {
                throw new Error("Explicit 2d layer view dimension was not preserved.");
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, error && error.stack ? error.stack : String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, BufferPrototypeMethodsUseTheirActualReceiver)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            const a = await adapter.requestDevice();
            const b = await adapter.requestDevice();
            const make = device => device.createBuffer({size: 16, usage: GPUBufferUsage.MAP_WRITE | GPUBufferUsage.COPY_SRC});
            const first = make(a), second = make(b);
            for (const name of ["mapAsync", "getMappedRange", "unmap", "destroy"]) {
                if (!Object.hasOwn(GPUBuffer.prototype, name) || Object.hasOwn(first, name) || first[name] !== second[name]) {
                    throw Error(name + " is not a shared GPUBuffer prototype operation");
                }
            }
            await first.mapAsync.call(second, GPUMapMode.WRITE);
            if (first.mapState !== "unmapped" || second.mapState !== "mapped") throw Error("Wrong mapped receiver");
            const range = first.getMappedRange.call(second);
            first.unmap.call(second);
            if (range.byteLength !== 0 || second.mapState !== "unmapped") throw Error("Wrong unmap receiver");
            const invalid = [Object.create(first), a.createSampler(), {}];
            for (const receiver of invalid) {
                const promise = first.mapAsync.call(receiver, GPUMapMode.WRITE);
                if (!(promise instanceof Promise) || !(await promise.then(() => null, e => e) instanceof TypeError)) {
                    throw Error("Invalid mapAsync receiver must reject TypeError");
                }
                for (const name of ["getMappedRange", "unmap", "destroy"]) {
                    let error;
                    try { first[name].call(receiver); } catch (e) { error = e; }
                    if (!(error instanceof TypeError)) throw Error("Invalid " + name + " receiver was accepted");
                }
            }
            first.destroy.call(second);
            await first.mapAsync(GPUMapMode.WRITE);
            first.unmap();
            first.destroy();
            __nativeWebGpuTestDone(true, "");
        })().catch(error => __nativeWebGpuTestDone(false, error.stack || String(error)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, TextureViewsRequireAnExplicitAlternateFormat)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const device = await (await navigator.gpu.requestAdapter()).requestDevice();
            const base = {size: [4, 4], format: "rgba8unorm", usage: GPUTextureUsage.TEXTURE_BINDING};
            async function check(descriptor, format, valid) {
                const texture = device.createTexture(descriptor);
                device.pushErrorScope("validation");
                texture.createView({format});
                const error = await device.popErrorScope();
                texture.destroy();
                if ((error === null) !== valid) {
                    throw Error("Unexpected validation for " + format + " with " +
                        JSON.stringify(descriptor.viewFormats) + ": " + (error?.message ?? "success"));
                }
            }
            await check(base, "rgba8unorm", true);
            await check(base, "rgba8unorm-srgb", false);
            await check({...base, viewFormats: []}, "rgba8unorm-srgb", false);
            await check({...base, viewFormats: ["rgba8unorm-srgb"]}, "rgba8unorm-srgb", true);
            await check({...base, viewFormats: new Set(["rgba8unorm-srgb"]),
                toJSON() { throw Error("GPUTextureDescriptor is not JSON"); }}, "rgba8unorm-srgb", true);
            await check({...base, format: "rgba8unorm-srgb", viewFormats: ["rgba8unorm"]}, "rgba8unorm", true);
            __nativeWebGpuTestDone(true, "");
        })().catch(error => __nativeWebGpuTestDone(false, error.stack || String(error)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, BindGroupEntriesConvertInIteratorOrderWithoutJsonSerialization)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const check = (v, m) => { if (!v) throw Error(m); };
            const adapter = await navigator.gpu.requestAdapter();
            const device = await adapter.requestDevice();
            const layout = device.createBindGroupLayout({entries: [
                {binding: 0, visibility: 4, buffer: {type: "uniform"}},
                {binding: 1, visibility: 4, sampler: {}}
            ]});
            const buffer = device.createBuffer({size: 256, usage: GPUBufferUsage.UNIFORM});
            const sampler = device.createSampler({});
            const calls = [];
            const marker = Error("entry getter failed");
            device.pushErrorScope("validation");
            const entries = {
                *[Symbol.iterator]() {
                    calls.push("first");
                    yield {binding: 0, get resource() { calls.push("read"); return {buffer, size: 64}; }};
                    calls.push("second");
                    yield {binding: 1, resource: sampler};
                }
            };
            device.createBindGroup({layout, entries, get toJSON() { throw Error("Unexpected JSON serialization"); }});
            check(calls.join(",") === "first,read,second", "Entry values must convert before advancing the iterator: " + calls);
            check(await device.popErrorScope() === null, "Valid mixed bindings produced a validation error");
            const rawLayout = device.createBindGroupLayout({entries: [
                {binding: 0, visibility: 4, buffer: {}}, {binding: 1, visibility: 4, texture: {}}
            ]});
            const texture = device.createTexture({size: [2, 2], format: "rgba8unorm", usage: GPUTextureUsage.TEXTURE_BINDING});
            texture.createView = () => { throw Error("Internal conversion must not call an application override"); };
            device.pushErrorScope("validation");
            for (let i = 0; i < 2; ++i) {
                device.createBindGroup({layout: rawLayout, entries: [{binding: 0, resource: buffer}, {binding: 1, resource: texture}]});
            }
            check(await device.popErrorScope() === null, "Raw GPUBuffer or GPUTexture was rejected");
            let caught;
            try { device.createBindGroup({layout, entries:[{binding:0, get resource(){throw marker;}}]}); }
            catch (error) { caught = error; }
            check(caught === marker, "Entry getter exception was replaced");
            caught = undefined;
            try { device.createBindGroup({layout, entries:{length:0}}); } catch (error) { caught = error; }
            check(caught instanceof TypeError, "Non-iterable array-like entries must fail Web IDL conversion");
            const empty = device.createBindGroupLayout({entries: []});
            device.pushErrorScope("validation");
            device.createBindGroup({layout: empty, entries: []});
            check(await device.popErrorScope() === null, "Empty group failed");
            device.pushErrorScope("validation");
            device.createBindGroup({layout, entries:[{binding:0,resource:{buffer,size:0}},{binding:1,resource:sampler}]});
            check(await device.popErrorScope() instanceof GPUValidationError, "Explicit zero size was treated as the remainder of the buffer");
            const other = await adapter.requestDevice();
            const foreign = other.createSampler({});
            other.pushErrorScope("validation");
            device.pushErrorScope("validation");
            device.createBindGroup({layout, entries:[{binding:0,resource:{buffer}},{binding:1,resource:foreign}]});
            check(await device.popErrorScope() instanceof GPUValidationError, "Foreign resource accepted");
            check(await other.popErrorScope() === null, "Validation error reported to foreign owner");
            device.destroy(); other.destroy();
            __nativeWebGpuTestDone(true, "");
        })().catch(e => __nativeWebGpuTestDone(false, e.stack || String(e)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, RejectPropagatesExactMessage)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            try {
                await navigator.gpu._testAsyncReject("boom:async-bridge");
                throw new Error("Expected rejection but promise resolved.");
            } catch (error) {
                const message = String(error);
                if (message.indexOf("boom:async-bridge") === -1) {
                    throw new Error("Missing rejection message: " + message);
                }
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, RejectStackPreservesJavaScriptCallsiteFrames)
{
    RunNativeWebGpuAsyncScript(R"JS(
        async function failingPath() {
            return navigator.gpu._testAsyncReject("boom:stack-fidelity");
        }

        async function callerPath() {
            try {
                await failingPath();
                throw new Error("Expected rejection but promise resolved.");
            } catch (error) {
                const stack = String(error && error.stack ? error.stack : "");
                if (stack.indexOf("boom:stack-fidelity") === -1) {
                    throw new Error("Stack missing error message: " + stack);
                }
                if (stack.indexOf("failingPath") === -1) {
                    throw new Error("Stack missing failingPath frame: " + stack);
                }
                if (stack.indexOf("callerPath") === -1) {
                    throw new Error("Stack missing callerPath frame: " + stack);
                }
            }
        }

        callerPath().then(() => {
            __nativeWebGpuTestDone(true, "");
        }).catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, RejectCarriesOperationMetadataForTelemetry)
{
    RunNativeWebGpuAsyncScript(R"JS(
        async function callPath() {
            try {
                await navigator.gpu._testAsyncReject("boom:telemetry-fidelity");
                throw new Error("Expected rejection but promise resolved.");
            } catch (error) {
                if (!(error instanceof Error)) {
                    throw new Error("Rejection is not an Error instance.");
                }
                if (error.message !== "boom:telemetry-fidelity") {
                    throw new Error("Unexpected error message: " + error.message);
                }
                if (error.nativeOperation !== "NativeWebGPU._testAsyncReject") {
                    throw new Error("Unexpected nativeOperation metadata: " + String(error.nativeOperation));
                }
                const stack = String(error.stack || "");
                if (stack.indexOf("callPath") === -1) {
                    throw new Error("Stack missing JS callsite: " + stack);
                }
                if (stack.indexOf("[native async] NativeWebGPU._testAsyncReject") === -1) {
                    throw new Error("Stack missing native operation frame: " + stack);
                }
            }
        }

        callPath().then(() => {
            __nativeWebGpuTestDone(true, "");
        }).catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, ResolveFactoryThrowRejectsPromiseWithOperationMetadata)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            try {
                await navigator.gpu._testAsyncResolveFactoryThrows("boom:resolve-factory");
                throw new Error("Expected rejection but promise resolved.");
            } catch (error) {
                if (!(error instanceof Error)) {
                    throw new Error("Rejection is not an Error instance.");
                }
                if (error.message !== "boom:resolve-factory") {
                    throw new Error("Unexpected rejection message: " + error.message);
                }
                if (error.nativeOperation !== "NativeWebGPU._testAsyncResolveFactoryThrows") {
                    throw new Error("Unexpected nativeOperation metadata: " + String(error.nativeOperation));
                }
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, RustBackendErrorsThrowActionableJavaScriptException)
{
    RunNativeWebGpuAsyncScript(R"JS(
        function rustFailurePath() {
            navigator.gpu._testThrowRustError();
        }

        (async () => {
            try {
                rustFailurePath();
                throw new Error("Expected Rust backend failure to throw.");
            } catch (error) {
                if (!(error instanceof Error)) {
                    throw new Error("Rust backend failure was not an Error instance.");
                }
                if (error.nativeOperation !== "NativeWebGPU._testThrowRustError") {
                    throw new Error("Unexpected Rust nativeOperation metadata: " + String(error.nativeOperation));
                }
                const message = String(error.message || error);
                if (message.indexOf("NativeWebGPU._testThrowRustError") === -1 ||
                    message.indexOf("copyExternalImageToTexture source dimensions must be non-zero") === -1) {
                    throw new Error("Rust error message is not actionable: " + message);
                }
                const stack = String(error.stack || "");
                if (stack.indexOf("rustFailurePath") === -1 ||
                    stack.indexOf("[native] NativeWebGPU._testThrowRustError") === -1) {
                    throw new Error("Rust error stack is missing JS/native context: " + stack);
                }
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, CppExceptionsThrowActionableJavaScriptException)
{
    RunNativeWebGpuAsyncScript(R"JS(
        function cppFailurePath() {
            navigator.gpu._testThrowCppException();
        }

        (async () => {
            try {
                cppFailurePath();
                throw new Error("Expected C++ exception to throw.");
            } catch (error) {
                if (!(error instanceof Error)) {
                    throw new Error("C++ exception was not an Error instance.");
                }
                if (error.nativeOperation !== "NativeWebGPU._testThrowCppException") {
                    throw new Error("Unexpected C++ nativeOperation metadata: " + String(error.nativeOperation));
                }
                const message = String(error.message || error);
                if (message.indexOf("NativeWebGPU._testThrowCppException") === -1 ||
                    message.indexOf("simulated C++ exception") === -1) {
                    throw new Error("C++ exception message is not actionable: " + message);
                }
                const stack = String(error.stack || "");
                if (stack.indexOf("cppFailurePath") === -1 ||
                    stack.indexOf("[native] NativeWebGPU._testThrowCppException") === -1) {
                    throw new Error("C++ exception stack is missing JS/native context: " + stack);
                }
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, CreateRenderPipelineAsyncRejectsForInvalidDescriptor)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            if (!adapter) {
                throw new Error("requestAdapter returned null.");
            }

            const device = await adapter.requestDevice();
            if (!device) {
                throw new Error("requestDevice returned null.");
            }

            try {
                await device.createRenderPipelineAsync();
                throw new Error("Expected createRenderPipelineAsync to reject.");
            } catch (error) {
                const message = String(error);
                if (message.indexOf("descriptor") === -1) {
                    throw new Error("Unexpected rejection message: " + message);
                }
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, SynchronousPipelineValidationErrorsAreScopedAndDeviceSurvives)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            if (!adapter) {
                throw new Error("requestAdapter returned null.");
            }

            const device = await adapter.requestDevice();
            if (!device) {
                throw new Error("requestDevice returned null.");
            }

            const shader = device.createShaderModule({ code: `
                @vertex
                fn vs(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {
                    var positions = array<vec2f, 3>(
                        vec2f(-1.0, -1.0),
                        vec2f(3.0, -1.0),
                        vec2f(-1.0, 3.0));
                    return vec4f(positions[vertexIndex], 0.0, 1.0);
                }

                @fragment
                fn fs() -> @location(0) vec4f {
                    return vec4f(0.0, 0.0, 0.0, 1.0);
                }

                @compute @workgroup_size(1)
                fn cs() {}
            ` });

            async function expectPipelineFailure(operation, create) {
                    device.pushErrorScope("validation");
                    create();
                    const error = await device.popErrorScope();
                    if (!(error instanceof GPUValidationError)) {
                        throw new Error(operation + " did not produce GPUValidationError.");
                    }
                    const message = String(error.message || error);
                    if (message.toLowerCase().indexOf("validation error") === -1 ||
                        message.indexOf("missingEntryPoint") === -1) {
                        throw new Error(operation + " error was not actionable: " + message);
                    }
            }

            await expectPipelineFailure("GPUDevice.createRenderPipeline", () => {
                device.createRenderPipeline({
                    label: "invalid render pipeline",
                    vertex: { module: shader, entryPoint: "missingEntryPoint" },
                    fragment: {
                        module: shader,
                        entryPoint: "fs",
                        targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }]
                    }
                });
            });

            await expectPipelineFailure("GPUDevice.createComputePipeline", () => {
                device.createComputePipeline({
                    label: "invalid compute pipeline",
                    compute: { module: shader, entryPoint: "missingEntryPoint" }
                });
            });

            const renderPipeline = device.createRenderPipeline({
                label: "valid render pipeline after validation failure",
                vertex: { module: shader, entryPoint: "vs" },
                fragment: {
                    module: shader,
                    entryPoint: "fs",
                    targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }]
                }
            });
            const computePipeline = device.createComputePipeline({
                label: "valid compute pipeline after validation failure",
                compute: { module: shader, entryPoint: "cs" }
            });
            if (!renderPipeline || !computePipeline) {
                throw new Error("Device did not recover after pipeline validation failures.");
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, error && error.stack ? error.stack : String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, ErrorScopesCaptureFirstMatchingErrorWithoutLeaking)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const device = await (await navigator.gpu.requestAdapter()).requestDevice();
            const check = (ok, message) => { if (!ok) throw new Error(message); };
            let events = 0;
            device.onuncapturederror = e => { e.preventDefault(); ++events; };
            device.pushErrorScope('validation');
            device.pushErrorScope('validation');
            device.pushErrorScope('internal');
            device.createBuffer({size: 16, usage: 0xffff});
            device.createShaderModule({code: 'not valid WGSL'});
            check(await device.popErrorScope() === null, 'Wrong filter captured validation');
            const first = await device.popErrorScope();
            check(first instanceof GPUValidationError && /usage/.test(first.message), 'First error was replaced');
            check(await device.popErrorScope() === null, 'Occupied inner scope leaked to parent');
            device.pushErrorScope('validation');
            const encoder = device.createCommandEncoder();
            const src = device.createBuffer({size: 16, usage: GPUBufferUsage.COPY_SRC});
            const dst = device.createBuffer({size: 4, usage: GPUBufferUsage.COPY_DST});
            encoder.copyBufferToBuffer(src, dst, 16);
            encoder.finish();
            check(await device.popErrorScope() instanceof GPUValidationError, 'Encoding error missed finish scope');
            await new Promise(resolve => setTimeout(resolve, 10));
            check(events === 0, 'Captured errors leaked as events');
            device.destroy();
            __nativeWebGpuTestDone(true, '');
        })().catch(e => __nativeWebGpuTestDone(false, e.stack || String(e)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, LogicalDevicesIsolateErrorsResourcesAndLoss)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            const first = await adapter.requestDevice(), second = await adapter.requestDevice();
            const check = (ok, message) => { if (!ok) throw new Error(message); };
            const mapped = first.createBuffer({size: 16, usage: GPUBufferUsage.COPY_SRC, mappedAtCreation: true});
            const range = mapped.getMappedRange();
            const foreign = first.createBuffer({size: 16, usage: GPUBufferUsage.COPY_DST});
            first.pushErrorScope('validation'); second.pushErrorScope('validation');
            second.queue.writeBuffer(foreign, 0, new Uint32Array([1]));
            check(await second.popErrorScope() instanceof GPUValidationError, 'Cross-device resource accepted');
            check(await first.popErrorScope() === null, 'Error delivered to wrong device');
            const lost = first.lost;
            first.destroy(); first.destroy();
            check((await lost).reason === 'destroyed' && first.lost === lost, 'Loss identity or reason incorrect');
            check(range.byteLength === 0 && mapped.mapState === 'unmapped', 'Destroy retained active mapping');
            first.pushErrorScope('validation');
            first.createShaderModule({code: 'invalid'});
            first.queue.submit([]);
            check(await first.popErrorScope() === null, 'Lost device generated errors');
            const data = second.createBuffer({size: 4, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST});
            const readback = second.createBuffer({size: 4, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
            second.queue.writeBuffer(data, 0, new Uint32Array([0x12345678]));
            const encoder = second.createCommandEncoder(); encoder.copyBufferToBuffer(data, readback);
            second.queue.submit([encoder.finish()]);
            await readback.mapAsync(GPUMapMode.READ);
            check(new Uint32Array(readback.getMappedRange())[0] === 0x12345678, 'Destroy broke another device');
            readback.unmap(); second.destroy();
            __nativeWebGpuTestDone(true, '');
        })().catch(e => __nativeWebGpuTestDone(false, e.stack || String(e)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, MapCompletionHandlesCancellationRemapAndQueueOrder)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const device = await (await navigator.gpu.requestAdapter()).requestDevice();
            const check = (ok, message) => { if (!ok) throw Error(message); };
            const make = () => device.createBuffer({size: 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
            const a = make(), b = make(), order = [];
            device.queue.writeBuffer(a, 0, new Uint32Array([17, 23]));
            const aborted = a.mapAsync(GPUMapMode.READ).then(() => "resolved", e => e.name);
            a.unmap();
            const next = a.mapAsync(GPUMapMode.READ).then(() => {
                check(new Uint32Array(a.getMappedRange())[1] === 23, "remap lost queued bytes");
                order.push("map");
            });
            const other = b.mapAsync(GPUMapMode.READ).then(() => order.push("other"));
            const done = device.queue.onSubmittedWorkDone().then(() => order.push("queue"));
            check(a.mapState === "pending", "mapping completed inline");
            check(await aborted === "AbortError", "cancelled request did not reject");
            await Promise.all([next, other, done]);
            check(order.length === 3 && order[2] === "queue", "queue settled ahead of an earlier map: " + order);
            a.unmap(); b.unmap();
            for (const destroyDevice of [false, true]) {
                const buffer = make();
                const pending = buffer.mapAsync(GPUMapMode.READ).then(() => "resolved", e => e.name);
                if (destroyDevice) device.destroy(); else buffer.destroy();
                check(await pending === "AbortError", "destroy did not cancel map");
            }
            __nativeWebGpuTestDone(true, "");
        })().catch(e => __nativeWebGpuTestDone(false, e.stack || String(e)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, AsyncPipelineErrorsRejectWithoutScopeOrEventLeakage)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const device = await (await navigator.gpu.requestAdapter()).requestDevice();
            const check = (ok, message) => { if (!ok) throw new Error(message); };
            const shader = device.createShaderModule({code: '@compute @workgroup_size(1) fn main() {}'});
            let events = 0;
            device.onuncapturederror = e => { e.preventDefault(); ++events; };
            device.pushErrorScope('validation');
            const result = device.createComputePipelineAsync({layout: 'auto', compute: {module: shader, entryPoint: 'missing'}});
            const popped = device.popErrorScope();
            let failure;
            try { await result; } catch (error) { failure = error; }
            check(failure instanceof GPUPipelineError && failure.reason === 'validation', 'Wrong async pipeline error');
            check(await popped === null, 'Async pipeline error leaked into scope');
            await new Promise(resolve => setTimeout(resolve, 10));
            check(events === 0, 'Async pipeline error leaked into event');
            const pending = device.createComputePipelineAsync({layout: 'auto', compute: {module: shader, entryPoint: 'main'}});
            device.destroy();
            check(await pending instanceof GPUComputePipeline, 'Destroyed device did not resolve invalid pipeline');
            __nativeWebGpuTestDone(true, '');
        })().catch(e => __nativeWebGpuTestDone(false, e.stack || String(e)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, QueuedCompilesSnapshotDescriptorsAndKeepDevicesIsolated)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            const a = await adapter.requestDevice(), b = await adapter.requestDevice();
            const shader = device => device.createShaderModule({code:"@compute @workgroup_size(1) fn entry() {}"});
            const descriptor = {layout:"auto", compute:{module:shader(a), entryPoint:"entry"}};
            a.pushErrorScope("validation");
            b.pushErrorScope("validation");
            const goodA = a.createComputePipelineAsync(descriptor);
            descriptor.compute.module = shader(b);
            const wrong = a.createComputePipelineAsync(descriptor);
            const goodB = b.createComputePipelineAsync(descriptor);
            descriptor.compute.entryPoint = "missing";
            const bad = b.createComputePipelineAsync(descriptor);
            const scopes = Promise.all([a.popErrorScope(), b.popErrorScope()]);
            const [one, two, three, four] = await Promise.allSettled([goodA, wrong, goodB, bad]);
            for (const r of [one, three]) {
                if (r.status !== "fulfilled" || !(r.value instanceof GPUComputePipeline)) throw Error("Lost valid pipeline");
            }
            for (const r of [two, four]) {
                if (r.status !== "rejected" || !(r.reason instanceof GPUPipelineError) || r.reason.reason !== "validation") throw Error("Lost validation failure");
            }
            if ((await scopes).some(Boolean)) throw Error("Error leaked between compiles");
            a.destroy(); b.destroy();
            __nativeWebGpuTestDone(true, "");
        })().catch(e => __nativeWebGpuTestDone(false, e.stack || String(e)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, RenderPassMethodsAreSharedWithoutCapturingTheFirstDevice)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            const devices = [await adapter.requestDevice(), await adapter.requestDevice()];
            const methods = ["setPipeline", "setBindGroup", "setVertexBuffer", "setIndexBuffer",
                "draw", "drawIndexed", "drawIndirect", "drawIndexedIndirect", "end"];
            for (const device of devices) device.pushErrorScope("validation");
            let first;
            for (const index of [0, 0, 1]) {
                const device = devices[index];
                const target = device.createTexture({size: [2, 2], format: "rgba8unorm", usage: GPUTextureUsage.RENDER_ATTACHMENT});
                const encoder = device.createCommandEncoder();
                const pass = encoder.beginRenderPass({colorAttachments: [{view: target.createView(),
                    loadOp: "clear", storeOp: "store", clearValue: [index, 0, 0, 1]}]});
                for (const name of methods) {
                    if (Object.prototype.hasOwnProperty.call(pass, name) ||
                        pass[name] !== GPURenderPassEncoder.prototype[name] ||
                        (first && pass[name] !== first[name])) {
                        throw new Error("Render pass method is not shared: " + name);
                    }
                }
                first = first || pass;
                pass.end();
                device.queue.submit([encoder.finish()]);
                await device.queue.onSubmittedWorkDone();
                target.destroy();
            }
            for (const device of devices) {
                const error = await device.popErrorScope();
                if (error) throw new Error("Shared method used the wrong device: " + error.message);
            }
            __nativeWebGpuTestDone(true, "");
        })().catch((error) => __nativeWebGpuTestDone(false, String(error)));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, SharedRenderPassMethodsReportForeignResourcesToReceiverDevice)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            const adapter = await navigator.gpu.requestAdapter();
            const devices = [await adapter.requestDevice(), await adapter.requestDevice()];
            const buffers = devices.map(device => device.createBuffer({size: 16, usage: GPUBufferUsage.VERTEX}));
            for (const index of [0, 1]) {
                const device = devices[index];
                const other = devices[1 - index];
                const target = device.createTexture({size: [2, 2], format: "rgba8unorm", usage: GPUTextureUsage.RENDER_ATTACHMENT});
                const encoder = device.createCommandEncoder();
                const pass = encoder.beginRenderPass({colorAttachments: [{view: target.createView(),
                    loadOp: "clear", storeOp: "store"}]});
                device.pushErrorScope("validation");
                other.pushErrorScope("validation");
                GPURenderPassEncoder.prototype.setVertexBuffer.call(pass, 0, buffers[1 - index]);
                pass.end();
                device.queue.submit([encoder.finish()]);
                const error = await device.popErrorScope();
                if (!(error instanceof GPUValidationError) || !/different GPUDevice/.test(error.message)) {
                    throw new Error("Receiver device did not capture the foreign-buffer error: " + index);
                }
                if (await other.popErrorScope() !== null) {
                    throw new Error("Foreign-buffer error leaked to the other device: " + index);
                }
                await device.queue.onSubmittedWorkDone();
                target.destroy();
            }
            devices.forEach(device => device.destroy());
            __nativeWebGpuTestDone(true, "");
        })().catch(error => __nativeWebGpuTestDone(false, String(error) + "\n" + (error.stack || "")));
    )JS");
}

TEST(NativeWebGPUAsyncBridge, RenderPassEncoderExposesDebugMarkerMethods)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            try {
                const adapter = await navigator.gpu.requestAdapter();
                const device = await adapter.requestDevice();
                const context = navigator.gpu._createCanvasContext();
                context.configure({
                    device,
                    format: navigator.gpu.getPreferredCanvasFormat(),
                    width: 64,
                    height: 64
                });

                const encoder = device.createCommandEncoder({ label: "debug marker unit test" });
                for (const method of ["pushDebugGroup", "popDebugGroup", "insertDebugMarker"]) {
                    if (typeof encoder[method] !== "function") {
                        throw new Error("GPUCommandEncoder missing " + method);
                    }
                }

                encoder.pushDebugGroup("encoder group");
                encoder.insertDebugMarker("encoder marker");

                const pass = encoder.beginRenderPass({
                    colorAttachments: [{
                        view: context.getCurrentTexture().createView(),
                        loadOp: "clear",
                        storeOp: "store",
                        clearValue: { r: 0, g: 0, b: 0, a: 1 }
                    }]
                });
                for (const method of ["pushDebugGroup", "popDebugGroup", "insertDebugMarker"]) {
                    if (typeof pass[method] !== "function") {
                        throw new Error("GPURenderPassEncoder missing " + method);
                    }
                }

                pass.pushDebugGroup("pass group");
                pass.insertDebugMarker("pass marker");
                pass.popDebugGroup();
                pass.end();

                encoder.popDebugGroup();
                device.queue.submit([encoder.finish()]);
                await device.queue.onSubmittedWorkDone();

                __nativeWebGpuTestDone(true, "");
            } catch (error) {
                __nativeWebGpuTestDone(false, error && error.stack ? error.stack : String(error));
            }
        })();
    )JS");
}

TEST(NativeWebGPUAsyncBridge, SetPipelineWithoutDrawActivatesNativeDrawPath)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            if (typeof navigator.gpu._testResetDebugStats !== "function") {
                throw new Error("Missing _testResetDebugStats test hook.");
            }
            navigator.gpu._testResetDebugStats();

            const adapter = await navigator.gpu.requestAdapter();
            if (!adapter) {
                throw new Error("requestAdapter returned null.");
            }

            const device = await adapter.requestDevice();
            if (!device) {
                throw new Error("requestDevice returned null.");
            }

            const context = navigator.gpu._createCanvasContext();
            context.configure({
                device,
                format: navigator.gpu.getPreferredCanvasFormat(),
                width: 64,
                height: 64
            });

            const before = navigator.gpu._debugStats();
            const shader = device.createShaderModule({ code: `
                @vertex
                fn vs(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {
                    var positions = array<vec2f, 3>(
                        vec2f(-1.0, -1.0),
                        vec2f(3.0, -1.0),
                        vec2f(-1.0, 3.0));
                    return vec4f(positions[vertexIndex], 0.0, 1.0);
                }

                @fragment
                fn fs() -> @location(0) vec4f {
                    return vec4f(0.0, 0.0, 0.0, 1.0);
                }` });
            const pipeline = device.createRenderPipeline({
                vertex: { module: shader, entryPoint: "vs" },
                fragment: {
                    module: shader,
                    entryPoint: "fs",
                    targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }]
                }
            });
            const encoder = device.createCommandEncoder();
            const pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: context.getCurrentTexture().createView(),
                    loadOp: "clear",
                    storeOp: "store",
                    clearValue: { r: 0, g: 0, b: 0, a: 1 }
                }]
            });
            pass.setPipeline(pipeline);
            pass.end();
            device.queue.submit([encoder.finish()]);

            const after = navigator.gpu._debugStats();
            if (after.drawPathActive !== true) {
                throw new Error("Expected draw path to become active after setPipeline.");
            }
            if (after.drawCallCount !== before.drawCallCount) {
                throw new Error("setPipeline-only pass should not increment drawCallCount.");
            }
            if (after.queueSubmitCount <= before.queueSubmitCount) {
                throw new Error("Expected queue submit count to increment.");
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}

TEST(NativeWebGPUAsyncBridge, DrawIndirectIncrementsNativeDrawCounters)
{
    RunNativeWebGpuAsyncScript(R"JS(
        (async () => {
            if (typeof navigator.gpu._testResetDebugStats !== "function") {
                throw new Error("Missing _testResetDebugStats test hook.");
            }
            navigator.gpu._testResetDebugStats();

            const adapter = await navigator.gpu.requestAdapter();
            if (!adapter) {
                throw new Error("requestAdapter returned null.");
            }

            const device = await adapter.requestDevice();
            if (!device) {
                throw new Error("requestDevice returned null.");
            }

            const context = navigator.gpu._createCanvasContext();
            context.configure({
                device,
                format: navigator.gpu.getPreferredCanvasFormat(),
                width: 64,
                height: 64
            });

            const before = navigator.gpu._debugStats();
            const shader = device.createShaderModule({ code: `
                @vertex
                fn vs(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {
                    var positions = array<vec2f, 3>(
                        vec2f(-1.0, -1.0),
                        vec2f(3.0, -1.0),
                        vec2f(-1.0, 3.0));
                    return vec4f(positions[vertexIndex], 0.0, 1.0);
                }

                @fragment
                fn fs() -> @location(0) vec4f {
                    return vec4f(0.0, 0.0, 0.0, 1.0);
                }` });
            const pipeline = device.createRenderPipeline({
                vertex: { module: shader, entryPoint: "vs" },
                fragment: {
                    module: shader,
                    entryPoint: "fs",
                    targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }]
                }
            });
            const encoder = device.createCommandEncoder();
            const pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: context.getCurrentTexture().createView(),
                    loadOp: "clear",
                    storeOp: "store",
                    clearValue: { r: 0, g: 0, b: 0, a: 1 }
                }]
            });
            pass.setPipeline(pipeline);

            const indirectBuffer = device.createBuffer({
                size: 16,
                usage: GPUBufferUsage.INDIRECT
            });
            pass.drawIndirect(indirectBuffer, 0);
            pass.end();
            device.queue.submit([encoder.finish()]);

            const after = navigator.gpu._debugStats();
            if (after.drawPathActive !== true) {
                throw new Error("Expected draw path to become active after drawIndirect.");
            }
            if (after.drawCallCount <= before.drawCallCount) {
                throw new Error("Expected drawCallCount to increment after drawIndirect.");
            }

            __nativeWebGpuTestDone(true, "");
        })().catch((error) => {
            __nativeWebGpuTestDone(false, String(error));
        });
    )JS");
}
