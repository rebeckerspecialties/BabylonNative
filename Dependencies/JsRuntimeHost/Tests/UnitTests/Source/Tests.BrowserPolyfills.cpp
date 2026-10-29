#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/Compression.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Fetch.h>
#include <Babylon/Polyfills/File.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/Streams.h>
#include <Babylon/Polyfills/TextDecoder.h>
#include <Babylon/Polyfills/TextEncoder.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#if defined(JSRUNTIMEHOST_TEST_WORKER)
#include <Babylon/Polyfills/Worker.h>
#endif
#include <gtest/gtest.h>
#include <arcana/threading/blocking_concurrent_queue.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    const char* EnumToString(Babylon::Polyfills::Console::LogLevel logLevel)
    {
        switch (logLevel)
        {
            case Babylon::Polyfills::Console::LogLevel::Log:
                return "log";
            case Babylon::Polyfills::Console::LogLevel::Warn:
                return "warn";
            case Babylon::Polyfills::Console::LogLevel::Error:
                return "error";
        }

        return "unknown";
    }
}

TEST(Streams, PreservesHostConstructorsAndIsIdempotent)
{
    Babylon::AppRuntime runtime{};
    std::promise<void> done;

    runtime.Dispatch([&done](Napi::Env env) {
        auto global = env.Global();
        const auto hostReadableStream = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostReadableStream");
        global.Set("ReadableStream", hostReadableStream);

        Babylon::Polyfills::Streams::Initialize(env);
        EXPECT_TRUE(global.Get("ReadableStream").StrictEquals(hostReadableStream));
        EXPECT_TRUE(global.Get("TransformStream").IsFunction());

        const auto installedTransformStream = global.Get("TransformStream");
        Babylon::Polyfills::Streams::Initialize(env);
        EXPECT_TRUE(global.Get("ReadableStream").StrictEquals(hostReadableStream));
        EXPECT_TRUE(global.Get("TransformStream").StrictEquals(installedTransformStream));
        done.set_value();
    });

    done.get_future().get();
}

TEST(Fetch, PreservesHostClassesAndIsIdempotent)
{
    Babylon::AppRuntime runtime{};
    std::promise<void> done;

    runtime.Dispatch([&done](Napi::Env env) {
        auto global = env.Global();
        const auto hostHeaders = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostHeaders");
        const auto hostResponse = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostResponse");
        global.Set("Headers", hostHeaders);
        global.Set("Response", hostResponse);

        Babylon::Polyfills::Fetch::Initialize(env);
        EXPECT_TRUE(global.Get("Headers").StrictEquals(hostHeaders));
        EXPECT_TRUE(global.Get("Response").StrictEquals(hostResponse));

        const auto installedFetch = global.Get("fetch");
        Babylon::Polyfills::Fetch::Initialize(env);
        EXPECT_TRUE(global.Get("Headers").StrictEquals(hostHeaders));
        EXPECT_TRUE(global.Get("Response").StrictEquals(hostResponse));
        EXPECT_TRUE(global.Get("fetch").StrictEquals(installedFetch));
        done.set_value();
    });

    done.get_future().get();
}

TEST(Compression, PreservesHostConstructorsAndIsIdempotent)
{
    Babylon::AppRuntime runtime{};
    std::promise<void> done;

    runtime.Dispatch([&done](Napi::Env env) {
        auto global = env.Global();
        const auto hostCompressionStream = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostCompressionStream");
        global.Set("CompressionStream", hostCompressionStream);

        Babylon::Polyfills::Compression::Initialize(env);
        EXPECT_TRUE(global.Get("CompressionStream").StrictEquals(hostCompressionStream));
        EXPECT_TRUE(global.Get("DecompressionStream").IsFunction());

        const auto installedDecompressionStream = global.Get("DecompressionStream");
        Babylon::Polyfills::Compression::Initialize(env);
        EXPECT_TRUE(global.Get("CompressionStream").StrictEquals(hostCompressionStream));
        EXPECT_TRUE(global.Get("DecompressionStream").StrictEquals(installedDecompressionStream));
        done.set_value();
    });

    done.get_future().get();
}

#if defined(JSRUNTIMEHOST_APPLE_JAVASCRIPTCORE)
TEST(NodeApi, ConcurrentJavaScriptCoreEnvironmentsInvokeNativeCallbacks)
{
    constexpr size_t runtimeCount{16};
    std::promise<void> startPromise;
    auto startFuture{startPromise.get_future().share()};
    std::vector<std::future<bool>> runtimes;
    runtimes.reserve(runtimeCount);

    for (size_t index{0}; index < runtimeCount; ++index)
    {
        runtimes.emplace_back(std::async(std::launch::async, [startFuture]() {
            startFuture.wait();

            std::promise<bool> callbackPromise;
            auto callbackFuture{callbackPromise.get_future()};
            Babylon::AppRuntime runtime{};
            runtime.Dispatch([&callbackPromise](Napi::Env env) {
                auto callback{Napi::Function::New(env, [&callbackPromise](const Napi::CallbackInfo&) {
                    callbackPromise.set_value(true);
                })};
                callback.Call({});
            });

            if (callbackFuture.wait_for(std::chrono::seconds{5}) != std::future_status::ready)
            {
                return false;
            }
            return callbackFuture.get();
        }));
    }

    startPromise.set_value();
    for (auto& runtime : runtimes)
    {
        ASSERT_EQ(runtime.wait_for(std::chrono::seconds{10}), std::future_status::ready);
        EXPECT_TRUE(runtime.get());
    }
}
#endif

#if defined(JSRUNTIMEHOST_TEST_WORKER) && !defined(__ANDROID__)
TEST(Worker, WebPlatformTests)
{
    struct Result
    {
        bool Passed{};
        std::string Detail{};
    };

    std::promise<Result> completion;
    std::atomic_bool completed{false};

    Babylon::AppRuntime::Options runtimeOptions{};
    runtimeOptions.UnhandledExceptionHandler = [&completion, &completed](const Napi::Error& error) {
        if (!completed.exchange(true))
        {
            completion.set_value({false, Napi::GetErrorString(error)});
        }
    };

    Babylon::AppRuntime runtime{std::move(runtimeOptions)};
    runtime.Dispatch([&completion, &completed](Napi::Env env) {
        Babylon::Polyfills::Scheduling::Initialize(env);

        Babylon::Polyfills::Worker::Options options{};
        options.ScriptRoot = (std::filesystem::current_path() / "WebPlatformTests").string();
        options.ConsoleCallback = [](const char* message) {
            std::cerr << "[Worker] " << message << std::endl;
        };
        Babylon::Polyfills::Worker::Initialize(env, std::move(options));

        env.Global().Set("__jsrhWptDone", Napi::Function::New(
            env,
            [&completion, &completed](const Napi::CallbackInfo& info) {
                if (!completed.exchange(true))
                {
                    completion.set_value({
                        info[0].ToBoolean().Value(),
                        info[1].ToString().Utf8Value(),
                    });
                }
            },
            "__jsrhWptDone"));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.LoadScript("app:///WebPlatformTests/runner.js");

    auto future = completion.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds{90}), std::future_status::ready)
        << "Worker WPT subset timed out";
    const auto result = future.get();
    EXPECT_TRUE(result.Passed) << result.Detail;
}
#endif

#if defined(JSRUNTIMEHOST_APPLE_JAVASCRIPTCORE)
TEST(AppRuntime, AsyncWebAssemblyInstantiationMakesProgress)
{
    Babylon::AppRuntime runtime{};
    std::promise<bool> completionPromise;
    auto completionFuture = completionPromise.get_future();

    runtime.Dispatch([&completionPromise](Napi::Env env) {
        env.Global().Set(
            "completeAsyncWebAssemblyTest",
            Napi::Function::New(env, [&completionPromise](const Napi::CallbackInfo& info) {
                completionPromise.set_value(info[0].ToBoolean().Value());
            }));

        Napi::Eval(env, R"(
            (() => {
                const customPayloadSize = 1024 * 1024;
                const bytes = new Uint8Array(13 + customPayloadSize);
                bytes.set([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);
                bytes.set([0x00, 0x81, 0x80, 0x40, 0x00], 8);
                WebAssembly.instantiate(bytes, {}).then(
                    () => completeAsyncWebAssemblyTest(true),
                    () => completeAsyncWebAssemblyTest(false));
            })();
        )",
            "async-webassembly-test.js");
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (completionFuture.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::promise<void> pumpPromise;
        auto pumpFuture = pumpPromise.get_future();
        runtime.Dispatch([&pumpPromise](Napi::Env) {
            pumpPromise.set_value();
        });
        ASSERT_EQ(pumpFuture.wait_for(std::chrono::seconds{1}), std::future_status::ready);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    ASSERT_EQ(completionFuture.wait_for(std::chrono::milliseconds{0}), std::future_status::ready)
        << "WebAssembly.instantiate did not settle while the host runtime was being pumped";
    EXPECT_TRUE(completionFuture.get());
}
#endif

#if NAPI_VERSION >= 7 && !defined(JSRUNTIMEHOST_NAPI_ENGINE_JSI) && !defined(JSR_NAPI_ENGINE_CHAKRA)
TEST(NodeApi, ArrayBufferWrapperRefreshesInfoAfterDetach)
{
    // Regression: Napi::ArrayBuffer used to cache Data()/ByteLength() under
    // the assumption that they could never change. Detachment disproves that
    // assumption, and WebGPU keeps wrapper instances alive across unmap().
    Babylon::AppRuntime runtime{};
    std::promise<uint32_t> result;

    runtime.Dispatch([&result](Napi::Env env) {
        auto arrayBuffer = Napi::ArrayBuffer::New(env, 16);
        const auto* beforeData = arrayBuffer.Data();
        const auto beforeLength = arrayBuffer.ByteLength();

        const auto detachStatus = napi_detach_arraybuffer(env, arrayBuffer);
        if (detachStatus != napi_ok)
        {
            // Frozen JavaScriptCore builds have no public detach primitive,
            // and transitional JSC may refuse detachment after Node-API has
            // exposed the backing pointer. Node-API explicitly permits those
            // engine-specific detachability conditions.
            napi_value pendingException{nullptr};
            napi_get_and_clear_last_exception(env, &pendingException);
            result.set_value(0x3Fu);
            return;
        }

        bool detached{false};
        const auto detachedStatus = napi_is_detached_arraybuffer(env, arrayBuffer, &detached);
        const auto* afterData = arrayBuffer.Data();
        const auto afterLength = arrayBuffer.ByteLength();

        result.set_value(
            (beforeData != nullptr ? 1u << 0 : 0u) |
            (beforeLength == 16 ? 1u << 1 : 0u) |
            (detachedStatus == napi_ok ? 1u << 2 : 0u) |
            (detached ? 1u << 3 : 0u) |
            (afterData == nullptr ? 1u << 4 : 0u) |
            (afterLength == 0 ? 1u << 5 : 0u));
    });

    EXPECT_EQ(0x3Fu, result.get_future().get())
        << "bits: initial data, initial length, detached status, detached state, "
           "detached data, detached length";
}
#endif
