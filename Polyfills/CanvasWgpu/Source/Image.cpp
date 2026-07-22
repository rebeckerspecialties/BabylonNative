#include <map>
#include "Canvas.h"
#include "Image.h"
#include "Context.h"
#include <functional>
#include <sstream>
#include <assert.h>
#include "nanovg/nanovg.h"
#include <cassert>
#include <cstring>
#include <napi/pointer.h>
#include <basen.hpp>
#include <cstdint>
#include <limits>

extern "C"
{
    int32_t babylon_canvas_decode_image_rgba(
        const uint8_t* data,
        size_t len,
        uint32_t* out_width,
        uint32_t* out_height,
        uint8_t** out_rgba,
        size_t* out_len);
    void babylon_canvas_free_bytes(uint8_t* data, size_t len);
}

namespace
{
    std::string NormalizeImageUrl(std::string url)
    {
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        {
            return url;
        }

        std::string normalized;
        normalized.reserve(url.size());
        for (char ch : url)
        {
            if (ch == ' ')
            {
                normalized += "%20";
            }
            else
            {
                normalized += ch;
            }
        }
        return normalized;
    }

    class ScopedNativeCanvasImageRef final
    {
    public:
        struct AdoptTag
        {
        };

        explicit ScopedNativeCanvasImageRef(Babylon::Polyfills::Internal::NativeCanvasImage& image)
            : m_image{&image}
        {
            m_image->Ref();
        }

        ScopedNativeCanvasImageRef(Babylon::Polyfills::Internal::NativeCanvasImage& image, AdoptTag)
            : m_image{&image}
        {
        }

        ScopedNativeCanvasImageRef(const ScopedNativeCanvasImageRef&) = delete;
        ScopedNativeCanvasImageRef& operator=(const ScopedNativeCanvasImageRef&) = delete;

        ~ScopedNativeCanvasImageRef()
        {
            if (m_image != nullptr)
            {
                try
                {
                    m_image->Unref();
                }
                catch (...)
                {
                }
            }
        }

        void Release()
        {
            m_image = nullptr;
        }

    private:
        Babylon::Polyfills::Internal::NativeCanvasImage* m_image{};
    };
}

namespace Babylon::Polyfills::Internal
{
    static constexpr auto JS_IMAGE_CONSTRUCTOR_NAME = "Image";

    void NativeCanvasImage::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};

        Napi::Function func = DefineClass(
            env,
            JS_IMAGE_CONSTRUCTOR_NAME,
            {
                InstanceAccessor("width", &NativeCanvasImage::GetWidth, nullptr),
                InstanceAccessor("height", &NativeCanvasImage::GetHeight, nullptr),
                InstanceAccessor("naturalWidth", &NativeCanvasImage::GetNaturalWidth, nullptr),
                InstanceAccessor("naturalHeight", &NativeCanvasImage::GetNaturalHeight, nullptr),
                InstanceAccessor("src", &NativeCanvasImage::GetSrc, &NativeCanvasImage::SetSrc),
                InstanceAccessor("onload", nullptr, &NativeCanvasImage::SetOnload),
                InstanceAccessor("onerror", nullptr, &NativeCanvasImage::SetOnerror),
                InstanceMethod("dispose", &NativeCanvasImage::DisposeJs),
                InstanceMethod("close", &NativeCanvasImage::DisposeJs),
                InstanceMethod("_getNativeImageData", &NativeCanvasImage::GetNativeImageData),
                // TODO: This should be set directly on the JS Object rather than via an instanceAccessor see: https://github.com/BabylonJS/BabylonNative/issues/1030
                InstanceAccessor("_imageContainer", &NativeCanvasImage::GetImageContainer, nullptr),
            });

        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_IMAGE_CONSTRUCTOR_NAME, func);

        auto global = env.Global();
        if (global.Get(JS_IMAGE_CONSTRUCTOR_NAME).IsUndefined())
        {
            global.Set(JS_IMAGE_CONSTRUCTOR_NAME, func);
        }
        if (global.Get("HTMLImageElement").IsUndefined())
        {
            global.Set("HTMLImageElement", func);
        }
        if (global.Get("createImageBitmap").IsUndefined())
        {
            global.Set("createImageBitmap", Napi::Function::New(env, &NativeCanvasImage::CreateImageBitmap, "createImageBitmap"));
        }
    }

    Napi::Object NativeCanvasImage::DecodeImageBitmap(Napi::Env env, const Napi::Value& source)
    {
        const std::byte* data{};
        size_t size{};
        if (source.IsArrayBuffer())
        {
            auto buffer = source.As<Napi::ArrayBuffer>();
            data = static_cast<const std::byte*>(buffer.Data());
            size = buffer.ByteLength();
        }
        else if (source.IsTypedArray())
        {
            auto typedArray = source.As<Napi::TypedArray>();
            auto buffer = typedArray.ArrayBuffer();
            data = static_cast<const std::byte*>(buffer.Data()) + typedArray.ByteOffset();
            size = typedArray.ByteLength();
        }
        else
        {
            throw Napi::TypeError::New(env, "createImageBitmap expected a Blob or encoded image buffer.");
        }

        if (data == nullptr || size == 0)
        {
            throw Napi::Error::New(env, "createImageBitmap received an empty image buffer.");
        }

        auto constructor = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_IMAGE_CONSTRUCTOR_NAME).As<Napi::Function>();
        auto imageObject = constructor.New({});
        auto* image = NativeCanvasImage::Unwrap(imageObject);
        if (image == nullptr || !image->SetBuffer({data, size}))
        {
            throw Napi::Error::New(env, "createImageBitmap could not decode the supplied image.");
        }

        return imageObject;
    }

    Napi::Object NativeCanvasImage::SnapshotImageBitmap(Napi::Env env, const Napi::Object& source)
    {
        Napi::Object pixelSource;
        auto nativeImageData = source.Get("_getNativeImageData");
        if (nativeImageData.IsFunction())
        {
            auto payload = nativeImageData.As<Napi::Function>().Call(source, {});
            if (!payload.IsObject())
            {
                throw Napi::Error::New(env, "createImageBitmap source image has no pixel data.");
            }
            pixelSource = payload.As<Napi::Object>();
        }
        else
        {
            auto getContext = source.Get("getContext");
            if (getContext.IsFunction())
            {
                const auto width = source.Get("width").ToNumber().Uint32Value();
                const auto height = source.Get("height").ToNumber().Uint32Value();
                auto contextValue = getContext.As<Napi::Function>().Call(source, {Napi::String::New(env, "2d")});
                if (!contextValue.IsObject())
                {
                    throw Napi::Error::New(env, "createImageBitmap could not access the canvas 2D context.");
                }

                auto context = contextValue.As<Napi::Object>();
                auto getImageData = context.Get("getImageData");
                if (!getImageData.IsFunction())
                {
                    throw Napi::Error::New(env, "createImageBitmap canvas context cannot read pixels.");
                }
                auto imageData = getImageData.As<Napi::Function>().Call(context, {
                    Napi::Number::New(env, 0),
                    Napi::Number::New(env, 0),
                    Napi::Number::New(env, width),
                    Napi::Number::New(env, height),
                });
                if (!imageData.IsObject())
                {
                    throw Napi::Error::New(env, "createImageBitmap canvas readback did not return ImageData.");
                }
                pixelSource = imageData.As<Napi::Object>();
            }
            else if (source.Get("data").IsTypedArray())
            {
                pixelSource = source;
            }
            else
            {
                throw Napi::TypeError::New(env, "createImageBitmap could not read the supplied image source.");
            }
        }

        auto widthValue = pixelSource.Get("width");
        auto heightValue = pixelSource.Get("height");
        auto dataValue = pixelSource.Get("data");
        if (!widthValue.IsNumber() || !heightValue.IsNumber() || !dataValue.IsTypedArray())
        {
            throw Napi::TypeError::New(env, "createImageBitmap source pixel data is invalid.");
        }

        const auto width = widthValue.As<Napi::Number>().Uint32Value();
        const auto height = heightValue.As<Napi::Number>().Uint32Value();
        auto data = dataValue.As<Napi::TypedArray>();
        const auto expectedLength = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
        if (width == 0 || height == 0 || data.ElementSize() != 1 ||
            expectedLength > std::numeric_limits<size_t>::max() || data.ByteLength() < expectedLength)
        {
            throw Napi::RangeError::New(env, "createImageBitmap source pixel data does not match its dimensions.");
        }

        auto buffer = data.ArrayBuffer();
        const auto* begin = static_cast<const uint8_t*>(buffer.Data()) + data.ByteOffset();
        std::vector<uint8_t> pixels(begin, begin + static_cast<size_t>(expectedLength));
        auto constructor = JsRuntime::NativeObject::GetFromJavaScript(env).Get(JS_IMAGE_CONSTRUCTOR_NAME).As<Napi::Function>();
        auto imageObject = constructor.New({});
        auto* image = NativeCanvasImage::Unwrap(imageObject);
        if (image == nullptr || !image->SetPixels(width, height, std::move(pixels)))
        {
            throw Napi::Error::New(env, "createImageBitmap could not snapshot the supplied image source.");
        }
        return imageObject;
    }

    Napi::Value NativeCanvasImage::CreateImageBitmap(const Napi::CallbackInfo& info)
    {
        auto env = info.Env();
        auto deferred = Napi::Promise::Deferred::New(env);
        if (info.Length() == 0)
        {
            deferred.Reject(Napi::TypeError::New(env, "createImageBitmap requires an image source.").Value());
            return deferred.Promise();
        }

        try
        {
            auto source = info[0];
            if (source.IsArrayBuffer() || source.IsTypedArray())
            {
                deferred.Resolve(DecodeImageBitmap(env, source));
                return deferred.Promise();
            }

            if (!source.IsObject())
            {
                throw Napi::TypeError::New(env, "createImageBitmap expected a Blob, image, or canvas source.");
            }

            auto sourceObject = source.As<Napi::Object>();
            if ((sourceObject.Has("_getNativeImageData") && sourceObject.Get("_getNativeImageData").IsFunction()) ||
                sourceObject.Get("getContext").IsFunction() || sourceObject.Get("data").IsTypedArray())
            {
                deferred.Resolve(SnapshotImageBitmap(env, sourceObject));
                return deferred.Promise();
            }

            auto arrayBufferValue = sourceObject.Get("arrayBuffer");
            if (!arrayBufferValue.IsFunction())
            {
                throw Napi::TypeError::New(env, "createImageBitmap could not read the supplied image source.");
            }

            auto bufferPromise = arrayBufferValue.As<Napi::Function>().Call(sourceObject, {});
            if (!bufferPromise.IsObject())
            {
                throw Napi::Error::New(env, "createImageBitmap source arrayBuffer() did not return a Promise.");
            }

            auto promiseObject = bufferPromise.As<Napi::Object>();
            auto thenValue = promiseObject.Get("then");
            if (!thenValue.IsFunction())
            {
                throw Napi::Error::New(env, "createImageBitmap source arrayBuffer() did not return a Promise.");
            }

            auto onResolved = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& callbackInfo) {
                try
                {
                    deferred.Resolve(DecodeImageBitmap(callbackInfo.Env(), callbackInfo[0]));
                }
                catch (const Napi::Error& error)
                {
                    deferred.Reject(error.Value());
                }
            });
            auto onRejected = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& callbackInfo) {
                deferred.Reject(callbackInfo.Length() > 0 ? callbackInfo[0] : Napi::Error::New(callbackInfo.Env(), "createImageBitmap could not read the supplied Blob.").Value());
            });
            thenValue.As<Napi::Function>().Call(promiseObject, {onResolved, onRejected});
        }
        catch (const Napi::Error& error)
        {
            deferred.Reject(error.Value());
        }
        return deferred.Promise();
    }

    NativeCanvasImage::NativeCanvasImage(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<NativeCanvasImage>{info}
        , m_runtimeScheduler{JsRuntime::GetFromJavaScript(info.Env())}
        , m_cancellationSource{std::make_shared<arcana::cancellation_source>()}
    {
    }

    NativeCanvasImage::~NativeCanvasImage()
    {
        Dispose();
    }

    void NativeCanvasImage::Dispose()
    {
        m_rgbaData.clear();
        m_cancellationSource->cancel();
    }

    void NativeCanvasImage::DisposeJs(const Napi::CallbackInfo&)
    {
        Dispose();
    }

    Napi::Value NativeCanvasImage::GetWidth(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_width);
    }

    Napi::Value NativeCanvasImage::GetHeight(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_height);
    }

    Napi::Value NativeCanvasImage::GetNaturalWidth(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_width);
    }

    Napi::Value NativeCanvasImage::GetNaturalHeight(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_height);
    }

    Napi::Value NativeCanvasImage::GetSrc(const Napi::CallbackInfo&)
    {
        return Napi::Value::From(Env(), m_src);
    }

    Napi::Value NativeCanvasImage::GetImageContainer(const Napi::CallbackInfo&)
    {
        // CanvasWgpu does not expose a bgfx/bimg image container.
        return Env().Null();
    }

    Napi::Value NativeCanvasImage::GetNativeImageData(const Napi::CallbackInfo& info)
    {
        if (m_rgbaData.empty() || m_width == 0 || m_height == 0)
        {
            return info.Env().Null();
        }

        auto buffer = Napi::ArrayBuffer::New(info.Env(), m_rgbaData.size());
        std::memcpy(buffer.Data(), m_rgbaData.data(), m_rgbaData.size());

        auto result = Napi::Object::New(info.Env());
        result.Set("width", Napi::Number::From(info.Env(), m_width));
        result.Set("height", Napi::Number::From(info.Env(), m_height));
        result.Set("data", Napi::Uint8Array::New(info.Env(), m_rgbaData.size(), buffer, 0));
        return result;
    }

    bool NativeCanvasImage::SetBuffer(gsl::span<const std::byte> buffer)
    {
        const auto* encodedData = reinterpret_cast<const uint8_t*>(buffer.data());
        const auto encodedSize = buffer.size_bytes();

        uint32_t decodedWidth{};
        uint32_t decodedHeight{};
        uint8_t* decodedRgba{};
        size_t decodedLength{};
        const auto decodeSuccess = babylon_canvas_decode_image_rgba(
            encodedData,
            encodedSize,
            &decodedWidth,
            &decodedHeight,
            &decodedRgba,
            &decodedLength);

        if (!decodeSuccess || decodedRgba == nullptr || decodedWidth == 0 || decodedHeight == 0)
        {
            return false;
        }

        const auto expectedLength = static_cast<uint64_t>(decodedWidth) * static_cast<uint64_t>(decodedHeight) * 4ull;
        if (expectedLength != decodedLength)
        {
            babylon_canvas_free_bytes(decodedRgba, decodedLength);
            return false;
        }

        m_width = decodedWidth;
        m_height = decodedHeight;
        m_rgbaData.assign(decodedRgba, decodedRgba + decodedLength);
        babylon_canvas_free_bytes(decodedRgba, decodedLength);

        if (!m_onloadHandlerRef.IsEmpty())
        {
            m_onloadHandlerRef.Call({});
        }
        return true;
    }

    bool NativeCanvasImage::SetPixels(uint32_t width, uint32_t height, std::vector<uint8_t> pixels)
    {
        const auto expectedLength = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
        if (width == 0 || height == 0 || expectedLength != pixels.size())
        {
            return false;
        }

        m_width = width;
        m_height = height;
        m_rgbaData = std::move(pixels);
        return true;
    }

    void NativeCanvasImage::SetSrc(const Napi::CallbackInfo& info, const Napi::Value& value)
    {
        auto text{value.As<Napi::String>().Utf8Value()};
        m_src = text;

        // try with base64
        static const std::string base64{"base64,"};
        const auto pos = text.find(base64);
        if (pos != std::string::npos)
        {
            ScopedNativeCanvasImageRef pendingLoadRef{*this};
            arcana::make_task(m_runtimeScheduler, *m_cancellationSource, [env{info.Env()}, this, text{std::move(text)}, pos]() {
                ScopedNativeCanvasImageRef callbackLoadRef{*this, ScopedNativeCanvasImageRef::AdoptTag{}};
                std::vector<uint8_t> base64Buffer;
                bn::decode_b64(text.begin() + pos + base64.length(), text.end(), std::back_inserter(base64Buffer));
                gsl::span<const std::byte> buffer = {reinterpret_cast<std::byte*>(base64Buffer.data()), base64Buffer.size()};

                if (!SetBuffer(buffer))
                {
                    HandleLoadImageError(Napi::Error::New(env, "Unable to decode image with provided base64 source."));
                }
            });
            pendingLoadRef.Release();
            return;
        }

        // try with URL
        UrlLib::UrlRequest request{};
        request.Open(UrlLib::UrlMethod::Get, NormalizeImageUrl(text));
        request.ResponseType(UrlLib::UrlResponseType::Buffer);
        ScopedNativeCanvasImageRef pendingLoadRef{*this};
        request.SendAsync().then(m_runtimeScheduler, *m_cancellationSource, [env{info.Env()}, this, cancellationSource{m_cancellationSource}, request{std::move(request)}](arcana::expected<void, std::exception_ptr> result) {
            ScopedNativeCanvasImageRef callbackLoadRef{*this, ScopedNativeCanvasImageRef::AdoptTag{}};
            if (result.has_error())
            {
                HandleLoadImageError(Napi::Error::New(env, result.error()));
                return;
            }

            m_rgbaData.clear();
            m_width = 1;
            m_height = 1;

            auto buffer{request.ResponseBuffer()};
            if (buffer.data() == nullptr || buffer.size_bytes() == 0)
            {
                HandleLoadImageError(Napi::Error::New(env, "Image with provided source returned empty response or invalid base64."));
                return;
            }

            if (!SetBuffer(buffer))
            {
                HandleLoadImageError(Napi::Error::New(env, "Unable to decode image with provided source URL."));
            }
        });
        pendingLoadRef.Release();
    }

    void NativeCanvasImage::SetOnload(const Napi::CallbackInfo&, const Napi::Value& value)
    {
        Napi::Function eventHandler{value.As<Napi::Function>()};
        m_onloadHandlerRef = Napi::Persistent(eventHandler);
    }

    void NativeCanvasImage::SetOnerror(const Napi::CallbackInfo&, const Napi::Value& value)
    {
        Napi::Function eventHandler{value.As<Napi::Function>()};
        m_onerrorHandlerRef = Napi::Persistent(eventHandler);
    }

    int NativeCanvasImage::CreateNVGImageForContext(NVGcontext* nvgContext) const
    {
        if (m_rgbaData.empty())
        {
            static constexpr unsigned char transparentPixel[4] = {0, 0, 0, 0};
            return nvgCreateImageRGBA(nvgContext, 1, 1, 0, transparentPixel);
        }

        return nvgCreateImageRGBA(nvgContext, static_cast<int>(m_width), static_cast<int>(m_height), 0, m_rgbaData.data());
    }

    void NativeCanvasImage::HandleLoadImageError(const Napi::Error& error)
    {
        if (!m_onerrorHandlerRef.IsEmpty())
        {
            m_onerrorHandlerRef.Call({error.Value()});
            return;
        }

        error.ThrowAsJavaScriptException();
    }
}
