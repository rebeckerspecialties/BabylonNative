#include <gtest/gtest.h>

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/Canvas.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/ScriptLoader.h>

#if defined(BABYLON_NATIVE_UNITTESTS_WITH_NATIVEENGINE)
#include <Babylon/Plugins/NativeEngine.h>
#endif

#if defined(BABYLON_NATIVE_UNITTESTS_WITH_NATIVEENCODING)
#include <Babylon/Plugins/NativeEncoding.h>
#endif

#if defined(BABYLON_NATIVE_UNITTESTS_WITH_WEBGPU)
#include <Babylon/Plugins/NativeWebGPU.h>
#endif

#include <cstdlib>

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

        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Polyfills::Console::Initialize(env, [](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::cout << "[" << EnumToString(logLevel) << "] " << message << std::endl;
        });
        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);
        nativeCanvas.emplace(Babylon::Polyfills::Canvas::Initialize(env));

#if defined(BABYLON_NATIVE_UNITTESTS_WITH_NATIVEENGINE)
        Babylon::Plugins::NativeEngine::Initialize(env);
#endif

#if defined(BABYLON_NATIVE_UNITTESTS_WITH_NATIVEENCODING)
        Babylon::Plugins::NativeEncoding::Initialize(env);
#endif

#if defined(BABYLON_NATIVE_UNITTESTS_WITH_WEBGPU)
        Babylon::Plugins::NativeWebGPU::Initialize(env);
#endif

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

#if defined(BABYLON_NATIVE_UNITTESTS_WITH_NATIVEENGINE)
    loader.LoadScript("app:///Assets/babylon.max.js");
    loader.LoadScript("app:///Assets/babylonjs.materials.js");
    loader.LoadScript("app:///Assets/tests.javaScript.all.js");
#elif defined(BABYLON_NATIVE_UNITTESTS_WITH_WEBGPU)
    loader.LoadScript("app:///Assets/tests.wgpu.js");
#else
#error "UnitTests JavaScript suite requires NativeEngine or NativeWebGPU."
#endif

    device.StartRenderingCurrentFrame();
    device.FinishRenderingCurrentFrame();

    auto exitCode{exitCodePromise.get_future().get()};
    EXPECT_EQ(exitCode, 0);
}
