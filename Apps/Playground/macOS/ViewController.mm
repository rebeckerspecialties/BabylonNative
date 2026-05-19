#import "ViewController.h"

#import <Babylon/Embedding/Apple/BabylonNativeEmbedding.h>
#import "AppleShared/PlaygroundBootstrap.h"

#import <CoreGraphics/CoreGraphics.h>
#import <MetalKit/MTKView.h>
#import <QuartzCore/QuartzCore.h>

#include <Shared/CommandLine.h>

#include <algorithm>
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
            options.Hdr10 ||
            options.ProfileFrames ||
            options.SaveResults.has_value() ||
            options.InspectionHoldMs.has_value() ||
            options.PreferredFps.has_value() ||
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

    NSInteger ScreenMaximumFramesPerSecond(MTKView* engineView)
    {
        if (@available(macOS 12.0, *))
        {
            NSScreen* screen = engineView.window.screen;
            if (screen == nil)
            {
                screen = [NSScreen mainScreen];
            }
            return screen != nil ? screen.maximumFramesPerSecond : 0;
        }
        return 0;
    }

    void ConfigureFrameRate(MTKView* engineView, const PlaygroundOptions& options)
    {
        if (!options.PreferredFps.has_value())
        {
            return;
        }

        const NSInteger requestedFramesPerSecond = static_cast<NSInteger>(*options.PreferredFps);
        const NSInteger maximumFramesPerSecond = ScreenMaximumFramesPerSecond(engineView);
        engineView.preferredFramesPerSecond = maximumFramesPerSecond > 0
            ? std::min(requestedFramesPerSecond, maximumFramesPerSecond)
            : requestedFramesPerSecond;
        NSLog(@"[Playground] MTKView preferredFramesPerSecond=%ld requestedFramesPerSecond=%ld screenMaximumFramesPerSecond=%ld",
            static_cast<long>(engineView.preferredFramesPerSecond),
            static_cast<long>(requestedFramesPerSecond),
            static_cast<long>(maximumFramesPerSecond));
    }

    void ConfigureDrawable(MTKView* engineView, const PlaygroundOptions& options)
    {
        if (!options.Hdr10)
        {
            engineView.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
            return;
        }

        engineView.colorPixelFormat = MTLPixelFormatRGBA16Float;

        CAMetalLayer* layer = (CAMetalLayer*)engineView.layer;
        layer.pixelFormat = MTLPixelFormatRGBA16Float;
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearITUR_2020);
        layer.colorspace = colorSpace;
        if (colorSpace != nullptr)
        {
            CGColorSpaceRelease(colorSpace);
        }

        if (@available(macOS 10.15, *))
        {
            layer.EDRMetadata = [CAEDRMetadata HDR10MetadataWithMinLuminance:0.005f maxLuminance:1000.0f opticalOutputScale:100.0f];
        }
        if (@available(macOS 15.0, *))
        {
            layer.toneMapMode = CAToneMapModeIfSupported;
        }
        if (@available(macOS 26.0, *))
        {
            layer.preferredDynamicRange = CADynamicRangeHigh;
            layer.contentsHeadroom = 10.0;
        }
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
        js << "includeExcluded:" << (options.IncludeExcluded ? "true" : "false") << ',';
        js << "hdr10:" << (options.Hdr10 ? "true" : "false") << ',';
        js << "profileFrames:" << (options.ProfileFrames ? "true" : "false");
        if (options.SaveResults.has_value())
        {
            js << ",saveResults:" << (*options.SaveResults ? "true" : "false");
        }
        if (options.InspectionHoldMs.has_value())
        {
            js << ",inspectionHoldMs:" << *options.InspectionHoldMs;
        }
        if (options.PreferredFps.has_value())
        {
            js << ",preferredFps:" << *options.PreferredFps;
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
        if (options.Hdr10)
        {
            js << "globalThis.__nativeValidationHdr10=true;";
            js << "globalThis.__nativeValidationRenderWidth=3840;";
            js << "globalThis.__nativeValidationRenderHeight=2160;";
            js << "globalThis.__nativeValidationComparisonWidth=600;";
            js << "globalThis.__nativeValidationComparisonHeight=400;";
        }
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
    ConfigureFrameRate(_mtkView, playgroundOptions);
    ConfigureDrawable(_mtkView, playgroundOptions);
    [[self view] addSubview:_mtkView];

    if (playgroundOptions.Hdr10)
    {
        NSLog(@"[Playground] macOS HDR validation render size: 3840x2160; comparison size: 600x400");
    }

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
