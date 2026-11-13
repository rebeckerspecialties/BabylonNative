#include <Babylon/AppRuntime.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/Compression.h>
#include <Babylon/Polyfills/File.h>
#include <gtest/gtest.h>

#if defined(JSRUNTIMEHOST_APPLE_JAVASCRIPTCORE)
#include <napi/env.h>
#include <JavaScriptCore/JavaScript.h>
#endif

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    struct Completion
    {
        std::promise<void> promise;
        std::atomic<bool> completed{false};

        void Finish(std::exception_ptr error = {})
        {
            if (!completed.exchange(true))
            {
                if (error)
                {
                    promise.set_exception(error);
                }
                else
                {
                    promise.set_value();
                }
            }
        }
    };

    void RunLifetimeScript(const char* script)
    {
        auto completion = std::make_shared<Completion>();
        auto future = completion->promise.get_future();
        Babylon::AppRuntime::Options options;
        options.UnhandledExceptionHandler = [completion](const Napi::Error& error) {
            completion->Finish(std::make_exception_ptr(std::runtime_error{Napi::GetErrorString(error)}));
        };
        Babylon::AppRuntime runtime{options};
        runtime.Dispatch([completion, script](Napi::Env env) {
            try
            {
                Babylon::Polyfills::Blob::Initialize(env);
                Babylon::Polyfills::File::Initialize(env);
                Babylon::Polyfills::Compression::Initialize(env);
                env.Global().Set("__lifetimeDone", Napi::Function::New(env, [completion](const Napi::CallbackInfo&) {
                    completion->Finish();
                }));
                env.Global().Set("__lifetimeFailed", Napi::Function::New(env, [completion](const Napi::CallbackInfo& info) {
                    completion->Finish(std::make_exception_ptr(std::runtime_error{info[0].ToString().Utf8Value()}));
                }));
                env.Global().Set("__collectGarbage", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
#if defined(JSRUNTIMEHOST_APPLE_JAVASCRIPTCORE)
                    JSGarbageCollect(Napi::GetContext(info.Env()));
#else
                    static_cast<void>(info);
#endif
                }));

                const std::string source = std::string{R"JS(
                    (async function () {
                        function check(condition, message) {
                            if (!condition) { throw new Error(message); }
                        }
                        function equal(actual, expected) {
                            check(actual.length === expected.length, "length: " + actual.length + " != " + expected.length);
                            for (let i = 0; i < expected.length; ++i) {
                                if (actual[i] !== expected[i]) {
                                    throw new Error("value at " + i + ": " + actual[i] + " != " + expected[i]);
                                }
                            }
                        }
                        function collectWithPressure() {
                            const pressure = [];
                            for (let i = 0; i < 64; ++i) { pressure.push(new Uint8Array(16 * 1024)); }
                            __collectGarbage();
                            return pressure.length;
                        }
                        async function concatenate(readable) {
                            const chunks = [];
                            let size = 0;
                            const reader = readable.getReader();
                            while (true) {
                                const result = await reader.read();
                                if (result.done) { break; }
                                check(result.value instanceof Uint8Array, "output must be Uint8Array");
                                chunks.push(result.value);
                                size += result.value.length;
                            }
                            const output = new Uint8Array(size);
                            let offset = 0;
                            for (const chunk of chunks) { output.set(chunk, offset); offset += chunk.length; }
                            return output;
                        }
                        async function transform(stream, bytes) {
                            const output = concatenate(stream.readable);
                            const writer = stream.writable.getWriter();
                            await writer.write(bytes);
                            await writer.close();
                            return output;
                        }
                )JS"} + script + R"JS(
                    })().then(__lifetimeDone, error => __lifetimeFailed(String(error) + "\n" + (error && error.stack || "")));
                )JS";
                Napi::Eval(env, source.c_str(), "compression-filereader-lifetime.js");
            }
            catch (const Napi::Error& error)
            {
                completion->Finish(std::make_exception_ptr(std::runtime_error{Napi::GetErrorString(error)}));
            }
            catch (...)
            {
                completion->Finish(std::current_exception());
            }
        });
        if (future.wait_for(std::chrono::seconds{60}) != std::future_status::ready)
        {
            runtime.Terminate();
            throw std::runtime_error{"Compression/FileReader lifetime test timed out"};
        }
        future.get();
    }
}

TEST(CompressionLifetime, PendingOutputSurvivesCollectionAndInputMutation)
{
    RunLifetimeScript(R"JS(
        for (const format of ["deflate", "deflate-raw", "gzip"]) {
            const input = new Uint8Array(256 * 1024);
            let state = 0x12345678;
            for (let i = 0; i < input.length; ++i) {
                state ^= state << 13;
                state ^= state >>> 17;
                state ^= state << 5;
                input[i] = state & 0xff;
            }
            const expected = input.slice();
            const prototype = TransformStreamDefaultController.prototype;
            const original = prototype.enqueue;
            let chunkCount = 0;
            let compressed;
            prototype.enqueue = function (chunk) {
                ++chunkCount;
                input.fill(0);
                collectWithPressure();
                return original.call(this, chunk);
            };
            try {
                compressed = await transform(new CompressionStream(format), input);
            } finally {
                prototype.enqueue = original;
            }
            check(chunkCount > 1, "fixture must produce multiple output chunks");
            const output = await transform(new DecompressionStream(format), compressed);
            equal(output, expected);
        }
    )JS");
}

TEST(CompressionLifetime, EmitsOutputBeforeTrailingInputError)
{
    RunLifetimeScript(R"JS(
        const expected = new Uint8Array([65, 66, 67, 68, 69, 70]);
        for (const format of ["deflate", "deflate-raw", "gzip"]) {
            const compressed = await transform(new CompressionStream(format), expected);
            const trailing = new Uint8Array(compressed.length + 1);
            trailing.set(compressed);
            const stream = new DecompressionStream(format);
            const reader = stream.readable.getReader();
            const writer = stream.writable.getWriter();
            const firstRead = reader.read();
            const write = writer.write(trailing).catch(error => error);
            const first = await firstRead;
            check(!first.done, "valid output must precede trailing-input error");
            equal(first.value, expected);
            check(await reader.read().catch(error => error) instanceof TypeError, "read must reject with TypeError");
            check(await write instanceof TypeError, "write must reject with TypeError");
        }
    )JS");
}

TEST(CompressionLifetime, InvalidAndTruncatedInputRejectBothSides)
{
    RunLifetimeScript(R"JS(
        for (const format of ["deflate", "deflate-raw", "gzip"]) {
            const compressed = await transform(new CompressionStream(format), new Uint8Array([65, 66, 67, 68]));
            for (const input of [new Uint8Array([255, 255, 255, 255]), compressed.slice(0, -1)]) {
                const stream = new DecompressionStream(format);
                const output = concatenate(stream.readable).catch(error => error);
                const writer = stream.writable.getWriter();
                await writer.write(input).catch(() => undefined);
                const close = await writer.close().catch(error => error);
                check(close instanceof TypeError, "close must reject with TypeError");
                check(await output instanceof TypeError, "output must reject with TypeError");
            }
        }
    )JS");
}

TEST(FileReaderLifetime, RemovedListenerIsSkippedDuringLoadAndCollection)
{
    RunLifetimeScript(R"JS(
        const reader = new FileReader();
        const calls = [];
        let later = () => calls.push("removed");
        reader.addEventListener("load", function () {
            calls.push("first");
            reader.removeEventListener("load", later);
            later = null;
            calls.push("allocated " + collectWithPressure());
        });
        reader.addEventListener("load", later);
        reader.addEventListener("load", () => calls.push("last"));
        await new Promise((resolve, reject) => {
            reader.onloadend = resolve;
            reader.onerror = reject;
            reader.readAsText(new Blob(["abc"]));
        });
        equal(calls, ["first", "allocated 64", "last"]);
        check(reader.result === "abc", "read result must survive listener mutation");
    )JS");
}

TEST(FileReaderLifetime, RemoveAndReaddCreatesNewRegistration)
{
    RunLifetimeScript(R"JS(
        const reader = new FileReader();
        const calls = [];
        function later() { calls.push("later"); }
        function first() {
            calls.push("first");
            reader.removeEventListener("load", first);
            reader.removeEventListener("load", later);
            reader.addEventListener("load", later);
        }
        reader.addEventListener("load", first);
        reader.addEventListener("load", later);
        reader.addEventListener("load", () => calls.push("last"));
        reader.dispatchEvent({type: "load"});
        equal(calls, ["first", "last"]);
        calls.length = 0;
        reader.dispatchEvent({type: "load"});
        equal(calls, ["last", "later"]);
    )JS");
}

TEST(FileReaderLifetime, SelfRemovalDefersAddedListener)
{
    RunLifetimeScript(R"JS(
        const reader = new FileReader();
        const calls = [];
        function added() { calls.push("added"); }
        function first() {
            calls.push("first");
            reader.removeEventListener("load", first);
            reader.addEventListener("load", added);
            collectWithPressure();
        }
        reader.addEventListener("load", first);
        reader.addEventListener("load", () => calls.push("last"));
        reader.dispatchEvent({type: "load"});
        equal(calls, ["first", "last"]);
        calls.length = 0;
        reader.dispatchEvent({type: "load"});
        equal(calls, ["last", "added"]);
    )JS");
}

TEST(FileReaderLifetime, NestedDispatchRemovalIsVisibleToOuterSnapshot)
{
    RunLifetimeScript(R"JS(
        const reader = new FileReader();
        const calls = [];
        function first() {
            calls.push("first");
            reader.removeEventListener("load", first);
            reader.dispatchEvent({type: "load"});
        }
        function middle() {
            calls.push("middle");
            reader.removeEventListener("load", middle);
            collectWithPressure();
        }
        reader.addEventListener("load", first);
        reader.addEventListener("load", middle);
        reader.addEventListener("load", () => calls.push("last"));
        reader.dispatchEvent({type: "load"});
        equal(calls, ["first", "middle", "last", "last"]);
        calls.length = 0;
        reader.dispatchEvent({type: "load"});
        equal(calls, ["last"]);
    )JS");
}
