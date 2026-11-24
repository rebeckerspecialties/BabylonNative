#pragma once

#include <AndroidExtensions/Globals.h>
#include <Babylon/Graphics/ExternalImagePixels.h>
#include <android/bitmap.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

namespace Babylon
{
    // Java framework media services, not a bundled codec. AssetFileDescriptor
    // includes the offset and length within the uncompressed APK entry.
    class AndroidAssetPlayer final : public xr::MediaSurfaceProducer, public Graphics::ExternalImagePixels
    {
        std::mutex m_mutex;
        jobject m_player{}, m_retriever{};
        std::string m_path;
        jobject m_decoder{};
        jlong m_requestedTime{};
        bool m_compositorSurface{};
        std::vector<uint8_t> m_rgba;
        uint32_t m_width{}, m_height{};
        std::mutex m_frameMutex;
        std::condition_variable m_frameRequested;
        std::condition_variable m_frameReady;
        std::thread m_worker;
        bool m_stop{}, m_requested{};
        std::exception_ptr m_decodeError;
        std::vector<uint8_t> m_front;
        uint32_t m_frontWidth{}, m_frontHeight{};
        struct Locals
        {
            JNIEnv* e{android::global::GetEnvForCurrentThread()};
            Locals()
            {
                if (e->PushLocalFrame(80))
                    throw std::runtime_error{"Could not allocate JNI local frame"};
            }
            ~Locals() { e->PopLocalFrame(nullptr); }
        };
        static void Check(JNIEnv* e, const char* op)
        {
            if (!e->ExceptionCheck())
                return;
            const auto cause = e->ExceptionOccurred();
            e->ExceptionClear();
            const auto text = static_cast<jstring>(e->CallObjectMethod(cause, e->GetMethodID(e->FindClass("java/lang/Throwable"), "toString", "()Ljava/lang/String;")));
            const auto* chars = text ? e->GetStringUTFChars(text, nullptr) : nullptr;
            std::string message = std::string{op} + (chars ? std::string{": "} + chars : "");
            if (chars)
                e->ReleaseStringUTFChars(text, chars);
            throw std::runtime_error{message};
        }
        static jmethodID Method(JNIEnv* e, jobject o, const char* name, const char* sig)
        {
            auto m = e->GetMethodID(e->GetObjectClass(o), name, sig);
            Check(e, name);
            return m;
        }
        static jobject Open(JNIEnv* e, const char* type, const std::string& path)
        {
            auto cls = e->FindClass(type);
            auto obj = e->NewObject(cls, e->GetMethodID(cls, "<init>", "()V"));
            Check(e, type);
            auto assets = android::global::GetAppContext().getAssets();
            auto manager = static_cast<jobject>(assets);
            auto afd = e->CallObjectMethod(manager, Method(e, manager, "openFd", "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;"), e->NewStringUTF(path.c_str()));
            Check(e, path.c_str());
            try
            {
                auto fd = e->CallObjectMethod(afd, Method(e, afd, "getFileDescriptor", "()Ljava/io/FileDescriptor;"));
                auto off = e->CallLongMethod(afd, Method(e, afd, "getStartOffset", "()J"));
                auto len = e->CallLongMethod(afd, Method(e, afd, "getLength", "()J"));
                e->CallVoidMethod(obj, Method(e, obj, "setDataSource", "(Ljava/io/FileDescriptor;JJ)V"), fd, off, len);
                Check(e, "setDataSource");
            }
            catch (...)
            {
                e->CallVoidMethod(afd, Method(e, afd, "close", "()V"));
                throw;
            }
            e->CallVoidMethod(afd, Method(e, afd, "close", "()V"));
            Check(e, "close");
            return obj;
        }
        static void Release(JNIEnv* e, jobject& obj)
        {
            if (!obj)
                return;
            e->CallVoidMethod(obj, Method(e, obj, "release", "()V"));
            if (e->ExceptionCheck())
                e->ExceptionClear();
            e->DeleteGlobalRef(obj);
            obj = nullptr;
        }
        void ResetFrames()
        {
            {
                std::lock_guard lock{m_frameMutex};
                m_stop = true;
            }
            m_frameRequested.notify_one();
            if (m_worker.joinable())
                m_worker.join();
            Release(android::global::GetEnvForCurrentThread(), m_decoder);
            m_stop = m_requested = false;
            m_decodeError = nullptr;
            m_front.clear();
            m_rgba.clear();
            m_width = m_height = 0;
        }
        bool RequestStreamingFrame(JNIEnv* e)
        {
            if (m_compositorSurface)
                return false;
            if (!m_decoder)
            {
                auto context = android::global::GetAppContext();
                auto loader = e->CallObjectMethod(context, Method(e, context, "getClassLoader", "()Ljava/lang/ClassLoader;"));
                auto cls = static_cast<jclass>(e->CallObjectMethod(loader, Method(e, loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"),
                    e->NewStringUTF("com.babylonjs.embedding.TextureVideoDecoder")));
                Check(e, "load video texture decoder");
                auto assets = android::global::GetAppContext().getAssets();
                auto decoder = e->NewObject(cls, e->GetMethodID(cls, "<init>", "(Landroid/content/res/AssetManager;Ljava/lang/String;)V"),
                    static_cast<jobject>(assets), e->NewStringUTF(m_path.c_str()));
                Check(e, "open video texture decoder");
                m_decoder = e->NewGlobalRef(decoder);
                m_worker = std::thread{[this] { DecodeLoop(); }};
            }
            const auto time = e->CallIntMethod(m_player, Method(e, m_player, "getCurrentPosition", "()I"));
            Check(e, "get video texture time");
            std::lock_guard lock{m_frameMutex};
            if (m_decodeError)
                std::rethrow_exception(m_decodeError);
            m_requestedTime = 1000LL * time;
            m_requested = true;
            m_frameRequested.notify_one();
            return true;
        }
        void DecodeLoop()
        {
            try
            {
                std::unique_lock lock{m_frameMutex};
                for (;;)
                {
                    m_frameRequested.wait(lock, [this] { return m_requested || m_stop; });
                    if (m_stop)
                        return;
                    m_requested = false;
                    const auto timeUs = m_requestedTime;
                    lock.unlock();
                    const bool decoded = DecodeLatestFrame(timeUs);
                    lock.lock();
                    if (decoded)
                    {
                        m_front.swap(m_rgba);
                        m_frontWidth = m_width;
                        m_frontHeight = m_height;
                        m_frameReady.notify_one();
                    }
                }
            }
            catch (...)
            {
                std::lock_guard lock{m_frameMutex};
                m_decodeError = std::current_exception();
                m_frameReady.notify_one();
            }
        }
        bool DecodeLatestFrame(jlong timeUs)
        {
            Locals locals;
            auto* e = locals.e;
            const auto image = e->CallObjectMethod(m_decoder, Method(e, m_decoder, "frameAt", "(J)Landroid/media/Image;"), timeUs);
            Check(e, "decode linear video image");
            if (!image)
                return false;
            const auto rect = e->CallObjectMethod(image, Method(e, image, "getCropRect", "()Landroid/graphics/Rect;"));
            auto coordinate = [&](const char* name) { return e->GetIntField(rect, e->GetFieldID(e->GetObjectClass(rect), name, "I")); };
            struct { int left, top, right, bottom; } crop{coordinate("left"), coordinate("top"), coordinate("right"), coordinate("bottom")};
            const auto array = static_cast<jobjectArray>(e->CallObjectMethod(image, Method(e, image, "getPlanes", "()[Landroid/media/Image$Plane;")));
            Check(e, "get video image planes");
            if (crop.left < 0 || crop.top < 0 || crop.right <= crop.left || crop.bottom <= crop.top || e->GetArrayLength(array) != 3)
                throw std::runtime_error{"Invalid flexible YUV video image"};
            struct Plane { const uint8_t* data; int row, step; } planes[3]{};
            for (int i = 0; i < 3; ++i)
            {
                auto& p = planes[i];
                const auto plane = e->GetObjectArrayElement(array, i);
                const auto buffer = e->CallObjectMethod(plane, Method(e, plane, "getBuffer", "()Ljava/nio/ByteBuffer;"));
                p.row = e->CallIntMethod(plane, Method(e, plane, "getRowStride", "()I"));
                p.step = e->CallIntMethod(plane, Method(e, plane, "getPixelStride", "()I"));
                const int position = e->CallIntMethod(buffer, Method(e, buffer, "position", "()I"));
                const int limit = e->CallIntMethod(buffer, Method(e, buffer, "limit", "()I"));
                Check(e, "get video plane layout");
                p.data = static_cast<const uint8_t*>(e->GetDirectBufferAddress(buffer));
                if (!p.data || position < 0 || limit > e->GetDirectBufferCapacity(buffer) || p.row <= 0 || p.step <= 0 ||
                    position + int64_t((crop.bottom - 1) >> (i != 0)) * p.row + int64_t((crop.right - 1) >> (i != 0)) * p.step >= limit)
                    throw std::runtime_error{"Invalid decoder YUV plane bounds"};
                p.data += position;
            }
            // Android MediaFormat: COLOR_STANDARD_BT709 and COLOR_RANGE_FULL.
            const bool hd = e->CallIntMethod(m_decoder, Method(e, m_decoder, "colorStandard", "()I")) == 1;
            const bool full = e->CallIntMethod(m_decoder, Method(e, m_decoder, "colorRange", "()I")) == 1;
            Check(e, "get video color format");
            const int rv = hd ? (full ? 403 : 459) : (full ? 359 : 409);
            const int gu = hd ? (full ? 48 : 55) : (full ? 88 : 100);
            const int gv = hd ? (full ? 120 : 136) : (full ? 183 : 208);
            const int bu = hd ? (full ? 475 : 541) : (full ? 454 : 516);
            m_width = static_cast<uint32_t>(crop.right - crop.left);
            m_height = static_cast<uint32_t>(crop.bottom - crop.top);
            m_rgba.resize(size_t(m_width) * m_height * 4);
            uint8_t* dst = m_rgba.data();
            auto channel = [](int v) { return static_cast<uint8_t>(std::clamp((v + 128) >> 8, 0, 255)); };
            for (int y = crop.top; y < crop.bottom; ++y)
            {
                const auto* yRow = planes[0].data + size_t(y) * planes[0].row;
                const auto* uRow = planes[1].data + size_t(y / 2) * planes[1].row;
                const auto* vRow = planes[2].data + size_t(y / 2) * planes[2].row;
                for (int x = crop.left; x < crop.right; ++x)
                {
                    int luma = yRow[x * planes[0].step];
                    luma = full ? 256 * luma : 298 * (luma - 16);
                    const int u = uRow[(x / 2) * planes[1].step] - 128;
                    const int v = vRow[(x / 2) * planes[2].step] - 128;
                    *dst++ = channel(luma + rv * v);
                    *dst++ = channel(luma - gu * u - gv * v);
                    *dst++ = channel(luma + bu * u);
                    *dst++ = 255;
                }
            }
            return true;
        }

    public:
        ~AndroidAssetPlayer() { Reset(); }
        void Reset()
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            Release(l.e, m_retriever);
            Release(l.e, m_player);
            ResetFrames();
            m_compositorSurface = false;
            m_path.clear();
        }
        void Play(const std::string& url, bool loop, bool muted)
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            auto e = l.e;
            if (url.rfind("app:///", 0) != 0)
                throw std::runtime_error{"Android asset video requires an app:/// URL"};
            auto path = url.substr(7);
            if (!m_player || m_path != path)
            {
                Release(e, m_player);
                Release(e, m_retriever);
                ResetFrames();
                m_compositorSurface = false;
                auto obj = Open(e, "android/media/MediaPlayer", path);
                e->CallVoidMethod(obj, Method(e, obj, "prepare", "()V"));
                Check(e, "prepare video");
                m_player = e->NewGlobalRef(obj);
                m_path = path;
            }
            e->CallVoidMethod(m_player, Method(e, m_player, "setLooping", "(Z)V"), static_cast<jboolean>(loop));
            e->CallVoidMethod(m_player, Method(e, m_player, "setVolume", "(FF)V"), muted ? 0.f : 1.f, muted ? 0.f : 1.f);
            e->CallVoidMethod(m_player, Method(e, m_player, "start", "()V"));
            Check(e, "start video");
        }
        void SetSurface(void* surface) override
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            if (m_player)
            {
                l.e->CallVoidMethod(m_player, Method(l.e, m_player, "setSurface", "(Landroid/view/Surface;)V"), static_cast<jobject>(surface));
                Check(l.e, "connect video to OpenXR surface");
                ResetFrames();
                m_compositorSurface = surface != nullptr;
            }
        }
        void Pause()
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            if (m_player)
            {
                l.e->CallVoidMethod(m_player, Method(l.e, m_player, "pause", "()V"));
                Check(l.e, "pause");
            }
        }
        int Property(const char* name)
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            if (!m_player)
                return 0;
            auto n = l.e->CallIntMethod(m_player, Method(l.e, m_player, name, "()I"));
            Check(l.e, name);
            return n;
        }
        bool ReadCurrentPixels(const Reader& reader) override
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            if (!m_player || !RequestStreamingFrame(l.e)) return false;
            std::unique_lock frameLock{m_frameMutex};
            if (m_front.empty()) return false; // Use the existing first-frame fallback.
            reader(m_front, m_frontWidth, m_frontHeight);
            return true;
        }
        Napi::Object CopyFrame(Napi::Env env)
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            auto e = l.e;
            if (!m_player)
                throw std::runtime_error{"Video has no decoded frame"};
            if (RequestStreamingFrame(e))
            {
                std::unique_lock frameLock{m_frameMutex};
                if (m_front.empty() && !m_retriever)
                {
                    m_frameReady.wait_for(frameLock, std::chrono::milliseconds{150}, [this] { return !m_front.empty() || m_decodeError; });
                    if (m_decodeError)
                        std::rethrow_exception(m_decodeError);
                }
                if (!m_front.empty())
                {
                    auto result = Napi::Object::New(env);
                    auto bytes = Napi::ArrayBuffer::New(env, m_front.size());
                    std::memcpy(bytes.Data(), m_front.data(), m_front.size());
                    result.Set("width", m_frontWidth);
                    result.Set("height", m_frontHeight);
                    result.Set("data", bytes);
                    return result;
                }
            }
            if (!m_retriever)
                m_retriever = e->NewGlobalRef(Open(e, "android/media/MediaMetadataRetriever", m_path));
            const jlong us = 1000LL * e->CallIntMethod(m_player, Method(e, m_player, "getCurrentPosition", "()I"));
            auto bitmap = e->CallObjectMethod(m_retriever, Method(e, m_retriever, "getFrameAtTime", "(JI)Landroid/graphics/Bitmap;"), us, 3);
            Check(e, "decode current video frame");
            AndroidBitmapInfo info{};
            if (!bitmap || AndroidBitmap_getInfo(e, bitmap, &info))
                throw std::runtime_error{"No decoded video frame at the current playback position"};
            if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
            {
                auto config = e->FindClass("android/graphics/Bitmap$Config");
                auto rgba = e->GetStaticObjectField(config, e->GetStaticFieldID(config, "ARGB_8888", "Landroid/graphics/Bitmap$Config;"));
                auto converted = e->CallObjectMethod(bitmap, Method(e, bitmap, "copy", "(Landroid/graphics/Bitmap$Config;Z)Landroid/graphics/Bitmap;"), rgba, JNI_FALSE);
                Check(e, "convert video frame to RGBA");
                e->CallVoidMethod(bitmap, Method(e, bitmap, "recycle", "()V"));
                bitmap = converted;
                if (!bitmap || AndroidBitmap_getInfo(e, bitmap, &info) || info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
                    throw std::runtime_error{"Could not convert decoded bitmap to RGBA"};
            }
            auto bytes = Napi::ArrayBuffer::New(env, static_cast<size_t>(info.width) * info.height * 4);
            void* pixels{};
            if (AndroidBitmap_lockPixels(e, bitmap, &pixels))
                throw std::runtime_error{"Couldn't access decoded pixels"};
            for (uint32_t y = 0; y < info.height; ++y)
                std::memcpy(static_cast<char*>(bytes.Data()) + size_t(y) * info.width * 4, static_cast<char*>(pixels) + size_t(y) * info.stride, info.width * 4);
            AndroidBitmap_unlockPixels(e, bitmap);
            e->CallVoidMethod(bitmap, Method(e, bitmap, "recycle", "()V"));
            auto result = Napi::Object::New(env);
            result.Set("width", info.width);
            result.Set("height", info.height);
            result.Set("data", bytes);
            return result;
        }
    };

    class AndroidAssetVideo final : public Napi::ObjectWrap<AndroidAssetVideo>
    {
    public:
        std::shared_ptr<AndroidAssetPlayer> Player{std::make_shared<AndroidAssetPlayer>()};
        Graphics::ExternalImageRegistration Registration;
        explicit AndroidAssetVideo(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<AndroidAssetVideo>{info}
            , Registration{static_cast<napi_env>(info.Env()), this, Player}
        {
        }
        static void Initialize(Napi::Env e)
        {
            const auto ctor = DefineClass(e, "HTMLVideoElement", {InstanceMethod("play", &AndroidAssetVideo::Play), InstanceMethod("pause", &AndroidAssetVideo::Pause), InstanceMethod("load", &AndroidAssetVideo::Load), InstanceMethod("removeAttribute", &AndroidAssetVideo::Remove), InstanceMethod("_getNativeImageData", &AndroidAssetVideo::Pixels), InstanceAccessor("currentTime", &AndroidAssetVideo::Time, nullptr), InstanceAccessor("videoWidth", &AndroidAssetVideo::Width, nullptr), InstanceAccessor("videoHeight", &AndroidAssetVideo::Height, nullptr), InstanceAccessor("readyState", &AndroidAssetVideo::Ready, nullptr)});
            SetGlobalClass(e, "NativeAssetVideo", ctor);
            if (e.Global().Get("HTMLVideoElement").IsUndefined())
                e.Global().Set("HTMLVideoElement", ctor);
        }

    private:
        Napi::Value Play(const Napi::CallbackInfo& i)
        {
            auto d = Napi::Promise::Deferred::New(i.Env());
            try
            {
                Player->Play(Value().Get("src").ToString().Utf8Value(), Value().Get("loop").ToBoolean(), Value().Get("muted").ToBoolean());
                d.Resolve(i.Env().Undefined());
            }
            catch (const std::exception& e)
            {
                d.Reject(Napi::Error::New(i.Env(), e.what()).Value());
            }
            return d.Promise();
        }
        void Pause(const Napi::CallbackInfo&) { Player->Pause(); }
        void Remove(const Napi::CallbackInfo& i)
        {
            if (i[0].ToString().Utf8Value() == "src")
                Value().Delete("src");
        }
        void Load(const Napi::CallbackInfo&) { Player->Reset(); }
        Napi::Value Pixels(const Napi::CallbackInfo& i)
        {
            try
            {
                return Player->CopyFrame(i.Env());
            }
            catch (const std::exception& error)
            {
                throw Napi::Error::New(i.Env(), error.what());
            }
        }
        Napi::Value Time(const Napi::CallbackInfo& i) { return Napi::Number::New(i.Env(), Player->Property("getCurrentPosition") / 1000.); }
        Napi::Value Width(const Napi::CallbackInfo& i) { return Napi::Number::New(i.Env(), Player->Property("getVideoWidth")); }
        Napi::Value Height(const Napi::CallbackInfo& i) { return Napi::Number::New(i.Env(), Player->Property("getVideoHeight")); }
        Napi::Value Ready(const Napi::CallbackInfo& i) { return Napi::Number::New(i.Env(), Player->Property("getVideoWidth") > 0 ? 2 : 0); }
    };
}
