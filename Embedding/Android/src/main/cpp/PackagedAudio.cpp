#include "PackagedAudio.h"
#include <AndroidExtensions/Globals.h>
#include <Babylon/Embedding/Runtime.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace Babylon::Embedding
{
    namespace
    {
        JNIEnv* Env() { return android::global::GetEnvForCurrentThread(); }

        struct Factory
        {
            jclass type{};
            jmethodID ctor{}, playing{}, state{}, error{}, close{};
            ~Factory() { if (type) Env()->DeleteGlobalRef(type); }
        };
        struct Player
        {
            std::shared_ptr<Factory> factory;
            jobject object{};
            void Close()
            {
                if (!object) return;
                auto* env = Env();
                env->CallVoidMethod(object, factory->close);
                env->DeleteGlobalRef(object);
                object = nullptr;
            }
            ~Player() { Close(); }
        };

        // The factory and decoder handles stay private to the media element.
        constexpr auto Script = R"JS((open => {
            if (typeof globalThis.Audio === "function" || typeof globalThis.EventTarget !== "function") return;
            class Audio extends EventTarget {
                constructor(src) {
                    super();
                    this.paused = true; this.ended = false; this._pending = [];
                    this.src = src || "";
                }
                get src() { return this._src; }
                set src(value) {
                    this.pause(); this._release(); this.ended = false; this.error = null;
                    const base = globalThis.document?.baseURI || globalThis.location?.href || "app:///";
                    this._src = value ? new URL(String(value), base).href : "";
                }
                _emit(type) {
                    const event = new Event(type);
                    this.dispatchEvent(event);
                    this["on" + type]?.call(this, event);
                }
                _release() {
                    clearTimeout(this._timer); this._timer = undefined;
                    this._player?.close(); this._player = undefined;
                }
                _settle(reason) {
                    for (const [resolve,reject] of this._pending.splice(0)) reason ? reject(reason) : resolve();
                }
                _tick() {
                    if (!this._player || this.paused) return;
                    const state = this._player.state();
                    if (state < 0) {
                        const error = new Error(this._player.error()); error.name = "NotSupportedError";
                        this.error = error; this.paused = true; this._release(); this._settle(error); this._emit("error");
                    } else if (state === 3) {
                        this.ended = true; this.paused = true; this._release(); this._settle(); this._emit("ended");
                    } else {
                        if (state === 1 && this._pending.length) { this._settle(); this._emit("playing"); }
                        if (!this.paused) this._timer = setTimeout(() => this._tick(), state === 1 ? 200 : 25);
                    }
                }
                async play() {
                    if (!this.src) throw new Error("Audio.play requires a media URL");
                    if (!this._player) this._player = open(this.src);
                    const wasPaused = this.paused;
                    this.ended = false; this.paused = false;
                    const promise = new Promise((resolve,reject) => this._pending.push([resolve,reject]));
                    this._player.playing(true);
                    clearTimeout(this._timer); this._timer = setTimeout(() => this._tick(), 0);
                    if (wasPaused) this._emit("play");
                    return promise;
                }
                pause() {
                    if (this.paused) return;
                    this.paused = true; this._player?.playing(false); clearTimeout(this._timer);
                    const error = new Error("Playback was interrupted"); error.name = "AbortError";
                    this._settle(error); this._emit("pause");
                }
            }
            globalThis.Audio = Audio;
        }))JS";
    }

    bool PackagedAudioEnabled(JNIEnv* env, jobject context)
    {
        if (!context) return false;
        auto local = env->FindClass("com/babylonjs/embedding/PackagedAudio");
        if (!local) { env->ExceptionClear(); return false; }
        const auto enabled = env->GetStaticMethodID(local, "enabled", "(Landroid/content/Context;)Z");
        const bool result = enabled && env->CallStaticBooleanMethod(local, enabled, context);
        env->DeleteLocalRef(local);
        return result;
    }

    void SetPackagedAudioForeground(JNIEnv* env, bool foreground)
    {
        auto type = env->FindClass("com/babylonjs/embedding/PackagedAudio");
        if (type)
        {
            auto method = env->GetStaticMethodID(type, "setForeground", "(Z)V");
            if (method) env->CallStaticVoidMethod(type, method, static_cast<jboolean>(foreground));
            env->DeleteLocalRef(type);
        }
    }

    void InstallPackagedAudio(JNIEnv* env, Runtime& runtime)
    {
        // Resolve the companion with the host's Java class loader, not the JS thread.
        auto local = env->FindClass("com/babylonjs/embedding/PackagedAudio");
        if (!local) return;
        auto factory = std::make_shared<Factory>();
        factory->type = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        if (!factory->type) return;
        factory->ctor = env->GetMethodID(factory->type, "<init>", "(Landroid/content/Context;Ljava/lang/String;)V");
        factory->playing = env->GetMethodID(factory->type, "setPlaying", "(Z)V");
        factory->state = env->GetMethodID(factory->type, "state", "()I");
        factory->error = env->GetMethodID(factory->type, "error", "()Ljava/lang/String;");
        factory->close = env->GetMethodID(factory->type, "close", "()V");
        if (env->ExceptionCheck()) return;
        runtime.RunOnJsThread([factory](Napi::Env js) {
            auto open = Napi::Function::New(js, [factory](const Napi::CallbackInfo& info) {
                auto player = std::make_shared<Player>();
                player->factory = factory;
                auto* jni = Env();
                const auto source = info[0].As<Napi::String>().Utf16Value();
                auto url = jni->NewString(reinterpret_cast<const jchar*>(source.data()), static_cast<jsize>(source.size()));
                auto object = url ? jni->NewObject(factory->type, factory->ctor, static_cast<jobject>(android::global::GetAppContext()), url) : nullptr;
                if (url) jni->DeleteLocalRef(url);
                if (object) { player->object = jni->NewGlobalRef(object); jni->DeleteLocalRef(object); }
                if (!player->object) {
                    if (jni->ExceptionCheck()) jni->ExceptionClear();
                    throw std::runtime_error{"Unable to open Android media player"};
                }
                auto api = Napi::Object::New(info.Env());
                api.Set("playing", Napi::Function::New(info.Env(), [player](const Napi::CallbackInfo& args) {
                    if (player->object) Env()->CallVoidMethod(player->object, player->factory->playing,
                        static_cast<jboolean>(args[0].As<Napi::Boolean>().Value()));
                }));
                api.Set("state", Napi::Function::New(info.Env(), [player](const Napi::CallbackInfo& args) {
                    return Napi::Number::New(args.Env(), player->object ? Env()->CallIntMethod(player->object, player->factory->state) : -1);
                }));
                api.Set("error", Napi::Function::New(info.Env(), [player](const Napi::CallbackInfo& args) {
                    if (!player->object) return Napi::String::New(args.Env(), "Player is closed");
                    auto* env = Env();
                    auto message = static_cast<jstring>(env->CallObjectMethod(player->object, player->factory->error));
                    const char* value = message ? env->GetStringUTFChars(message, nullptr) : nullptr;
                    const std::string copy{value ? value : "Android decoder failed"};
                    if (value) env->ReleaseStringUTFChars(message, value);
                    if (message) env->DeleteLocalRef(message);
                    return Napi::String::New(args.Env(), copy);
                }));
                api.Set("close", Napi::Function::New(info.Env(), [player](const Napi::CallbackInfo&) { player->Close(); }));
                return api;
            });
            Napi::Eval(js, Script, "babylonnative://packaged-audio.js").As<Napi::Function>().Call({open});
        });
    }
}
