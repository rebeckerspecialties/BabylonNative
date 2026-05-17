// PlaygroundBootstrap.mm — calls into Apps/Playground/Shared so the bootstrap
// script list and validation options stay in one place across Apple hosts.

#import "PlaygroundBootstrap.h"

#import <Babylon/Embedding/Apple/BNRuntime.h>

#include <Babylon/Embedding/Runtime.h>
#include <Shared/CommandLine.h>
#include <Shared/PlaygroundScripts.h>
#include <napi/napi.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Re-declare the internal class extension that exposes the C++ Runtime*
// from BNRuntime (implementation lives in Embedding/Apple/Source/BNRuntime.mm).
@interface BNRuntime ()
- (Babylon::Embedding::Runtime*)nativeRuntime;
@end

namespace
{
    bool EndsWith(std::string_view value, std::string_view suffix)
    {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool IsValidationScript(std::string_view script)
    {
        return EndsWith(script, "validation_native.js") || EndsWith(script, "validation_webgpu_native.js");
    }

    bool IsWebGPUValidationScript(std::string_view script)
    {
        return EndsWith(script, "validation_webgpu_native.js");
    }

    bool HasValidationIntent(const PlaygroundOptions& options)
    {
        return options.ListTests ||
            options.BreakOnFail ||
            options.GenerateReferences ||
            options.RunOnce ||
            options.IncludeExcluded ||
            options.SaveResults.has_value() ||
            options.CaptureFrame.has_value() ||
            !options.TestFilters.empty() ||
            !options.TestIndices.empty();
    }

    PlaygroundOptions ParsePlaygroundOptionsFromProcess()
    {
        NSArray* arguments = [[NSProcessInfo processInfo] arguments];
        std::vector<std::string> storage{};
        storage.reserve(arguments.count);
        std::vector<const char*> argv{};
        argv.reserve(arguments.count);

        for (NSString* argument in arguments)
        {
            storage.emplace_back([argument UTF8String]);
        }

        for (const auto& argument : storage)
        {
            argv.emplace_back(argument.c_str());
        }

        return CommandLine::Parse(static_cast<int>(argv.size()), argv.data());
    }

    void QueuePlaygroundOptions(Babylon::Embedding::Runtime& runtime, PlaygroundOptions options)
    {
        runtime.RunOnJsThread([options = std::move(options)](Napi::Env env) {
            auto js = Napi::Object::New(env);
            js.Set("listTests", Napi::Boolean::New(env, options.ListTests));
            js.Set("headless", Napi::Boolean::New(env, options.Headless));
            js.Set("breakOnFail", Napi::Boolean::New(env, options.BreakOnFail));
            js.Set("generateReferences", Napi::Boolean::New(env, options.GenerateReferences));
            js.Set("runOnce", Napi::Boolean::New(env, options.RunOnce));
            js.Set("includeExcluded", Napi::Boolean::New(env, options.IncludeExcluded));
            if (options.SaveResults.has_value())
            {
                js.Set("saveResults", Napi::Boolean::New(env, *options.SaveResults));
            }
            if (options.CaptureFrame.has_value())
            {
                js.Set("captureFrame", Napi::Number::New(env, *options.CaptureFrame));
            }

            auto filters = Napi::Array::New(env, options.TestFilters.size());
            for (uint32_t idx = 0; idx < options.TestFilters.size(); ++idx)
            {
                filters[idx] = Napi::String::New(env, options.TestFilters[idx]);
            }
            js.Set("testFilters", filters);

            auto indices = Napi::Array::New(env, options.TestIndices.size());
            for (uint32_t idx = 0; idx < options.TestIndices.size(); ++idx)
            {
                indices[idx] = Napi::Number::New(env, options.TestIndices[idx]);
            }
            js.Set("testIndices", indices);

            env.Global().Set("_playgroundOptions", js);
        }, true);
    }

    void QueueSelectedScripts(Babylon::Embedding::Runtime& runtime, const PlaygroundOptions& options)
    {
        if (options.Scripts.empty() && !HasValidationIntent(options))
        {
            runtime.Eval(
                "(function(){"
                "globalThis.createScene=undefined;"
                "globalThis.__babylonPlaygroundSceneFactoryReady=undefined;"
                "globalThis.__babylonPlaygroundWebGpuSmokeReady=undefined;"
                "globalThis.__webgpuSmokeDispose=undefined;"
                "})();",
                "app:///Scripts/playground_bootstrap_reset.js");
            runtime.LoadScript("app:///Scripts/webgpu_smoke.js");
            runtime.LoadScript("app:///Scripts/playground_runner.js");
            return;
        }

        if (options.Scripts.empty())
        {
            runtime.LoadScript("app:///Scripts/validation_webgpu_native.js");
            runtime.LoadScript("app:///Scripts/validation_native.js");
            return;
        }

        bool validationScriptLoaded = false;
        bool nativeValidationScriptLoaded = false;
        for (const auto& script : options.Scripts)
        {
            runtime.LoadScript(script);
            validationScriptLoaded = validationScriptLoaded || IsValidationScript(script);
            nativeValidationScriptLoaded = nativeValidationScriptLoaded || EndsWith(script, "validation_native.js");

            if (IsWebGPUValidationScript(script) && !nativeValidationScriptLoaded)
            {
                runtime.LoadScript("app:///Scripts/validation_native.js");
                nativeValidationScriptLoaded = true;
            }
        }

        if (!validationScriptLoaded)
        {
            runtime.LoadScript("app:///Scripts/playground_runner.js");
        }
    }
}

@implementation PlaygroundBootstrap

+ (void)loadPlaygroundScripts:(BNRuntime*)runtime
{
    if (runtime == nil)
    {
        return;
    }

    auto options = ParsePlaygroundOptionsFromProcess();
    if (options.ParseError)
    {
        NSLog(@"Playground: %s", options.ErrorMessage.c_str());
        CommandLine::PrintUsage([[[NSProcessInfo processInfo] processName] UTF8String]);
        std::quick_exit(2);
    }

    if (options.ShowHelp)
    {
        CommandLine::PrintUsage([[[NSProcessInfo processInfo] processName] UTF8String]);
        std::quick_exit(0);
    }

    auto& nativeRuntime = *[runtime nativeRuntime];
    Playground::Initialize(options);
    Playground::LoadBootstrapScripts(nativeRuntime);
    QueuePlaygroundOptions(nativeRuntime, options);
    QueueSelectedScripts(nativeRuntime, options);
}

+ (void)loadScripts:(BNRuntime*)runtime
{
    if (runtime == nil)
    {
        return;
    }
    Playground::Initialize();
    Playground::LoadBootstrapScripts(*[runtime nativeRuntime]);
}

@end
