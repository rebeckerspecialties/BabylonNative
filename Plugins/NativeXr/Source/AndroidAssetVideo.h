#pragma once

#include <AndroidExtensions/Globals.h>
#include <android/bitmap.h>
#include <mutex>

namespace Babylon
{
    // Java framework media services, not a bundled codec. AssetFileDescriptor
    // includes the offset and length within the uncompressed APK entry.
    class AndroidAssetPlayer final : public xr::MediaSurfaceProducer
    {
        std::mutex m_mutex;
        jobject m_player{}, m_retriever{};
        std::string m_path;
        struct Locals
        {
            JNIEnv* e{android::global::GetEnvForCurrentThread()};
            Locals()
            {
                if (e->PushLocalFrame(32))
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

    public:
        ~AndroidAssetPlayer() { Reset(); }
        void Reset()
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            Release(l.e, m_retriever);
            Release(l.e, m_player);
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
        Napi::Object CopyFrame(Napi::Env env)
        {
            std::scoped_lock lock{m_mutex};
            Locals l;
            auto e = l.e;
            if (!m_player)
                throw std::runtime_error{"Video has no decoded frame"};
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
        explicit AndroidAssetVideo(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<AndroidAssetVideo>{info}
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
