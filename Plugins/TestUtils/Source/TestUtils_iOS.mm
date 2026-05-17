#include "TestUtils.h"

#include <cstdlib>

#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

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
        const int32_t width = info[0].As<Napi::Number>().Int32Value();
        const int32_t height = info[1].As<Napi::Number>().Int32Value();

        dispatch_async(dispatch_get_main_queue(), ^{
            CAMetalLayer* layer = (__bridge CAMetalLayer*)m_window;
            CGSize drawableSize = CGSizeMake(width, height);
            layer.drawableSize = drawableSize;

            if ([layer.delegate isKindOfClass:[MTKView class]])
            {
                MTKView* view = (MTKView*)layer.delegate;
                view.autoResizeDrawable = false;
                view.drawableSize = drawableSize;
            }
        });
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
