#include "LibNativeBridge.h"

#import <Shared/AppContext.h>
#import <Shared/CommandLine.h>
#import <Babylon/Plugins/NativeInput.h>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#import <TargetConditionals.h>

std::optional<AppContext> appContext{};
float screenScale{1.0f};
bool profileNativeFrames{false};

namespace
{
    using Clock = std::chrono::steady_clock;

    double ElapsedMs(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

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
            options.Hdr10 ||
            options.SaveResults.has_value() ||
            options.InspectionHoldMs.has_value() ||
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

- (void)initializeWithView:(MTKView*)view screenScale:(float)inScreenScale width:(int)inWidth height:(int)inHeight comparisonWidth:(int)comparisonWidth comparisonHeight:(int)comparisonHeight xrView:(void*)xrView
{
    screenScale = inScreenScale;

    PlaygroundOptions playgroundOptions = ParsePlaygroundOptionsFromProcess();
#if TARGET_OS_TV
    if (playgroundOptions.Hdr10 && !playgroundOptions.InspectionHoldMs.has_value())
    {
        playgroundOptions.InspectionHoldMs = 8000;
    }
#endif
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

    const bool hdr10 = playgroundOptions.Hdr10;
    const auto inspectionHoldMs = playgroundOptions.InspectionHoldMs;
    profileNativeFrames = playgroundOptions.ProfileFrames;

    appContext.emplace(
        (__bridge CA::MetalLayer*)view.layer,
        static_cast<size_t>(inWidth),
        static_cast<size_t>(inHeight),
        [](const char* message) {
            NSLog(@"%s", message);
        },
        [inWidth, inHeight, comparisonWidth, comparisonHeight, hdr10, inspectionHoldMs](Napi::Env env) {
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

#if TARGET_OS_TV
            env.Global().Set("__nativeValidationHdr10", Napi::Boolean::New(env, hdr10));
            if (inspectionHoldMs.has_value())
            {
                env.Global().Set("__nativeValidationInspectionHoldMs", Napi::Number::New(env, *inspectionHoldMs));
            }
            env.Global().Set("__nativeValidationRenderWidth", Napi::Number::New(env, inWidth));
            env.Global().Set("__nativeValidationRenderHeight", Napi::Number::New(env, inHeight));
            env.Global().Set("__nativeValidationComparisonWidth", Napi::Number::New(env, comparisonWidth));
            env.Global().Set("__nativeValidationComparisonHeight", Napi::Number::New(env, comparisonHeight));
            NSLog(@"[Playground] tvOS validation render size: %dx%d; comparison size: %dx%d",
                inWidth,
                inHeight,
                comparisonWidth,
                comparisonHeight);
#else
            (void)inWidth;
            (void)inHeight;
            (void)comparisonWidth;
            (void)comparisonHeight;
            (void)hdr10;
            (void)inspectionHoldMs;
#endif
        },
        playgroundOptions);

    appContext->UpdateXrWindow(xrView);

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
            const auto frameStart = Clock::now();
            appContext->DeviceUpdate().Finish();
            const auto updateFinished = Clock::now();
            appContext->Device().FinishRenderingCurrentFrame();
            const auto frameFinished = Clock::now();
            appContext->Device().StartRenderingCurrentFrame();
            const auto frameStarted = Clock::now();
            appContext->DeviceUpdate().Start();
            const auto updateStarted = Clock::now();
            appContext->DispatchAnimationFrame();

            if (profileNativeFrames)
            {
                static uint64_t frameCount{0};
                static auto windowStart = Clock::now();
                static double finishUpdateMs{0};
                static double finishFrameMs{0};
                static double startFrameMs{0};
                static double startUpdateMs{0};
                static double totalMs{0};

                frameCount++;
                finishUpdateMs += ElapsedMs(frameStart, updateFinished);
                finishFrameMs += ElapsedMs(updateFinished, frameFinished);
                startFrameMs += ElapsedMs(frameFinished, frameStarted);
                startUpdateMs += ElapsedMs(frameStarted, updateStarted);
                totalMs += ElapsedMs(frameStart, updateStarted);

                if ((frameCount % 30u) == 0u)
                {
                    const auto now = Clock::now();
                    const auto elapsedMs = std::max(ElapsedMs(windowStart, now), 0.0001);
                    const auto frames = 30.0;
                    NSLog(@"[Playground] Native frame profile frame=%llu windowFps=%.2f finishUpdateMs=%.3f finishFrameMs=%.3f startFrameMs=%.3f startUpdateMs=%.3f totalBoundaryMs=%.3f",
                        static_cast<unsigned long long>(frameCount),
                        (frames * 1000.0) / elapsedMs,
                        finishUpdateMs / frames,
                        finishFrameMs / frames,
                        startFrameMs / frames,
                        startUpdateMs / frames,
                        totalMs / frames);
                    windowStart = now;
                    finishUpdateMs = 0;
                    finishFrameMs = 0;
                    startFrameMs = 0;
                    startUpdateMs = 0;
                    totalMs = 0;
                }
            }
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
    return appContext && appContext->IsXrActive();
}

@end
