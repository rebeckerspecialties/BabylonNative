#import "ViewController.h"

#include <Shared/AppContext.h>
#include <Shared/CommandLine.h>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#import <MetalKit/MTKView.h>

std::optional<AppContext> appContext{};

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

@interface EngineView : MTKView <MTKViewDelegate>

@end

@implementation EngineView

- (void)mtkView:(MTKView *)__unused view drawableSizeWillChange:(CGSize) size
{
    @autoreleasepool {
        if (appContext) {
            appContext->DeviceUpdate().Finish();
            appContext->Device().FinishRenderingCurrentFrame();

            appContext->Device().UpdateSize(static_cast<size_t>(size.width), static_cast<size_t>(size.height));

            appContext->Device().StartRenderingCurrentFrame();
            appContext->DeviceUpdate().Start();
        }
    }
}

- (void)drawInMTKView:(MTKView *)__unused view
{
    @autoreleasepool {
        if (appContext) {
            appContext->DeviceUpdate().Finish();
            appContext->Device().FinishRenderingCurrentFrame();
            appContext->Device().StartRenderingCurrentFrame();
            appContext->DeviceUpdate().Start();
        }
    }
}

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    NSTrackingArea* trackingArea = [
        [NSTrackingArea alloc]
        initWithRect:NSZeroRect
        options:NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingMouseMoved
        owner:self
        userInfo:nil
        ];
    [[self view] addTrackingArea:trackingArea];
}

- (void)uninitialize {
    appContext.reset();
}

- (void)refreshBabylon {
    [self uninitialize];

    PlaygroundOptions playgroundOptions = ParsePlaygroundOptionsFromProcess();
    if (playgroundOptions.ParseError)
    {
        fprintf(stderr, "Playground: %s\n", playgroundOptions.ErrorMessage.c_str());
        CommandLine::PrintUsage([[[NSProcessInfo processInfo] processName] UTF8String]);
        [NSApp terminate:nil];
        return;
    }

    if (playgroundOptions.ShowHelp)
    {
        CommandLine::PrintUsage([[[NSProcessInfo processInfo] processName] UTF8String]);
        [NSApp terminate:nil];
        return;
    }

    EngineView* engineView = [[EngineView alloc] initWithFrame:[self view].frame device:nil];
    engineView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [[self view] addSubview:engineView];
    engineView.delegate = engineView;

    size_t width = static_cast<size_t>(engineView.drawableSize.width);
    size_t height = static_cast<size_t>(engineView.drawableSize.height);

    appContext.emplace(
        (__bridge CA::MetalLayer*)engineView.layer,
        width,
        height,
        [](const char* message)
        {
            NSLog(@"%s", message);
        },
        [](Napi::Env env)
        {
            auto statusCallback = Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (info.Length() > 0)
                {
                    std::string message = info[0].IsString()
                        ? info[0].As<Napi::String>().Utf8Value()
                        : info[0].ToString().Utf8Value();
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
        for (const auto& script : playgroundOptions.Scripts) {
            appContext->ScriptLoader().LoadScript(script);
            validationScriptLoaded = validationScriptLoaded || IsValidationScript(script);
            nativeValidationScriptLoaded = nativeValidationScriptLoaded || EndsWith(script, "validation_native.js");

            if (IsWebGPUValidationScript(script) && !nativeValidationScriptLoaded) {
                appContext->ScriptLoader().LoadScript("app:///Scripts/validation_native.js");
                nativeValidationScriptLoaded = true;
            }
        }

        if (!validationScriptLoaded) {
            appContext->ScriptLoader().LoadScript("app:///Scripts/playground_runner.js");
        }
    }
}

- (void)viewDidAppear {
    [super viewDidAppear];

    [self refreshBabylon];
}

- (void)viewDidDisappear {
    [super viewDidDisappear];

    [self uninitialize];
}

- (void)mouseMoved:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)mouseDown:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)mouseDragged:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)mouseUp:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)otherMouseDown:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)otherMouseDragged:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)otherMouseUp:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)rightMouseDown:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)rightMouseDragged:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)rightMouseUp:(NSEvent *)theEvent {
    (void)theEvent;
}

- (void)scrollWheel:(NSEvent *)theEvent {
    (void)theEvent;
}

- (IBAction)refresh:(id)__unused sender
{
    [self refreshBabylon];
}

@end
