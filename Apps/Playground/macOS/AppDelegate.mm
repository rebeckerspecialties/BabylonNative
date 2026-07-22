#import "AppDelegate.h"
#import "ViewController.h"

@interface AppDelegate ()
@property(strong) NSWindow* window;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)__unused aNotification {
    constexpr CGFloat kInitialWidth = 1280.0;
    constexpr CGFloat kInitialHeight = 720.0;

    // The storyboard creates a window controller before this programmatic landscape window.
    for (NSWindow* storyboardWindow in NSApp.windows)
    {
        storyboardWindow.contentViewController = nil;
        [storyboardWindow close];
    }

    NSRect frame = NSMakeRect(0.0, 0.0, kInitialWidth, kInitialHeight);
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"BabylonNative Playground";
    ViewController* viewController = [[ViewController alloc] initWithNibName:nil bundle:nil];
    viewController.initializesBabylonRuntime = YES;
    self.window.contentViewController = viewController;
    [self.window setContentSize:NSMakeSize(kInitialWidth, kInitialHeight)];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)applicationWillTerminate:(NSNotification *)__unused aNotification {
    // Insert code here to tear down your application
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

@end
