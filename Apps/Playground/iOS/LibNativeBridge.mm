#include "LibNativeBridge.h"

#import <Shared/AppContext.h>
#import <Shared/CommandLine.h>
#import <Babylon/Plugins/NativeInput.h>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

std::optional<AppContext> appContext{};
float screenScale{1.0f};

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
}

@implementation LibNativeBridge

- (instancetype)init
{
    self = [super init];
    return self;
}

- (void)dealloc
{
    appContext.reset();
}

- (void)init:(MTKView*)view screenScale:(float)inScreenScale width:(int)inWidth height:(int)inHeight xrView:(void*)xrView
{
    screenScale = inScreenScale;

    PlaygroundOptions playgroundOptions = ParsePlaygroundOptionsFromProcess();
    if (playgroundOptions.ParseError)
    {
        NSLog(@"Playground: %s", playgroundOptions.ErrorMessage.c_str());
        CommandLine::PrintUsage([[[NSProcessInfo processInfo] processName] UTF8String]);
        std::quick_exit(2);
    }

    if (playgroundOptions.ShowHelp)
    {
        CommandLine::PrintUsage([[[NSProcessInfo processInfo] processName] UTF8String]);
        std::quick_exit(0);
    }

    appContext.emplace(
        (__bridge CA::MetalLayer*)view.layer,
        static_cast<size_t>(inWidth),
        static_cast<size_t>(inHeight),
        [](const char* message) {
            NSLog(@"%s", message);
        },
        [](Napi::Env env) {
            Napi::HandleScope scope{env};

            auto statusCallback = Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (info.Length() > 0)
                {
                    std::string message{};
                    if (info[0].IsString())
                    {
                        message = info[0].As<Napi::String>().Utf8Value();
                    }
                    else
                    {
                        message = info[0].ToString().Utf8Value();
                    }
                    NSLog(@"[Playground] %s", message.c_str());
                }
            });
            env.Global().Set("__nativePlaygroundStatus", statusCallback);
        },
        playgroundOptions);

    if (playgroundOptions.Scripts.empty() && !HasValidationIntent(playgroundOptions))
    {
        appContext->ScriptLoader().Eval(
            "(function(){"
            "globalThis.createScene=undefined;"
            "globalThis.__babylonPlaygroundSceneFactoryReady=undefined;"
            "globalThis.__babylonPlaygroundWebGpuSmokeReady=undefined;"
            "globalThis.__webgpuSmokeDispose=undefined;"
            "})();",
            "app:///Scripts/playground_bootstrap_reset.js");
        appContext->ScriptLoader().LoadScript("app:///Scripts/webgpu_smoke.js");
        appContext->ScriptLoader().LoadScript("app:///Scripts/playground_runner.js");
    }
    else if (playgroundOptions.Scripts.empty())
    {
        appContext->ScriptLoader().LoadScript("app:///Scripts/validation_webgpu_native.js");
        appContext->ScriptLoader().LoadScript("app:///Scripts/validation_native.js");
    }
    else
    {
        bool validationScriptLoaded = false;
        bool nativeValidationScriptLoaded = false;
        for (const auto& script : playgroundOptions.Scripts)
        {
            appContext->ScriptLoader().LoadScript(script);
            validationScriptLoaded = validationScriptLoaded || IsValidationScript(script);
            nativeValidationScriptLoaded = nativeValidationScriptLoaded || EndsWith(script, "validation_native.js");

            if (IsWebGPUValidationScript(script) && !nativeValidationScriptLoaded)
            {
                appContext->ScriptLoader().LoadScript("app:///Scripts/validation_native.js");
                nativeValidationScriptLoaded = true;
            }
        }

        if (!validationScriptLoaded)
        {
            appContext->ScriptLoader().LoadScript("app:///Scripts/playground_runner.js");
        }
    }
}

- (void)resize:(int)inWidth height:(int)inHeight
{
    @autoreleasepool {
        if (appContext)
        {
            appContext->DeviceUpdate().Finish();
            appContext->Device().FinishRenderingCurrentFrame();

            appContext->Device().UpdateSize(static_cast<size_t>(inWidth), static_cast<size_t>(inHeight));

            appContext->Device().StartRenderingCurrentFrame();
            appContext->DeviceUpdate().Start();
        }
    }
}

- (void)render
{
    @autoreleasepool {
        if (appContext)
        {
            appContext->DeviceUpdate().Finish();
            appContext->Device().FinishRenderingCurrentFrame();
            appContext->Device().StartRenderingCurrentFrame();
            appContext->DeviceUpdate().Start();
        }
    }
}

- (void)setTouchDown:(int)pointerId x:(int)inX y:(int)inY
{
    if (appContext && appContext->Input()) {
        appContext->Input()->TouchDown(pointerId, inX * screenScale, inY * screenScale);
    }
}

- (void)setTouchMove:(int)pointerId x:(int)inX y:(int)inY
{
    if (appContext && appContext->Input()) {
        appContext->Input()->TouchMove(pointerId, inX * screenScale, inY * screenScale);
    }
}

- (void)setTouchUp:(int)pointerId x:(int)inX y:(int)inY
{
    if (appContext && appContext->Input()) {
        appContext->Input()->TouchUp(pointerId, inX * screenScale, inY * screenScale);
    }
}

- (bool)isXRActive
{
    return false;
}

@end
