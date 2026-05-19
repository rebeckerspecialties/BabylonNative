#include "TestUtils.h"

#include <cstdlib>

#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

namespace
{
    CGSize NativeValidationDrawableSize(CAMetalLayer* layer)
    {
        if ([layer.delegate isKindOfClass:[MTKView class]])
        {
            MTKView* view = (MTKView*)layer.delegate;
            UIScreen* screen = view.window.windowScene.screen;
            if (screen != nil)
            {
                const CGRect nativeBounds = screen.nativeBounds;
                const CGFloat nativeWidth = CGRectGetWidth(nativeBounds);
                const CGFloat nativeHeight = CGRectGetHeight(nativeBounds);
                if (nativeWidth > 0 && nativeHeight > 0)
                {
                    return CGSizeMake(nativeWidth, nativeHeight);
                }
            }

            CGFloat scale = screen != nil && screen.nativeScale > 0 ? screen.nativeScale : layer.contentsScale;
            if (scale <= 0)
            {
                scale = view.contentScaleFactor > 0 ? view.contentScaleFactor : 1;
            }

            const CGSize viewSize = view.bounds.size;
            if (viewSize.width > 0 && viewSize.height > 0)
            {
                return CGSizeMake(viewSize.width * scale, viewSize.height * scale);
            }
        }

        const CGFloat scale = layer.contentsScale > 0 ? layer.contentsScale : 1;
        const CGFloat width = layer.bounds.size.width * scale;
        const CGFloat height = layer.bounds.size.height * scale;
        return CGSizeMake(width > 1 ? width : 1, height > 1 ? height : 1);
    }

    CGSize RequestedDrawableSizeOrCurrentDisplay(CAMetalLayer* layer, int32_t requestedWidth, int32_t requestedHeight)
    {
        const CGSize currentDisplaySize = NativeValidationDrawableSize(layer);
        if (requestedWidth <= 0 || requestedHeight <= 0)
        {
            return currentDisplaySize;
        }

        const CGSize requestedSize = CGSizeMake(requestedWidth, requestedHeight);
        if (currentDisplaySize.width > 0 &&
            currentDisplaySize.height > 0 &&
            (requestedSize.width > currentDisplaySize.width || requestedSize.height > currentDisplaySize.height))
        {
            NSLog(@"[Playground] tvOS drawable request %.0fx%.0f exceeds current display %.0fx%.0f; using current display size.",
                requestedSize.width,
                requestedSize.height,
                currentDisplaySize.width,
                currentDisplaySize.height);
            return currentDisplaySize;
        }

        return requestedSize;
    }
}

namespace Babylon::Plugins::Internal
{
    void TestUtils::Exit(const Napi::CallbackInfo& info)
    {
        auto exitCode = info[0].As<Napi::Number>().Int32Value();
        InvokeExitCallback(exitCode);
        std::quick_exit(exitCode);
    }

    void TestUtils::UpdateSize(const Napi::CallbackInfo& info)
    {
        const int32_t requestedWidth = info[0].As<Napi::Number>().Int32Value();
        const int32_t requestedHeight = info[1].As<Napi::Number>().Int32Value();

        void (^applySize)(void) = ^{
            CAMetalLayer* layer = (__bridge CAMetalLayer*)m_window;
            const CGSize drawableSize = RequestedDrawableSizeOrCurrentDisplay(layer, requestedWidth, requestedHeight);
            layer.drawableSize = drawableSize;

            if ([layer.delegate isKindOfClass:[MTKView class]])
            {
                MTKView* view = (MTKView*)layer.delegate;
                view.autoResizeDrawable = false;
                view.drawableSize = drawableSize;
            }
        };

        if ([NSThread isMainThread])
        {
            applySize();
        }
        else
        {
            dispatch_sync(dispatch_get_main_queue(), applySize);
        }
    }

    void TestUtils::SetTitle(const Napi::CallbackInfo& info)
    {
        NSString* title = @(info[0].As<Napi::String>().Utf8Value().c_str());
        NSLog(@"[Playground] %@", title);
    }

    Napi::Value TestUtils::GetOutputDirectory(const Napi::CallbackInfo& info)
    {
        NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString* cachePath = paths.count > 0 ? paths[0] : NSTemporaryDirectory();
        NSString* outputPath = [cachePath stringByAppendingPathComponent:@"BabylonNativePlayground"];
        return Napi::Value::From(info.Env(), [outputPath UTF8String]);
    }
}
