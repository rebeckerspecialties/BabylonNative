#include "AppContext.h"
#include "Diagnostics.h"

#include <Babylon/AppRuntime.h>
#include <Babylon/DebugTrace.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/ScriptLoader.h>

#include <Babylon/Plugins/NativeInput.h>
#if defined(BABYLON_NATIVE_PLAYGROUND_HAS_NATIVEOPTIMIZATIONS)
#include <Babylon/Plugins/NativeOptimizations.h>
#endif
#include <Babylon/Plugins/NativeWebGPU.h>
#if defined(BABYLON_NATIVE_PLAYGROUND_HAS_TESTUTILS)
#include <Babylon/Plugins/TestUtils.h>
#endif

#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/URL.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>

#include <cstdlib>
#include <sstream>

namespace
{
    const char* GetLogLevelString(Babylon::Polyfills::Console::LogLevel logLevel)
    {
        switch (logLevel)
        {
            case Babylon::Polyfills::Console::LogLevel::Log:
                return "Log";
            case Babylon::Polyfills::Console::LogLevel::Warn:
                return "Warn";
            case Babylon::Polyfills::Console::LogLevel::Error:
                return "Error";
            default:
                return "";
        }
    }
}

AppContext::AppContext(
    Babylon::Graphics::WindowT window,
    size_t width,
    size_t height,
    DebugLogCallback debugLog,
    AdditionalInitCallback additionalInit,
    PlaygroundOptions playgroundOptions)
{
    Babylon::DebugTrace::EnableDebugTrace(playgroundOptions.DebugTrace.value_or(true));
    Babylon::DebugTrace::SetTraceOutput(debugLog);

    Babylon::Graphics::Configuration graphicsConfig{};
    graphicsConfig.Window = window;
    graphicsConfig.Width = width;
    graphicsConfig.Height = height;
    graphicsConfig.MSAASamples = 4;

    m_device.emplace(graphicsConfig);
    m_deviceUpdate.emplace(m_device->GetUpdate("update"));

    m_device->StartRenderingCurrentFrame();
    m_deviceUpdate->Start();

    Babylon::AppRuntime::Options options{};
    options.EnableDebugger = true;
    options.UnhandledExceptionHandler = [debugLog](const Napi::Error& error) {
        std::ostringstream ss{};
        ss << "[Uncaught Error] " << Napi::GetErrorString(error);
        debugLog(ss.str().data());
        std::abort();
    };

    m_runtime.emplace(options);

    // Initialization ordering guarantee: AppRuntime::Dispatch uses a FIFO
    // WorkQueue. This callback runs on the JS thread before any ScriptLoader
    // work because ScriptLoader also dispatches through the same WorkQueue,
    // and it is constructed after this Dispatch call. This means navigator.gpu,
    // _native.Canvas, and all other N-API modules are fully available before any
    // user JavaScript executes.
    m_runtime->Dispatch([this, window, debugLog, additionalInit = std::move(additionalInit), playgroundOptions = std::move(playgroundOptions)](Napi::Env env) {
        m_device->AddToJavaScript(env);
#if defined(BABYLON_NATIVE_PLAYGROUND_HAS_TESTUTILS)
        Babylon::Plugins::TestUtils::Initialize(env, window);
#endif

        {
            auto js = Napi::Object::New(env);
            js.Set("listTests",          Napi::Boolean::New(env, playgroundOptions.ListTests));
            js.Set("headless",           Napi::Boolean::New(env, playgroundOptions.Headless));
            js.Set("breakOnFail",        Napi::Boolean::New(env, playgroundOptions.BreakOnFail));
            js.Set("generateReferences", Napi::Boolean::New(env, playgroundOptions.GenerateReferences));
            js.Set("runOnce",            Napi::Boolean::New(env, playgroundOptions.RunOnce));
            js.Set("includeExcluded",    Napi::Boolean::New(env, playgroundOptions.IncludeExcluded));
            if (playgroundOptions.SaveResults.has_value())
            {
                js.Set("saveResults", Napi::Boolean::New(env, *playgroundOptions.SaveResults));
            }
            if (playgroundOptions.CaptureFrame.has_value())
            {
                js.Set("captureFrame", Napi::Number::New(env, *playgroundOptions.CaptureFrame));
            }

            auto filters = Napi::Array::New(env, playgroundOptions.TestFilters.size());
            for (uint32_t i = 0; i < playgroundOptions.TestFilters.size(); ++i)
            {
                filters[i] = Napi::String::New(env, playgroundOptions.TestFilters[i]);
            }
            js.Set("testFilters", filters);

            auto indices = Napi::Array::New(env, playgroundOptions.TestIndices.size());
            for (uint32_t i = 0; i < playgroundOptions.TestIndices.size(); ++i)
            {
                indices[i] = Napi::Number::New(env, playgroundOptions.TestIndices[i]);
            }
            js.Set("testIndices", indices);

            env.Global().Set("_playgroundOptions", js);
        }

        Babylon::Polyfills::Blob::Initialize(env);
#if defined(BABYLON_NATIVE_PLAYGROUND_HAS_CANVAS)
        m_canvas.emplace(Babylon::Polyfills::Canvas::Initialize(env));
#endif

        Babylon::Polyfills::Console::Initialize(env, [env, debugLog](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::ostringstream ss{};
            ss << "[" << GetLogLevelString(logLevel) << "] " << message;
            debugLog(ss.str().data());

            // Promote console.error to a banner with JS + native callstack.
            // Babylon.js routes recoverable errors here.
            if (logLevel == Babylon::Polyfills::Console::LogLevel::Error)
            {
                auto jsStack = Babylon::Polyfills::Console::CaptureCurrentJsStack(env);
                Diagnostics::DumpFailure(
                    "JS CONSOLE ERROR",
                    nullptr,
                    0,
                    0,
                    "%s%s%s",
                    message != nullptr ? message : "(null)",
                    jsStack.empty() ? "" : "\nJS callstack:\n",
                    jsStack.c_str());
            }
        });

        Babylon::Polyfills::Performance::Initialize(env);

        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::URL::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);

        m_input = &Babylon::Plugins::NativeInput::CreateForJavaScript(env);
#if defined(BABYLON_NATIVE_PLAYGROUND_HAS_NATIVEOPTIMIZATIONS)
        Babylon::Plugins::NativeOptimizations::Initialize(env);
#endif
        Babylon::Plugins::NativeWebGPU::Initialize(env);

        if (additionalInit)
        {
            additionalInit(env);
        }
    });

    m_scriptLoader.emplace(*m_runtime);
    m_scriptLoader->LoadScript("app:///Scripts/ammo.js");
    m_scriptLoader->LoadScript("app:///Scripts/babylon.max.js");
    m_scriptLoader->LoadScript("app:///Scripts/babylonjs.loaders.js");
    m_scriptLoader->LoadScript("app:///Scripts/babylonjs.materials.js");
    m_scriptLoader->LoadScript("app:///Scripts/babylon.gui.js");
    m_scriptLoader->LoadScript("app:///Scripts/meshwriter.min.js");
    m_scriptLoader->LoadScript("app:///Scripts/babylonjs.serializers.js");
    m_scriptLoader->Dispatch([this](Napi::Env) {
        m_device->EnableRendering();
    });
}

AppContext::~AppContext()
{
    if (m_device)
    {
        m_deviceUpdate->Finish();
        m_device->FinishRenderingCurrentFrame();
    }

    Babylon::Plugins::ShaderCache::Disable();

    m_scriptLoader.reset();
    m_input = {};
    m_runtime.reset();
    m_deviceUpdate.reset();
    m_device.reset();
}
