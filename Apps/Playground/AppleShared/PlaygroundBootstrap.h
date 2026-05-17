// PlaygroundBootstrap.h — Obj-C helper exposed to Swift via the
// bridging header on iOS/visionOS, and used directly from Obj-C++ on macOS.
// It forwards into Apps/Playground/Shared so every Apple host shares the
// same Babylon.js bootstrap and validation-script selection.

#pragma once

#import <Foundation/Foundation.h>

@class BNRuntime;

NS_ASSUME_NONNULL_BEGIN

@interface PlaygroundBootstrap : NSObject

/// Performs process-wide Playground setup (PerfTrace level, ...), queues the
/// Babylon.js bootstrap scripts, applies _playgroundOptions, and queues the
/// smoke/validation/user scripts implied by the current process arguments.
+ (void)loadPlaygroundScripts:(BNRuntime*)runtime;

/// Queues only the Babylon.js bootstrap scripts. Kept for hosts that need to
/// own script selection themselves.
+ (void)loadScripts:(BNRuntime*)runtime;

@end

NS_ASSUME_NONNULL_END
