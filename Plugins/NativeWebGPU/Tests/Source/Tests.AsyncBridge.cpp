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
