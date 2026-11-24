#include "AppRuntime.h"
#include "V8Platform.h"
#include <napi/env.h>

#include <libplatform/libplatform.h>

#ifdef ENABLE_V8_INSPECTOR
#include <V8InspectorAgent.h>
#endif

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#include <chrono>
#include <string_view>
#endif

#ifdef JSRUNTIMEHOST_EMBED_V8_SNAPSHOT
#include "AndroidV8Snapshot.h"
#endif

namespace Babylon
{
    namespace
    {
        class Module final
        {
        public:
            Module(const char* executablePath)
            {
                v8::V8::InitializeICUDefaultLocation(executablePath);
#ifdef JSRUNTIMEHOST_EMBED_V8_SNAPSHOT
                // APK assets have no filesystem path. V8 borrows this data for the process lifetime.
                static v8::StartupData snapshot{reinterpret_cast<const char*>(AndroidV8Snapshot), sizeof(AndroidV8Snapshot)};
                v8::V8::SetSnapshotDataBlob(&snapshot);
#else
                v8::V8::InitializeExternalStartupData(executablePath);
#endif
                m_platform = std::make_unique<Internal::V8Platform>(v8::platform::NewDefaultPlatform());
                v8::V8::InitializePlatform(m_platform.get());
                v8::V8::Initialize();
            }

            ~Module()
            {
                v8::V8::Dispose();
                v8::V8::DisposePlatform();
            }

            static void Initialize(const char* executablePath)
            {
                if (s_module == nullptr)
                {
                    s_module = std::make_unique<Module>(executablePath);
                }
            }

            static Module& Instance()
            {
                if (!s_module)
                {
                    throw std::runtime_error{"Module not available"};
                }

                return *s_module;
            }

            Internal::V8Platform& Platform()
            {
                return *m_platform;
            }

        private:
            std::unique_ptr<Internal::V8Platform> m_platform;

            static std::unique_ptr<Module> s_module;
        };

        std::unique_ptr<Module> Module::s_module;
    }

    void AppRuntime::RunEnvironmentTier(const char* executablePath)
    {
        Module::Initialize(executablePath);
        auto& platform = Module::Instance().Platform();

        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        v8::Isolate* isolate = v8::Isolate::Allocate();
        // Initialization can post foreground work, so register before it starts.
        platform.RegisterHost(isolate, *this, GetDelayedTaskScheduler());
        v8::Isolate::Initialize(isolate, create_params);

#ifdef __ANDROID__
        // Read once per isolate. Ordinary runs install no callbacks.
        char traceGc[PROP_VALUE_MAX]{};
        if (__system_property_get("debug.babylon.gc", traceGc) && std::string_view{traceGc} == "1")
        {
            static thread_local std::chrono::steady_clock::time_point gcStart;
            isolate->AddGCPrologueCallback([](v8::Isolate*, v8::GCType, v8::GCCallbackFlags) {
                gcStart = std::chrono::steady_clock::now();
            });
            isolate->AddGCEpilogueCallback([](v8::Isolate*, v8::GCType type, v8::GCCallbackFlags) {
                const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - gcStart).count();
                __android_log_print(ANDROID_LOG_INFO, "BabylonGC", "type=%d us=%lld", int(type), static_cast<long long>(us));
            });
        }
#endif
        {
            v8::Isolate::Scope isolate_scope{isolate};
            v8::HandleScope isolate_handle_scope{isolate};
            v8::Local<v8::Context> context = v8::Context::New(isolate);
            v8::Context::Scope context_scope{context};

            Napi::Env env = Napi::Attach(context, [this](std::function<void()> task) {
                Dispatch([task = std::move(task)](Napi::Env) {
                    task();
                });
            });

#ifdef ENABLE_V8_INSPECTOR
            std::optional<V8InspectorAgent> agent;
            if (m_options.EnableDebugger)
            {
                agent.emplace(platform, isolate, context, "JsRuntimeHost");
                agent->Start(5643, "JsRuntimeHost");

                if (m_options.WaitForDebugger)
                {
                    agent->WaitForDebugger();
                }
            }
#endif

            Run(env);

#ifdef ENABLE_V8_INSPECTOR
            if (agent.has_value())
            {
                agent->Stop();
            }
#endif

            Napi::Detach(env);
        }

        // todo : GetArrayBufferAllocator not available?
        // delete isolate->GetArrayBufferAllocator();
        isolate->Dispose();
    }

    void AppRuntime::ShutdownEnvironment(Napi::Env)
    {
        Module::Instance().Platform().UnregisterHost(v8::Isolate::GetCurrent());
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // V8 auto-drains microtasks. Foreground tasks run on AppRuntime's dispatcher.
    }
}
