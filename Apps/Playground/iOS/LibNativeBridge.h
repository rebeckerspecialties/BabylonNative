#pragma once

#include <Foundation/Foundation.h>
#include <MetalKit/MetalKit.h>


@interface LibNativeBridge : NSObject

- (instancetype)init;
- (void)dealloc;

- (void)initializeWithView:(MTKView*)inView screenScale:(float)inScreenScale width:(int)inWidth height:(int)inHeight comparisonWidth:(int)comparisonWidth comparisonHeight:(int)comparisonHeight xrView:(void*)xrView NS_SWIFT_NAME(initialize(_:screenScale:width:height:comparisonWidth:comparisonHeight:xrView:));
- (void)resize:(int)inWidth height:(int)inHeight;
- (void)render;
- (void)setTouchDown:(int)pointerId x:(int)inX y:(int)inY;
- (void)setTouchMove:(int)pointerId x:(int)inX y:(int)inY;
- (void)setTouchUp:(int)pointerId x:(int)inX y:(int)inY;
- (bool)isXRActive;

@end
