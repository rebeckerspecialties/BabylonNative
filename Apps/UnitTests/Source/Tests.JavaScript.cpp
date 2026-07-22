#include <gtest/gtest.h>

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Plugins/NativeEncoding.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/Canvas.h>
#include <Babylon/Polyfills/Compression.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Fetch.h>
#include <Babylon/Polyfills/File.h>
#include <Babylon/Polyfills/Streams.h>
#include <Babylon/Polyfills/TextDecoder.h>
#include <Babylon/Polyfills/TextEncoder.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/ScriptLoader.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <optional>

extern Babylon::Graphics::Configuration g_deviceConfig;

namespace
{
    const char* EnumToString(Babylon::Polyfills::Console::LogLevel logLevel)
    {
        switch (logLevel)
        {
            case Babylon::Polyfills::Console::LogLevel::Log:
                return "log";
            case Babylon::Polyfills::Console::LogLevel::Warn:
                return "warn";
            case Babylon::Polyfills::Console::LogLevel::Error:
                return "error";
        }

        return "unknown";
    }
}

TEST(JavaScript, All)
{
    // Change this to true to wait for the JavaScript debugger to attach (only applies to V8)
    constexpr const bool waitForDebugger = false;

    Babylon::Graphics::Device device{g_deviceConfig};

    // Start rendering a frame to unblock the JavaScript from queuing graphics
    // commands. The frame is held open through script load and the test pump
    // (which only ticks bgfx via Finish; Start) so the JS thread can submit
    // at any time without racing the gate. A final Finish closes it after
    // runtime teardown.
    device.StartRenderingCurrentFrame();

    std::optional<Babylon::Polyfills::Canvas> nativeCanvas;

    Babylon::AppRuntime::Options options{};

    options.UnhandledExceptionHandler = [](const Napi::Error& error) {
        std::cerr << "[Uncaught Error] " << Napi::GetErrorString(error) << std::endl;
        std::quick_exit(1);
    };

    if (waitForDebugger)
    {
        std::cout << "Waiting for debugger..." << std::endl;
        options.WaitForDebugger = true;
    }

    Babylon::AppRuntime runtime{options};

    std::promise<int32_t> exitCodePromise;

    runtime.Dispatch([&exitCodePromise, &device, &nativeCanvas](Napi::Env env) {
        device.AddToJavaScript(env);

        Babylon::Polyfills::Console::Initialize(env, [](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::cout << "[" << EnumToString(logLevel) << "] " << message << std::endl;
        });
        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::Streams::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);
        Babylon::Polyfills::File::Initialize(env);
        Babylon::Polyfills::TextDecoder::Initialize(env);
        Babylon::Polyfills::TextEncoder::Initialize(env);
        Babylon::Polyfills::Compression::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Polyfills::Fetch::Initialize(env);
        nativeCanvas.emplace(Babylon::Polyfills::Canvas::Initialize(env));
        Babylon::Plugins::NativeEngine::Initialize(env);
        Babylon::Plugins::NativeEncoding::Initialize(env);

        auto setExitCodeCallback = Napi::Function::New(
            env, [&exitCodePromise](const Napi::CallbackInfo& info) {
                Napi::Env env = info.Env();
                exitCodePromise.set_value(info[0].As<Napi::Number>().Int32Value());
            },
            "setExitCode");
        env.Global().Set("setExitCode", setExitCodeCallback);
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("location = { href: '' };", ""); // Required for Mocha.js as we do not have a location in Babylon Native
    loader.LoadScript("app:///Assets/babylon.max.js");
    loader.LoadScript("app:///Assets/babylonjs.materials.js");
    loader.LoadScript("app:///Assets/tests.javaScript.all.js");

    // Pump frames while JS tests run - tests use RAF internally and
    // SubmitCommands requires an active frame. The frame was opened
    // immediately after device creation; the loop just ticks bgfx
    // (Finish; Start) once per iteration so commands can advance.
    auto exitCodeFuture = exitCodePromise.get_future();
    while (exitCodeFuture.wait_for(std::chrono::milliseconds(16)) != std::future_status::ready)
    {
        device.FinishRenderingCurrentFrame();
        device.StartRenderingCurrentFrame();
    }

    auto exitCode = exitCodeFuture.get();
    EXPECT_EQ(exitCode, 0);

    // Runtime destructor joins the JS thread; must happen before Finish.
    nativeCanvas.reset();

    device.FinishRenderingCurrentFrame();
}

TEST(JavaScript, BrowserStreamResponseCompressionIntegration)
{
    Babylon::AppRuntime runtime{};
    std::promise<std::string> scriptDonePromise;

    runtime.Dispatch([&scriptDonePromise](Napi::Env env) {
        Babylon::Polyfills::Streams::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);
        Babylon::Polyfills::File::Initialize(env);
        Babylon::Polyfills::TextDecoder::Initialize(env);
        Babylon::Polyfills::TextEncoder::Initialize(env);
        Babylon::Polyfills::Compression::Initialize(env);
        Babylon::Polyfills::Fetch::Initialize(env);
        env.Global().Set("__browserPolyfillTestDone", Napi::Function::New(env, [&scriptDonePromise](const Napi::CallbackInfo& info) {
            scriptDonePromise.set_value(info.Length() > 0 && info[0].IsString() ? info[0].As<Napi::String>().Utf8Value() : std::string{});
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"JS(
        (async () => {
            const prefix = new Uint8Array([110, 97, 116, 105, 118, 101, 32]);
            const blob = new Blob([prefix.subarray(1), "streams"], { type: "Text/Plain" });
            if (blob.type !== "text/plain" || blob.size !== 13) {
                throw new Error("Blob did not preserve browser-shaped type and size semantics.");
            }

            const compressedBody = blob.stream().pipeThrough(new CompressionStream("gzip"));
            const compressedResponse = new Response(compressedBody, {
                headers: [["X-Native-Test", " first "], ["x-native-test", "second"]]
            });
            if (compressedResponse.headers.get("X-NATIVE-TEST") !== "first, second") {
                throw new Error("Headers did not normalize and combine values.");
            }

            const compressedBytes = new Uint8Array(await compressedResponse.arrayBuffer());
            if (compressedBytes.length === 0 || !compressedResponse.bodyUsed) {
                throw new Error("Response did not consume the compressed stream.");
            }

            let secondReadRejected = false;
            try {
                await compressedResponse.arrayBuffer();
            } catch (error) {
                secondReadRejected = error instanceof TypeError;
            }
            if (!secondReadRejected) {
                throw new Error("A disturbed Response body was readable twice.");
            }

            const restoredResponse = new Response(
                new Blob([compressedBytes]).stream().pipeThrough(new DecompressionStream("gzip"))
            );
            if (await restoredResponse.text() !== "ative streams") {
                throw new Error("Compression stream round trip changed the payload.");
            }

            __browserPolyfillTestDone("");
        })().catch((error) => {
            __browserPolyfillTestDone(error && error.stack ? String(error.stack) : String(error));
        });
    )JS", "browser-polyfill.integration.test.js");

    auto scriptDoneFuture = scriptDonePromise.get_future();
    ASSERT_EQ(scriptDoneFuture.wait_for(std::chrono::seconds{30}), std::future_status::ready) << "Browser polyfill integration test timed out.";
    EXPECT_TRUE(scriptDoneFuture.get().empty());
}
