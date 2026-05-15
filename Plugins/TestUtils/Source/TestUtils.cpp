#include "TestUtils.h"
#include <Babylon/Plugins/TestUtils.h>

#ifdef BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bimg/decode.h>
#include <bimg/encode.h>
#include <bx/file.h>
#endif

#if defined(BABYLON_NATIVE_TESTUTILS_SYSTEM_IMAGES) && defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#include <Babylon/JsRuntime.h>
#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/Platform.h>

#include <cstring>
#include <filesystem>
#include <functional>
#include <gsl/span>
#include <memory>
#include <sstream>

#define STRINGIZEX(x) #x
#define STRINGIZE(x) STRINGIZEX(x)

namespace
{
#if defined(BABYLON_NATIVE_TESTUTILS_SYSTEM_IMAGES) && defined(__APPLE__)
    class ScopedCFRelease final
    {
    public:
        explicit ScopedCFRelease(CFTypeRef value) noexcept
            : m_value{value}
        {
        }

        ~ScopedCFRelease()
        {
            if (m_value != nullptr)
            {
                CFRelease(m_value);
            }
        }

        ScopedCFRelease(const ScopedCFRelease&) = delete;
        ScopedCFRelease& operator=(const ScopedCFRelease&) = delete;

        template<typename T>
        T Get() const noexcept
        {
            return reinterpret_cast<T>(const_cast<void*>(m_value));
        }

    private:
        CFTypeRef m_value{};
    };

    std::vector<uint8_t> DecodeImageData(Napi::Env env, const void* data, size_t size, uint32_t& width, uint32_t& height)
    {
        ScopedCFRelease cfData{CFDataCreate(kCFAllocatorDefault, static_cast<const UInt8*>(data), static_cast<CFIndex>(size))};
        if (cfData.Get<CFDataRef>() == nullptr)
        {
            throw Napi::Error::New(env, "Failed to create CFData for reference image.");
        }

        ScopedCFRelease source{CGImageSourceCreateWithData(cfData.Get<CFDataRef>(), nullptr)};
        if (source.Get<CGImageSourceRef>() == nullptr)
        {
            throw Napi::Error::New(env, "Failed to create CGImageSource for reference image.");
        }

        ScopedCFRelease image{CGImageSourceCreateImageAtIndex(source.Get<CGImageSourceRef>(), 0, nullptr)};
        if (image.Get<CGImageRef>() == nullptr)
        {
            throw Napi::Error::New(env, "Failed to decode reference image.");
        }

        width = static_cast<uint32_t>(CGImageGetWidth(image.Get<CGImageRef>()));
        height = static_cast<uint32_t>(CGImageGetHeight(image.Get<CGImageRef>()));
        if (width == 0 || height == 0)
        {
            throw Napi::Error::New(env, "Decoded reference image had zero dimensions.");
        }

        std::vector<uint8_t> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        const auto bitmapInfo = static_cast<CGBitmapInfo>(
            static_cast<uint32_t>(kCGBitmapByteOrder32Big) |
            static_cast<uint32_t>(kCGImageAlphaPremultipliedLast));
        ScopedCFRelease colorSpace{CGColorSpaceCreateDeviceRGB()};
        auto context = CGBitmapContextCreate(
            rgba.data(),
            width,
            height,
            8,
            static_cast<size_t>(width) * 4u,
            colorSpace.Get<CGColorSpaceRef>(),
            bitmapInfo);
        ScopedCFRelease bitmapContext{context};
        if (context == nullptr)
        {
            throw Napi::Error::New(env, "Failed to create CGBitmapContext for reference image.");
        }

        CGContextDrawImage(context, CGRectMake(0, 0, width, height), image.Get<CGImageRef>());
        return rgba;
    }

    void WritePngData(Napi::Env env, const uint8_t* data, uint32_t width, uint32_t height, const std::string& filename)
    {
        std::filesystem::create_directories(std::filesystem::path{filename}.parent_path());

        ScopedCFRelease url{CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(filename.c_str()),
            static_cast<CFIndex>(filename.size()),
            false)};
        if (url.Get<CFURLRef>() == nullptr)
        {
            throw Napi::Error::New(env, "Failed to create output URL for PNG.");
        }

        ScopedCFRelease colorSpace{CGColorSpaceCreateDeviceRGB()};
        const auto bitmapInfo = static_cast<CGBitmapInfo>(
            static_cast<uint32_t>(kCGBitmapByteOrder32Big) |
            static_cast<uint32_t>(kCGImageAlphaPremultipliedLast));
        ScopedCFRelease provider{CGDataProviderCreateWithData(nullptr, data, static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, nullptr)};
        if (provider.Get<CGDataProviderRef>() == nullptr)
        {
            throw Napi::Error::New(env, "Failed to create PNG data provider.");
        }

        ScopedCFRelease image{CGImageCreate(
            width,
            height,
            8,
            32,
            static_cast<size_t>(width) * 4u,
            colorSpace.Get<CGColorSpaceRef>(),
            bitmapInfo,
            provider.Get<CGDataProviderRef>(),
            nullptr,
            false,
            kCGRenderingIntentDefault)};
        if (image.Get<CGImageRef>() == nullptr)
        {
            throw Napi::Error::New(env, "Failed to create PNG image.");
        }

        ScopedCFRelease destination{CGImageDestinationCreateWithURL(url.Get<CFURLRef>(), CFSTR("public.png"), 1, nullptr)};
        if (destination.Get<CGImageDestinationRef>() == nullptr)
        {
            throw Napi::Error::New(env, "Failed to create PNG destination.");
        }

        CGImageDestinationAddImage(destination.Get<CGImageDestinationRef>(), image.Get<CGImageRef>(), nullptr);
        if (!CGImageDestinationFinalize(destination.Get<CGImageDestinationRef>()))
        {
            throw Napi::Error::New(env, "Failed to write PNG.");
        }
    }
#endif
}

namespace Babylon::Plugins::Internal
{
    Napi::Value TestUtils::GetGraphicsApiName(const Napi::CallbackInfo& info)
    {
        return Napi::Value::From(info.Env(), STRINGIZE(GRAPHICS_API));
    }

    void TestUtils::WritePNG(const Napi::CallbackInfo& info)
    {
        const auto buffer = info[0].As<Napi::Uint8Array>();
        const auto width = info[1].As<Napi::Number>().Uint32Value();
        const auto height = info[2].As<Napi::Number>().Uint32Value();
        const auto filename = info[3].As<Napi::String>().Utf8Value();

        if (buffer.ByteLength() < width * height * 4)
        {
            throw Napi::Error::New(info.Env(), "Buffer byte length is invalid for width and height");
        }

#if defined(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES)
        bx::MemoryBlock mb(&Graphics::DeviceContext::GetDefaultAllocator());
        bx::FileWriter writer;
        bx::FilePath filepath(filename.c_str());
        bx::FilePath filedir(filepath.getPath());
        bx::makeAll(filedir);
        bx::Error err;
        if (writer.open(filepath, false, &err))
        {
            bimg::imageWritePng(&writer, width, height, width * 4, buffer.Data(), bimg::TextureFormat::RGBA8, false);
            writer.close();
        }
#elif defined(BABYLON_NATIVE_TESTUTILS_SYSTEM_IMAGES) && defined(__APPLE__)
        WritePngData(info.Env(), buffer.Data(), width, height, filename);
#else
        throw Napi::Error::New(info.Env(), "PNG writing is disabled in this build.");
#endif
    }

    Napi::Value TestUtils::DecodeImage(const Napi::CallbackInfo& info)
    {
        Image* image = new Image;
        const auto buffer = info[0].As<Napi::ArrayBuffer>();

#if defined(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES)
        image->m_Image = bimg::imageParse(&Graphics::DeviceContext::GetDefaultAllocator(), buffer.Data(), static_cast<uint32_t>(buffer.ByteLength()));
#elif defined(BABYLON_NATIVE_TESTUTILS_SYSTEM_IMAGES) && defined(__APPLE__)
        image->m_Image = DecodeImageData(info.Env(), buffer.Data(), buffer.ByteLength(), image->m_Width, image->m_Height);
#else
        delete image;
        throw Napi::Error::New(info.Env(), "Image decoding is disabled in this build.");
#endif

        auto finalizer = [](Napi::Env, Image* image) { delete image; };
        return Napi::External<Image>::New(info.Env(), image, std::move(finalizer));
    }

    Napi::Value TestUtils::GetImageData(const Napi::CallbackInfo& info)
    {
        const auto imageData = info[0].As<Napi::External<Image>>().Data();

#if defined(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES)
        if (!imageData || !imageData->m_Image || !imageData->m_Image->m_size)
        {
            return info.Env().Undefined();
        }

        auto data = Napi::Uint8Array::New(info.Env(), imageData->m_Image->m_size);
        const auto ptr = static_cast<uint8_t*>(imageData->m_Image->m_data);
        memcpy(data.Data(), ptr, imageData->m_Image->m_size);

        return Napi::Value::From(info.Env(), data);
#elif defined(BABYLON_NATIVE_TESTUTILS_SYSTEM_IMAGES) && defined(__APPLE__)
        if (!imageData || imageData->m_Image.empty())
        {
            return info.Env().Undefined();
        }

        auto data = Napi::Uint8Array::New(info.Env(), imageData->m_Image.size());
        memcpy(data.Data(), imageData->m_Image.data(), imageData->m_Image.size());

        return Napi::Value::From(info.Env(), data);
#else
        throw Napi::Error::New(info.Env(), "Image data extraction is disabled in this build.");
#endif
    }

    void TestUtils::GetFrameBufferData(const Napi::CallbackInfo& info)
    {
        const auto callback{ info[0].As<Napi::Function>() };

        auto callbackPtr{ std::make_shared<Napi::FunctionReference>(Napi::Persistent(callback)) };
        m_deviceContext.RequestScreenShot([this, callbackPtr{ std::move(callbackPtr) }](std::vector<uint8_t> array) {
            m_runtime.Dispatch([callbackPtr{ std::move(callbackPtr) }, array{ std::move(array) }](Napi::Env env) mutable {
                auto span = gsl::span<uint8_t>{array};
                auto arrayBuffer{ Napi::ArrayBuffer::New(env, span.data(), span.size(), [array = std::move(array)](Napi::Env, void*) {}) };
                auto typedArray{ Napi::Uint8Array::New(env, span.size(), arrayBuffer, 0) };
                callbackPtr->Value().Call({ typedArray });
            });
        });
    }

    void TestUtils::CaptureNextFrame(const Napi::CallbackInfo& info)
    {
#if defined(BABYLON_NATIVE_TESTUTILS_WGPU)
        (void)info;
#else
        m_deviceContext.RequestCaptureNextFrame();
#endif
    }
}

namespace
{
    Babylon::Plugins::TestUtils::ExitCallback& ExitCallbackStorage()
    {
        static Babylon::Plugins::TestUtils::ExitCallback s_callback;
        return s_callback;
    }
}

namespace Babylon::Plugins::TestUtils
{
    void BABYLON_API Initialize(Napi::Env env, Graphics::WindowT window)
    {
        Internal::TestUtils::CreateInstance(env, window);
    }

    void BABYLON_API SetExitCallback(ExitCallback callback)
    {
        ExitCallbackStorage() = std::move(callback);
    }
}

namespace Babylon::Plugins::Internal
{
    // Bridges per-platform TestUtils::Exit() to the host-registered callback.
    void InvokeExitCallback(int exitCode)
    {
        auto& cb = ExitCallbackStorage();
        if (cb)
        {
            cb(exitCode);
        }
    }
}
