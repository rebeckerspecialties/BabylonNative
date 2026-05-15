#import "ViewController.h"

#import <Babylon/Embedding/Apple/BabylonNativeEmbedding.h>
#import "AppleShared/PlaygroundBootstrap.h"

#import <MetalKit/MTKView.h>

#include <Shared/CommandLine.h>

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

    void AppendJsString(std::ostringstream& out, std::string_view value)
    {
        out << '"';
        for (char ch : value)
        {
            switch (ch)
            {
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default: out << ch; break;
            }
        }
        out << '"';
    }

    NSString* MakePlaygroundOptionsScript(const PlaygroundOptions& options)
    {
        std::ostringstream js{};
        js << "globalThis._playgroundOptions={";
        js << "listTests:" << (options.ListTests ? "true" : "false") << ',';
        js << "headless:" << (options.Headless ? "true" : "false") << ',';
        js << "breakOnFail:" << (options.BreakOnFail ? "true" : "false") << ',';
        js << "generateReferences:" << (options.GenerateReferences ? "true" : "false") << ',';
        js << "runOnce:" << (options.RunOnce ? "true" : "false") << ',';
        js << "includeExcluded:" << (options.IncludeExcluded ? "true" : "false");
        if (options.SaveResults.has_value())
        {
            js << ",saveResults:" << (*options.SaveResults ? "true" : "false");
        }
        if (options.CaptureFrame.has_value())
        {
            js << ",captureFrame:" << *options.CaptureFrame;
        }
        js << ",testFilters:[";
        for (size_t index = 0; index < options.TestFilters.size(); ++index)
        {
            if (index != 0)
            {
                js << ',';
            }
            AppendJsString(js, options.TestFilters[index]);
        }
        js << "],testIndices:[";
        for (size_t index = 0; index < options.TestIndices.size(); ++index)
        {
            if (index != 0)
            {
                js << ',';
            }
            js << options.TestIndices[index];
        }
        js << "]};";
        const auto source = js.str();
        return [NSString stringWithUTF8String:source.c_str()];
    }
}

@implementation ViewController
{
    BNRuntime* _runtime;
    BNView* _bnView;
    MTKView* _mtkView;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    // Required for mouseMoved events to be delivered to the view.
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
    // Tear down View first (closes in-flight frame, unbinds the surface),
    // then Runtime (joins the JS thread).
    _bnView = nil;
    _runtime = nil;
    [_mtkView removeFromSuperview];
    _mtkView = nil;
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

    BNRuntimeOptions* options = [[BNRuntimeOptions alloc] init];
    options.enableDebugger = YES;
    options.enableDebugTrace = playgroundOptions.DebugTrace.value_or(true) ? YES : NO;
    _runtime = [[BNRuntime alloc] initWithOptions:options];

    [PlaygroundBootstrap loadScripts:_runtime];
    [_runtime eval:MakePlaygroundOptionsScript(playgroundOptions) sourceURL:@"app:///Scripts/playground_options.js"];

    if (playgroundOptions.Scripts.empty() && !HasValidationIntent(playgroundOptions))
    {
        [_runtime eval:@"(function(){"
                       @"globalThis.createScene=undefined;"
                       @"globalThis.__babylonPlaygroundSceneFactoryReady=undefined;"
                       @"globalThis.__babylonPlaygroundWebGpuSmokeReady=undefined;"
                       @"globalThis.__webgpuSmokeDispose=undefined;"
                       @"})();"
                sourceURL:@"app:///Scripts/playground_bootstrap_reset.js"];
        [_runtime loadScript:@"app:///Scripts/webgpu_smoke.js"];
        [_runtime loadScript:@"app:///Scripts/playground_runner.js"];
    }
    else if (playgroundOptions.Scripts.empty())
    {
        [_runtime loadScript:@"app:///Scripts/validation_webgpu_native.js"];
        [_runtime loadScript:@"app:///Scripts/validation_native.js"];
    }
    else
    {
        bool validationScriptLoaded = false;
        bool nativeValidationScriptLoaded = false;
        for (const auto& script : playgroundOptions.Scripts)
        {
            [_runtime loadScript:[NSString stringWithUTF8String:script.c_str()]];
            validationScriptLoaded = validationScriptLoaded || IsValidationScript(script);
            nativeValidationScriptLoaded = nativeValidationScriptLoaded || EndsWith(script, "validation_native.js");

            if (IsWebGPUValidationScript(script) && !nativeValidationScriptLoaded)
            {
                [_runtime loadScript:@"app:///Scripts/validation_native.js"];
                nativeValidationScriptLoaded = true;
            }
        }

        if (!validationScriptLoaded)
        {
            [_runtime loadScript:@"app:///Scripts/playground_runner.js"];
        }
    }

    _mtkView = [[MTKView alloc] initWithFrame:[self view].frame device:nil];
    _mtkView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [[self view] addSubview:_mtkView];

    // BNView attaches the runtime to the MTKView and installs a default
    // MTKViewDelegate that drives per-frame render. Resize is driven
    // separately from -viewDidLayout below.
    _bnView = [[BNView alloc] initWithRuntime:_runtime view:_mtkView];
}

- (void)viewDidAppear {
    [super viewDidAppear];

    [self refreshBabylon];
}

- (void)viewDidDisappear {
    [super viewDidDisappear];

    [self uninitialize];
}

- (void)viewDidLayout {
    [super viewDidLayout];

    // Babylon Native owns the drawable size (BNView sets
    // autoResizeDrawable = NO), so MTKView no longer reports size changes
    // via its delegate. Drive resize explicitly from layout, passing
    // logical points; BNView applies the device-pixel-ratio internally.
    if (_bnView != nil)
    {
        const CGSize size = _mtkView.bounds.size;
        if (size.width > 0 && size.height > 0)
        {
            [_bnView resizeWithWidth:static_cast<NSUInteger>(size.width)
                              height:static_cast<NSUInteger>(size.height)];
        }
    }
}

#pragma mark - Input forwarding

// AppKit reports event locations in window coordinates with a bottom-left
// origin; Babylon (CSS) expects top-left. Convert here and pass logical
// pixels through unchanged — BNView handles device-pixel-ratio scaling.
- (NSPoint)logicalPointFromEvent:(NSEvent*)event {
    NSPoint location = [event locationInWindow];
    CGFloat height = [self view].frame.size.height;
    return NSMakePoint(location.x, height - location.y);
}

- (void)mouseMoved:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseMoveAtX:p.x y:p.y];
}

- (void)mouseDown:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseDown:BNViewMouseButtonLeft atX:p.x y:p.y];
}

- (void)mouseDragged:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseMoveAtX:p.x y:p.y];
}

- (void)mouseUp:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseUp:BNViewMouseButtonLeft atX:p.x y:p.y];
}

- (void)otherMouseDown:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseDown:BNViewMouseButtonMiddle atX:p.x y:p.y];
}

- (void)otherMouseDragged:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseMoveAtX:p.x y:p.y];
}

- (void)otherMouseUp:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseUp:BNViewMouseButtonMiddle atX:p.x y:p.y];
}

- (void)rightMouseDown:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseDown:BNViewMouseButtonRight atX:p.x y:p.y];
}

- (void)rightMouseDragged:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseMoveAtX:p.x y:p.y];
}

- (void)rightMouseUp:(NSEvent*)theEvent {
    NSPoint p = [self logicalPointFromEvent:theEvent];
    [_bnView mouseUp:BNViewMouseButtonRight atX:p.x y:p.y];
}

- (void)scrollWheel:(NSEvent*)theEvent {
    // Negate so scroll-up matches Babylon's negative-delta convention.
    [_bnView mouseWheel:BNViewMouseWheelAxisY delta:-theEvent.deltaY];
}

- (IBAction)refresh:(id)__unused sender {
    [self refreshBabylon];
}

@end
