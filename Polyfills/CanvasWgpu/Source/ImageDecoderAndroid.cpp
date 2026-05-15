#include <AndroidExtensions/Globals.h>
#include <android/bitmap.h>
#include <android/imagedecoder.h>
#include <jni.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <memory>

namespace
{
#if defined(__clang__)
#define BABYLON_NO_ASAN __attribute__((no_sanitize("address")))
#else
#define BABYLON_NO_ASAN
#endif

    class LocalRef final
    {
    public:
        LocalRef(JNIEnv* env, jobject value)
            : m_env{env}
            , m_value{value}
        {
        }

        LocalRef(const LocalRef&) = delete;
        LocalRef& operator=(const LocalRef&) = delete;

        LocalRef(LocalRef&& other) noexcept
            : m_env{other.m_env}
            , m_value{other.m_value}
        {
            other.m_env = nullptr;
            other.m_value = nullptr;
        }

        LocalRef& operator=(LocalRef&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_env = other.m_env;
                m_value = other.m_value;
                other.m_env = nullptr;
                other.m_value = nullptr;
            }
            return *this;
        }

        ~LocalRef()
        {
            Reset();
        }

        jobject Get() const
        {
            return m_value;
        }

        jobject Release()
        {
            auto* value = m_value;
            m_value = nullptr;
            return value;
        }

        void Reset(jobject value = nullptr)
        {
            if (m_env != nullptr && m_value != nullptr)
            {
                m_env->DeleteLocalRef(m_value);
            }
            m_value = value;
        }

    private:
        JNIEnv* m_env{};
        jobject m_value{};
    };

    class LockedBitmapPixels final
    {
    public:
        LockedBitmapPixels(JNIEnv* env, jobject bitmap)
            : m_env{env}
            , m_bitmap{bitmap}
        {
        }

        LockedBitmapPixels(const LockedBitmapPixels&) = delete;
        LockedBitmapPixels& operator=(const LockedBitmapPixels&) = delete;

        ~LockedBitmapPixels()
        {
            if (m_pixels != nullptr)
            {
                AndroidBitmap_unlockPixels(m_env, m_bitmap);
            }
        }

        bool Lock()
        {
            return AndroidBitmap_lockPixels(m_env, m_bitmap, &m_pixels) == ANDROID_BITMAP_RESULT_SUCCESS;
        }

        const uint8_t* Pixels() const
        {
            return static_cast<const uint8_t*>(m_pixels);
        }

    private:
        JNIEnv* m_env{};
        jobject m_bitmap{};
        void* m_pixels{};
    };

    struct AImageDecoderFunctions final
    {
        using CreateFromBufferFn = int (*)(const void*, size_t, AImageDecoder**);
        using DeleteFn = void (*)(AImageDecoder*);
        using SetAndroidBitmapFormatFn = int (*)(AImageDecoder*, int32_t);
        using SetUnpremultipliedRequiredFn = int (*)(AImageDecoder*, bool);
        using GetHeaderInfoFn = const AImageDecoderHeaderInfo* (*)(const AImageDecoder*);
        using HeaderInfoGetSizeFn = int32_t (*)(const AImageDecoderHeaderInfo*);
        using GetMinimumStrideFn = size_t (*)(AImageDecoder*);
        using DecodeImageFn = int (*)(AImageDecoder*, void*, size_t, size_t);

        void* Library{};
        CreateFromBufferFn CreateFromBuffer{};
        DeleteFn Delete{};
        SetAndroidBitmapFormatFn SetAndroidBitmapFormat{};
        SetUnpremultipliedRequiredFn SetUnpremultipliedRequired{};
        GetHeaderInfoFn GetHeaderInfo{};
        HeaderInfoGetSizeFn HeaderInfoGetWidth{};
        HeaderInfoGetSizeFn HeaderInfoGetHeight{};
        GetMinimumStrideFn GetMinimumStride{};
        DecodeImageFn DecodeImage{};

        bool IsAvailable() const
        {
            return Library != nullptr &&
                CreateFromBuffer != nullptr &&
                Delete != nullptr &&
                SetAndroidBitmapFormat != nullptr &&
                SetUnpremultipliedRequired != nullptr &&
                GetHeaderInfo != nullptr &&
                HeaderInfoGetWidth != nullptr &&
                HeaderInfoGetHeight != nullptr &&
                GetMinimumStride != nullptr &&
                DecodeImage != nullptr;
        }
    };

    template <typename T>
    bool LoadImageDecoderSymbol(void* library, T& target, const char* name)
    {
        target = reinterpret_cast<T>(dlsym(library, name));
        return target != nullptr;
    }

    AImageDecoderFunctions LoadAImageDecoderFunctions()
    {
        AImageDecoderFunctions functions{};
        functions.Library = dlopen("libjnigraphics.so", RTLD_NOW | RTLD_LOCAL);
        if (functions.Library == nullptr)
        {
            return functions;
        }

        if (!LoadImageDecoderSymbol(functions.Library, functions.CreateFromBuffer, "AImageDecoder_createFromBuffer") ||
            !LoadImageDecoderSymbol(functions.Library, functions.Delete, "AImageDecoder_delete") ||
            !LoadImageDecoderSymbol(functions.Library, functions.SetAndroidBitmapFormat, "AImageDecoder_setAndroidBitmapFormat") ||
            !LoadImageDecoderSymbol(functions.Library, functions.SetUnpremultipliedRequired, "AImageDecoder_setUnpremultipliedRequired") ||
            !LoadImageDecoderSymbol(functions.Library, functions.GetHeaderInfo, "AImageDecoder_getHeaderInfo") ||
            !LoadImageDecoderSymbol(functions.Library, functions.HeaderInfoGetWidth, "AImageDecoderHeaderInfo_getWidth") ||
            !LoadImageDecoderSymbol(functions.Library, functions.HeaderInfoGetHeight, "AImageDecoderHeaderInfo_getHeight") ||
            !LoadImageDecoderSymbol(functions.Library, functions.GetMinimumStride, "AImageDecoder_getMinimumStride") ||
            !LoadImageDecoderSymbol(functions.Library, functions.DecodeImage, "AImageDecoder_decodeImage"))
        {
            dlclose(functions.Library);
            functions = {};
        }

        return functions;
    }

    const AImageDecoderFunctions* GetAImageDecoderFunctions()
    {
        static const auto functions = LoadAImageDecoderFunctions();
        return functions.IsAvailable() ? &functions : nullptr;
    }

    class NativeImageDecoder final
    {
    public:
        NativeImageDecoder(const AImageDecoderFunctions& functions, AImageDecoder* decoder)
            : m_functions{functions}
            , m_decoder{decoder}
        {
        }

        NativeImageDecoder(const NativeImageDecoder&) = delete;
        NativeImageDecoder& operator=(const NativeImageDecoder&) = delete;

        ~NativeImageDecoder()
        {
            if (m_decoder != nullptr)
            {
                m_functions.Delete(m_decoder);
            }
        }

        AImageDecoder* Get() const
        {
            return m_decoder;
        }

    private:
        const AImageDecoderFunctions& m_functions;
        AImageDecoder* m_decoder{};
    };

    bool ClearPendingJavaException(JNIEnv* env)
    {
        if (env == nullptr || !env->ExceptionCheck())
        {
            return false;
        }

        env->ExceptionClear();
        return true;
    }

    bool HasJavaException(JNIEnv* env)
    {
        return ClearPendingJavaException(env);
    }

    uint8_t UnpremultiplyChannel(uint8_t value, uint8_t alpha)
    {
        if (alpha == 0)
        {
            return 0;
        }

        if (alpha == 255)
        {
            return value;
        }

        const auto unpremultiplied = (static_cast<uint32_t>(value) * 255u + static_cast<uint32_t>(alpha) / 2u) / static_cast<uint32_t>(alpha);
        return static_cast<uint8_t>(std::min(unpremultiplied, 255u));
    }

    BABYLON_NO_ASAN void CopyBitmapRows(
        uint8_t* dst,
        const uint8_t* src,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        bool sourcePremultiplied)
    {
        const auto rowBytes = static_cast<size_t>(width) * 4u;
        for (uint32_t y = 0; y < height; ++y)
        {
            const auto* srcRow = src + static_cast<size_t>(y) * stride;
            auto* dstRow = dst + static_cast<size_t>(y) * rowBytes;

            if (!sourcePremultiplied)
            {
                for (size_t i = 0; i < rowBytes; ++i)
                {
                    dstRow[i] = srcRow[i];
                }
                continue;
            }

            for (uint32_t x = 0; x < width; ++x)
            {
                const auto srcOffset = static_cast<size_t>(x) * 4u;
                const auto alpha = srcRow[srcOffset + 3u];
                dstRow[srcOffset] = UnpremultiplyChannel(srcRow[srcOffset], alpha);
                dstRow[srcOffset + 1u] = UnpremultiplyChannel(srcRow[srcOffset + 1u], alpha);
                dstRow[srcOffset + 2u] = UnpremultiplyChannel(srcRow[srcOffset + 2u], alpha);
                dstRow[srcOffset + 3u] = alpha;
            }
        }
    }

    jclass FindClass(JNIEnv* env, const char* name)
    {
        auto* clazz = env->FindClass(name);
        if (HasJavaException(env))
        {
            return nullptr;
        }
        return clazz;
    }

    jfieldID GetStaticFieldID(JNIEnv* env, jclass clazz, const char* name, const char* signature)
    {
        auto* field = env->GetStaticFieldID(clazz, name, signature);
        if (HasJavaException(env))
        {
            return nullptr;
        }
        return field;
    }

    jmethodID GetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* signature)
    {
        auto* method = env->GetMethodID(clazz, name, signature);
        if (HasJavaException(env))
        {
            return nullptr;
        }
        return method;
    }

    jmethodID GetStaticMethodID(JNIEnv* env, jclass clazz, const char* name, const char* signature)
    {
        auto* method = env->GetStaticMethodID(clazz, name, signature);
        if (HasJavaException(env))
        {
            return nullptr;
        }
        return method;
    }

    bool EnsureRgba8888Bitmap(JNIEnv* env, LocalRef& bitmap, jobject argb8888Config)
    {
        AndroidBitmapInfo info{};
        if (AndroidBitmap_getInfo(env, bitmap.Get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS)
        {
            return false;
        }

        if (info.format == ANDROID_BITMAP_FORMAT_RGBA_8888)
        {
            return true;
        }

        auto* bitmapClass = FindClass(env, "android/graphics/Bitmap");
        LocalRef bitmapClassRef{env, bitmapClass};
        if (bitmapClass == nullptr)
        {
            return false;
        }

        auto* copyMethod = GetMethodID(env, bitmapClass, "copy", "(Landroid/graphics/Bitmap$Config;Z)Landroid/graphics/Bitmap;");
        if (copyMethod == nullptr)
        {
            return false;
        }

        auto* copied = env->CallObjectMethod(bitmap.Get(), copyMethod, argb8888Config, JNI_FALSE);
        if (HasJavaException(env) || copied == nullptr)
        {
            return false;
        }

        LocalRef copiedBitmap{env, copied};
        if (AndroidBitmap_getInfo(env, copiedBitmap.Get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
            info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
        {
            return false;
        }

        bitmap = std::move(copiedBitmap);
        return true;
    }

    bool DecodeImageRgbaWithAImageDecoder(
        const AImageDecoderFunctions& functions,
        const uint8_t* data,
        size_t len,
        uint32_t* outWidth,
        uint32_t* outHeight,
        uint8_t** outRgba,
        size_t* outLen)
    {
        AImageDecoder* rawDecoder{};
        if (functions.CreateFromBuffer(data, len, &rawDecoder) != ANDROID_IMAGE_DECODER_SUCCESS || rawDecoder == nullptr)
        {
            return false;
        }

        NativeImageDecoder decoder{functions, rawDecoder};
        if (functions.SetAndroidBitmapFormat(decoder.Get(), ANDROID_BITMAP_FORMAT_RGBA_8888) != ANDROID_IMAGE_DECODER_SUCCESS ||
            functions.SetUnpremultipliedRequired(decoder.Get(), true) != ANDROID_IMAGE_DECODER_SUCCESS)
        {
            return false;
        }

        const auto* headerInfo = functions.GetHeaderInfo(decoder.Get());
        if (headerInfo == nullptr)
        {
            return false;
        }

        const auto width = functions.HeaderInfoGetWidth(headerInfo);
        const auto height = functions.HeaderInfoGetHeight(headerInfo);
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        const auto outputWidth = static_cast<uint32_t>(width);
        const auto outputHeight = static_cast<uint32_t>(height);
        if (outputWidth > std::numeric_limits<uint32_t>::max() / 4u ||
            static_cast<size_t>(outputHeight) > std::numeric_limits<size_t>::max() / (static_cast<size_t>(outputWidth) * 4u))
        {
            return false;
        }

        const auto rowBytes = static_cast<size_t>(outputWidth) * 4u;
        const auto byteCount = rowBytes * static_cast<size_t>(outputHeight);
        const auto decodeStride = functions.GetMinimumStride(decoder.Get());
        if (decodeStride < rowBytes || static_cast<size_t>(outputHeight) > std::numeric_limits<size_t>::max() / decodeStride)
        {
            return false;
        }

        const auto decodeByteCount = decodeStride * static_cast<size_t>(outputHeight);
        std::unique_ptr<uint8_t[]> rgba;
        std::unique_ptr<uint8_t[]> decodeBuffer;
        uint8_t* decodeTarget{};
        if (decodeStride == rowBytes)
        {
            rgba = std::make_unique<uint8_t[]>(byteCount);
            decodeTarget = rgba.get();
        }
        else
        {
            decodeBuffer = std::make_unique<uint8_t[]>(decodeByteCount);
            decodeTarget = decodeBuffer.get();
        }

        if (functions.DecodeImage(decoder.Get(), decodeTarget, decodeStride, decodeByteCount) != ANDROID_IMAGE_DECODER_SUCCESS)
        {
            return false;
        }

        if (decodeStride != rowBytes)
        {
            rgba = std::make_unique<uint8_t[]>(byteCount);
            for (uint32_t y = 0; y < outputHeight; ++y)
            {
                std::copy_n(
                    decodeTarget + static_cast<size_t>(y) * decodeStride,
                    rowBytes,
                    rgba.get() + static_cast<size_t>(y) * rowBytes);
            }
        }

        *outWidth = outputWidth;
        *outHeight = outputHeight;
        *outRgba = rgba.release();
        *outLen = byteCount;
        return true;
    }

    bool DecodeImageRgbaWithBitmapFactory(
        const uint8_t* data,
        size_t len,
        uint32_t* outWidth,
        uint32_t* outHeight,
        uint8_t** outRgba,
        size_t* outLen)
    {
        if (data == nullptr ||
            len == 0 ||
            len > static_cast<size_t>(std::numeric_limits<jsize>::max()) ||
            outWidth == nullptr ||
            outHeight == nullptr ||
            outRgba == nullptr ||
            outLen == nullptr)
        {
            return false;
        }

        auto* env = android::global::GetEnvForCurrentThread();
        if (env == nullptr)
        {
            return false;
        }

        auto* bitmapFactoryClass = FindClass(env, "android/graphics/BitmapFactory");
        LocalRef bitmapFactoryClassRef{env, bitmapFactoryClass};
        auto* configClass = FindClass(env, "android/graphics/Bitmap$Config");
        LocalRef configClassRef{env, configClass};
        auto* bitmapClass = FindClass(env, "android/graphics/Bitmap");
        LocalRef bitmapClassRef{env, bitmapClass};
        if (bitmapFactoryClass == nullptr || configClass == nullptr || bitmapClass == nullptr)
        {
            return false;
        }

        auto* decodeMethod = GetStaticMethodID(env, bitmapFactoryClass, "decodeByteArray", "([BII)Landroid/graphics/Bitmap;");
        auto* argb8888Field = GetStaticFieldID(env, configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
        auto* isPremultipliedMethod = GetMethodID(env, bitmapClass, "isPremultiplied", "()Z");
        if (decodeMethod == nullptr ||
            argb8888Field == nullptr ||
            isPremultipliedMethod == nullptr)
        {
            return false;
        }

        auto* argb8888Config = env->GetStaticObjectField(configClass, argb8888Field);
        if (HasJavaException(env) || argb8888Config == nullptr)
        {
            return false;
        }
        LocalRef argb8888ConfigRef{env, argb8888Config};

        const auto encodedLength = static_cast<jsize>(len);
        auto* encodedBytes = env->NewByteArray(encodedLength);
        if (HasJavaException(env) || encodedBytes == nullptr)
        {
            return false;
        }
        LocalRef encodedBytesRef{env, encodedBytes};

        env->SetByteArrayRegion(encodedBytes, 0, encodedLength, reinterpret_cast<const jbyte*>(data));
        if (HasJavaException(env))
        {
            return false;
        }

        auto* decoded = env->CallStaticObjectMethod(bitmapFactoryClass, decodeMethod, encodedBytes, 0, encodedLength);
        if (HasJavaException(env) || decoded == nullptr)
        {
            return false;
        }

        LocalRef bitmap{env, decoded};
        if (!EnsureRgba8888Bitmap(env, bitmap, argb8888Config))
        {
            return false;
        }

        AndroidBitmapInfo info{};
        if (AndroidBitmap_getInfo(env, bitmap.Get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
            info.width == 0 ||
            info.height == 0 ||
            info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
        {
            return false;
        }

        const auto width = info.width;
        const auto height = info.height;
        if (width > std::numeric_limits<uint32_t>::max() / 4u ||
            static_cast<size_t>(height) > std::numeric_limits<size_t>::max() / (static_cast<size_t>(width) * 4u))
        {
            return false;
        }

        const auto rowBytes = static_cast<size_t>(width) * 4u;
        const auto byteCount = rowBytes * static_cast<size_t>(height);
        if (info.stride < rowBytes)
        {
            return false;
        }

        auto rgba = std::make_unique<uint8_t[]>(byteCount);
        LockedBitmapPixels lockedPixels{env, bitmap.Get()};
        if (!lockedPixels.Lock() || lockedPixels.Pixels() == nullptr)
        {
            return false;
        }

        const bool sourcePremultiplied = env->CallBooleanMethod(bitmap.Get(), isPremultipliedMethod) == JNI_TRUE;
        if (HasJavaException(env))
        {
            return false;
        }

        CopyBitmapRows(rgba.get(), lockedPixels.Pixels(), width, height, info.stride, sourcePremultiplied);

        *outWidth = width;
        *outHeight = height;
        *outRgba = rgba.release();
        *outLen = byteCount;
        return true;
    }

    bool DecodeImageRgba(
        const uint8_t* data,
        size_t len,
        uint32_t* outWidth,
        uint32_t* outHeight,
        uint8_t** outRgba,
        size_t* outLen)
    {
        if (data == nullptr ||
            len == 0 ||
            outWidth == nullptr ||
            outHeight == nullptr ||
            outRgba == nullptr ||
            outLen == nullptr)
        {
            return false;
        }

        if (const auto* imageDecoderFunctions = GetAImageDecoderFunctions())
        {
            return DecodeImageRgbaWithAImageDecoder(*imageDecoderFunctions, data, len, outWidth, outHeight, outRgba, outLen);
        }

        if (len > static_cast<size_t>(std::numeric_limits<jsize>::max()))
        {
            return false;
        }

        return DecodeImageRgbaWithBitmapFactory(data, len, outWidth, outHeight, outRgba, outLen);
    }
}

extern "C"
{
    int32_t babylon_canvas_decode_image_rgba(
        const uint8_t* data,
        size_t len,
        uint32_t* outWidth,
        uint32_t* outHeight,
        uint8_t** outRgba,
        size_t* outLen)
    {
        if (outWidth != nullptr)
        {
            *outWidth = 0;
        }
        if (outHeight != nullptr)
        {
            *outHeight = 0;
        }
        if (outRgba != nullptr)
        {
            *outRgba = nullptr;
        }
        if (outLen != nullptr)
        {
            *outLen = 0;
        }

        try
        {
            return DecodeImageRgba(data, len, outWidth, outHeight, outRgba, outLen) ? 1 : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    void babylon_canvas_free_bytes(uint8_t* data, size_t len)
    {
        (void)len;
        delete[] data;
    }
}
