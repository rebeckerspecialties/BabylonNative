#include <Babylon/AppRuntime.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/BlobInternal.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/Streams.h>
#include <Babylon/Polyfills/URL.h>
#include <UrlLib/UrlLib.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(JSRUNTIMEHOST_APPLE_JAVASCRIPTCORE)
#include <napi/env.h>
#include <JavaScriptCore/JavaScript.h>
#include <dlfcn.h>
#endif

namespace
{
    using Bytes = std::shared_ptr<const std::vector<std::byte>>;

    Bytes GetBytes(const Napi::Value& value)
    {
        const auto data = Babylon::Polyfills::Blob::TryGetData(value.As<Napi::Object>());
        if (!data)
        {
            throw std::runtime_error{"Expected a native Blob"};
        }
        return data->Data;
    }

    std::string Text(const Bytes& bytes)
    {
        return bytes->empty() ? std::string{} : std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    }

    void RunBlobTest(std::function<Napi::Value(Napi::Env)> run)
    {
        struct Completion
        {
            std::atomic_bool Settled{};
            std::promise<std::string> Result;

            void Finish(std::string error = {})
            {
                if (!Settled.exchange(true))
                {
                    Result.set_value(std::move(error));
                }
            }
        };
        auto completion = std::make_shared<Completion>();
        auto result = completion->Result.get_future();
        Babylon::AppRuntime::Options options{};
        options.UnhandledExceptionHandler = [completion](const Napi::Error& error) {
            completion->Finish(Napi::GetErrorString(error));
        };
        Babylon::AppRuntime runtime{options};
        runtime.Dispatch([completion, run = std::move(run)](Napi::Env env) {
            try
            {
                Babylon::Polyfills::Scheduling::Initialize(env);
                Babylon::Polyfills::Streams::Initialize(env);
                Babylon::Polyfills::Blob::Initialize(env);
                Babylon::Polyfills::URL::Initialize(env);
                auto value = run(env);
                auto promiseConstructor = env.Global().Get("Promise").As<Napi::Object>();
                auto promise = promiseConstructor.Get("resolve").As<Napi::Function>().Call(promiseConstructor, {value}).As<Napi::Object>();
                auto success = Napi::Function::New(env, [completion](const Napi::CallbackInfo&) {
                    completion->Finish();
                });
                auto failure = Napi::Function::New(env, [completion](const Napi::CallbackInfo& info) {
                    completion->Finish(info[0].ToString().Utf8Value());
                });
                promise.Get("then").As<Napi::Function>().Call(promise, {success, failure});
            }
            catch (const Napi::Error& error)
            {
                completion->Finish(Napi::GetErrorString(error));
            }
            catch (const std::exception& error)
            {
                completion->Finish(error.what());
            }
        });
        if (result.wait_for(std::chrono::seconds{30}) != std::future_status::ready)
        {
            runtime.Terminate();
            FAIL() << "Blob test did not settle within 30 seconds";
        }
        EXPECT_EQ(result.get(), "");
    }

    Napi::Value RunScript(Napi::Env env, const char* body)
    {
        const std::string script = std::string{"(async function () {\nfunction check(value, message) { if (!value) throw new Error(message); }\n"} + body + "\n})()";
        return Napi::Eval(env, script.c_str(), "blob-storage-regression.js");
    }
}

TEST(BlobStorage, SharesWholeSegmentsAndCachesZeroOneManyParts)
{
    RunBlobTest([](Napi::Env env) {
        const auto blobs = Napi::Eval(env, R"JS(
            (function () {
                var base = new Blob(["abcdef"]);
                return { base: base, whole: base.slice(), nested: new Blob(["", base, ""]),
                    empty: new Blob(), emptyParts: new Blob(["", new Blob(), ""]),
                    prefix: base.slice(0, 3), offset: base.slice(1, 5),
                    parts: new Blob(["ab", new Blob(["cd"]), "ef"]),
                    cross: new Blob(["ab", "cd", "ef"]).slice(1, 5) };
            })()
        )JS", "blob-storage.js").As<Napi::Object>();
        const auto base = GetBytes(blobs.Get("base"));
        EXPECT_EQ(base, GetBytes(blobs.Get("whole")));
        EXPECT_EQ(base, GetBytes(blobs.Get("nested")));
        EXPECT_NE(base, GetBytes(blobs.Get("prefix")));
        EXPECT_NE(base, GetBytes(blobs.Get("offset")));
        for (const auto* name : {"base", "whole", "nested", "empty", "emptyParts", "prefix", "offset", "parts", "cross"})
        {
            EXPECT_EQ(GetBytes(blobs.Get(name)), GetBytes(blobs.Get(name))) << name;
        }
        EXPECT_EQ(Text(GetBytes(blobs.Get("empty"))), "");
        EXPECT_EQ(Text(GetBytes(blobs.Get("emptyParts"))), "");
        EXPECT_EQ(Text(GetBytes(blobs.Get("prefix"))), "abc");
        EXPECT_EQ(Text(GetBytes(blobs.Get("offset"))), "bcde");
        EXPECT_EQ(Text(GetBytes(blobs.Get("parts"))), "abcdef");
        EXPECT_EQ(Text(GetBytes(blobs.Get("cross"))), "bcde");
        return env.Undefined();
    });
}

TEST(BlobStorage, ObjectUrlsShareBytesAndHonorRevocationTiming)
{
    RunBlobTest([](Napi::Env env) {
        auto urlClass = env.Global().Get("URL").As<Napi::Object>();
        auto create = urlClass.Get("createObjectURL").As<Napi::Function>();
        auto revoke = urlClass.Get("revokeObjectURL").As<Napi::Function>();
        for (const auto* expression : {"new Blob()", "new Blob(['abcdef'])", "new Blob(['abc', 'def'])", "new Blob(['abcdef']).slice(1, 5)"})
        {
            auto blob = Napi::Eval(env, expression, "blob-url-storage.js");
            const auto bytes = GetBytes(blob);
            const auto url = create.Call(urlClass, {blob}).ToString();
            UrlLib::UrlRequest resolved;
            resolved.ResponseType(UrlLib::UrlResponseType::Buffer);
            resolved.Open(UrlLib::UrlMethod::Get, url.Utf8Value());
            resolved.SendAsync();
            EXPECT_EQ(resolved.StatusCode(), UrlLib::UrlStatusCode::Ok);
            EXPECT_EQ(resolved.ResponseBuffer().size(), bytes->size());
            EXPECT_EQ(resolved.ResponseBuffer().data(), bytes->data());

            UrlLib::UrlRequest pending;
            pending.Open(UrlLib::UrlMethod::Get, url.Utf8Value());
            revoke.Call(urlClass, {url});
            pending.SendAsync();
            EXPECT_EQ(pending.StatusCode(), UrlLib::UrlStatusCode::None);
            EXPECT_EQ(pending.ErrorSymbol(), "SchemeResolverNotFound");
            EXPECT_EQ(resolved.ResponseBuffer().data(), bytes->data());
            EXPECT_EQ(resolved.ResponseBuffer().size(), bytes->size());
        }
        return env.Undefined();
    });
}

TEST(BlobStorage, ReadersDoNotMutateSharedSnapshots)
{
    RunBlobTest([](Napi::Env env) {
        return RunScript(env, R"JS(
            var input = new Uint8Array([65, 66, 67, 68]);
            var blob = new Blob([input]);
            var nested = new Blob([blob]);
            var slice = blob.slice(1, 3);
            input.fill(0);
            new Uint8Array(await blob.arrayBuffer()).fill(0);
            (await blob.bytes()).fill(0);
            var reader = blob.stream().getReader();
            (await reader.read()).value.fill(0);
            await reader.cancel();
            var byob = blob.stream().getReader({ mode: "byob" });
            (await byob.read(new Uint8Array(8))).value.fill(0);
            await byob.cancel();
            check(await blob.text() === "ABCD", "Blob was mutated through a reader");
            check(await nested.text() === "ABCD", "Nested shared Blob changed");
            check(await slice.text() === "BC", "Shared slice changed");
        )JS");
    });
}

TEST(BlobStorage, ResolvedObjectUrlOwnsBytesAfterRuntimeTeardown)
{
    for (const auto* expression : {"new Blob()", "new Blob(['abcdef'])", "new Blob(['abc', 'def'])", "new Blob(['abcdef']).slice(1, 5)"})
    {
        SCOPED_TRACE(expression);
        std::weak_ptr<const std::vector<std::byte>> backing;
        auto request = std::make_unique<UrlLib::UrlRequest>();
        RunBlobTest([&](Napi::Env env) {
            auto blob = Napi::Eval(env, expression, "blob-url-lifetime.js");
            backing = GetBytes(blob);
            auto urlClass = env.Global().Get("URL").As<Napi::Object>();
            auto url = urlClass.Get("createObjectURL").As<Napi::Function>().Call(urlClass, {blob}).ToString();
            request->ResponseType(UrlLib::UrlResponseType::Buffer);
            request->Open(UrlLib::UrlMethod::Get, url.Utf8Value());
            request->SendAsync();
            EXPECT_EQ(request->StatusCode(), UrlLib::UrlStatusCode::Ok);
            urlClass.Get("revokeObjectURL").As<Napi::Function>().Call(urlClass, {url});
            return env.Undefined();
        });
        {
            const auto bytes = backing.lock();
            ASSERT_TRUE(bytes);
            EXPECT_EQ(request->ResponseBuffer().data(), bytes->data());
            EXPECT_EQ(request->ResponseBuffer().size(), bytes->size());
        }
        request.reset();
        EXPECT_TRUE(backing.expired());
    }
}

TEST(BlobStreams, SharesCallbacksAndUsesPinnedConstructor)
{
    RunBlobTest([](Napi::Env env) {
        return RunScript(env, R"JS(
            var global = Function("return this")();
            var NativeBlob = global.Blob;
            var Readable = global.ReadableStream;
            global.ReadableStream = function (source) { return source; };
            try {
                var blob = new NativeBlob(["abc"]);
                var first = blob.stream();
                global.Blob = undefined;
                var second = new NativeBlob(["xyz"]).stream();
                check(first.pull === second.pull && first.cancel === second.cancel, "Callbacks were allocated per stream");
                check(!Object.prototype.hasOwnProperty.call(first, "pull"), "pull is not on the shared prototype");
                check(Object.getPrototypeOf(first) === Object.getPrototypeOf(second), "Source prototypes differ");
                var values = [];
                var controller = { enqueue: function (chunk) { values.push(Array.from(chunk).join(",")); }, close: function () {} };
                first.pull.call(second, controller);
                second.pull.call(first, controller);
                check(values.join(";") === "120,121,122;97,98,99", "Shared pull callback ignored its receiver");
                var cancelled = blob.stream();
                first.cancel.call(cancelled);
                cancelled.pull(controller);
                check(values.length === 2, "Shared cancel callback ignored its receiver");
            } finally {
                global.Blob = NativeBlob;
                global.ReadableStream = Readable;
            }
        )JS");
    });
}

TEST(BlobStreams, WritesOffsetByobViewsAcrossSegments)
{
    RunBlobTest([](Napi::Env env) {
        return RunScript(env, R"JS(
            var global = Function("return this")();
            var Readable = global.ReadableStream;
            global.ReadableStream = function (source) { return source; };
            try {
                var source = new Blob(["", "012", new Blob(), "34567", "", "89", ""]).slice(1, 9).stream();
                var bytes = [], counts = [], closed = false;
                for (var i = 0; i < 3; ++i) {
                    var storage = new Uint8Array(9);
                    storage.fill(238);
                    var controller = {
                        byobRequest: { view: new Uint8Array(storage.buffer, 2, 3), respond: function (n) {
                            counts.push(n);
                            bytes.push.apply(bytes, Array.from(this.view.subarray(0, n)));
                            controller.byobRequest = null;
                        } },
                        enqueue: function () { throw new Error("BYOB allocated/enqueued an intermediate chunk"); },
                        close: function () { closed = true; }
                    };
                    source.pull(controller);
                    check(storage[0] === 238 && storage[1] === 238 && storage[5] === 238 && storage[8] === 238, "Write escaped the BYOB view");
                }
                check(bytes.join(",") === "49,50,51,52,53,54,55,56", "Segment boundary bytes differ");
                check(counts.join(",") === "3,3,2" && closed, "Wrong BYOB completion");
            } finally { global.ReadableStream = Readable; }
        )JS");
    });
}

TEST(BlobStreams, DefaultAndByobReadersHandleEmptyAndChunkBoundaries)
{
    RunBlobTest([](Napi::Env env) {
        return RunScript(env, R"JS(
            for (var length of [0, 1, 65535, 65536, 65537, 131077]) {
                var input = new Uint8Array(length);
                for (var i = 0; i < length; ++i) input[i] = i % 251;
                var blob = new Blob([input.subarray(0, 3), new Blob([input.subarray(3)])]);
                for (var mode of ["default", "byob"]) {
                    var reader = blob.stream().getReader(mode === "byob" ? { mode: "byob" } : undefined);
                    var position = 0;
                    while (true) {
                        var result = mode === "byob" ? await reader.read(new Uint8Array(new ArrayBuffer(65543), 3, 65539)) : await reader.read();
                        if (result.done) break;
                        check(result.value.length <= 65536 && result.value.length > 0, "Unbounded or empty chunk");
                        for (var value of result.value) check(value === position++ % 251, "Wrong streamed byte");
                    }
                    check(position === length, "Truncated stream at " + length + " / " + mode);
                    await reader.closed;
                    reader.releaseLock();
                }
            }
            var cancelled = new Blob([new Uint8Array(131072)]).stream().getReader({ mode: "byob" });
            await cancelled.read(new Uint8Array(3));
            await cancelled.cancel();
            check((await cancelled.read(new Uint8Array(3))).done, "Cancelled stream did not close");
        )JS");
    });
}

TEST(BlobStreams, RepeatedCreationAndCancellationKeepIndependentState)
{
    RunBlobTest([](Napi::Env env) {
        return RunScript(env, R"JS(
            var blob = new Blob(["a", "b", "c"]);
            for (var i = 0; i < 200; ++i) {
                var first = blob.stream().getReader();
                var second = blob.stream().getReader({ mode: "byob" });
                await first.cancel();
                var result = await second.read(new Uint8Array(4));
                check(Array.from(result.value).join(",") === "97,98,99", "Cancellation affected another source");
                await second.cancel();
                await new Blob().stream().cancel();
            }
        )JS");
    });
}

TEST(BlobStreams, ByobTransfersRequestAndResultBuffersOnRepeatedReads)
{
    bool transferableBuffers{};
    RunBlobTest([&](Napi::Env env) {
        transferableBuffers = Napi::Eval(env,
            "typeof ArrayBuffer.prototype.transfer === 'function' || typeof structuredClone === 'function'",
            "blob-transfer-support.js").ToBoolean().Value();
        if (!transferableBuffers)
        {
            return env.Undefined();
        }
        return RunScript(env, R"JS(
            var global = Function("return this")();
            var Readable = global.ReadableStream;
            var requests = [];
            var requestCount = 0;
            global.ReadableStream = function (source) {
                var pull = source.pull;
                Object.defineProperty(source, "pull", { value: function (controller) {
                    if (controller.byobRequest) {
                        requests.push(controller.byobRequest.view.buffer);
                        ++requestCount;
                    }
                    return pull.call(this, controller);
                } });
                return new Readable(source);
            };
            try {
                for (var parts of [[], [""], ["", "abc", new Blob(), "def", ""]]) {
                    var reader = new Blob(parts).stream().getReader({ mode: "byob" });
                    var buffer = new ArrayBuffer(5);
                    var bytes = [];
                    while (true) {
                        var previous = buffer;
                        var pending = reader.read(new Uint8Array(previous, 1, 2));
                        check(previous.byteLength === 0, "BYOB input buffer was not detached on read");
                        var result = await pending;
                        for (var request of requests) check(request.byteLength === 0, "BYOB request buffer was pinned by native pointer access");
                        requests.length = 0;
                        buffer = result.value.buffer;
                        check(buffer.byteLength === 5 && result.value.byteOffset === 1, "BYOB result lost buffer capacity or view offset");
                        if (result.done) {
                            check(result.value.byteLength === 0, "EOF returned nonempty bytes");
                            break;
                        }
                        bytes.push.apply(bytes, Array.from(result.value));
                    }
                    check(bytes.join(",") === (parts.length > 1 ? "97,98,99,100,101,102" : ""), "Repeated reads changed bytes");
                    await reader.closed;
                    reader.releaseLock();
                }
                check(requestCount === 5, "Did not observe all native BYOB requests");
            } finally { global.ReadableStream = Readable; }
        )JS");
    });
    if (!transferableBuffers)
    {
        GTEST_SKIP() << "Engine does not expose transferable ArrayBuffers to the stream polyfill";
    }
}

TEST(BlobStorage, GarbageCollectionPreservesThenReleasesStreamAndUrlOwners)
{
#if defined(JSRUNTIMEHOST_APPLE_JAVASCRIPTCORE)
    using Collect = void (*)(JSContextRef);
    const auto collect = reinterpret_cast<Collect>(dlsym(RTLD_DEFAULT, "JSSynchronousGarbageCollectForDebugging"));
    if (!collect)
    {
        GTEST_SKIP() << "JavaScriptCore's synchronous test collector is unavailable";
    }
    RunBlobTest([collect](Napi::Env env) {
        auto backing = std::make_shared<std::array<std::weak_ptr<const std::vector<std::byte>>, 2>>();
        env.Global().Set("trackBlobBacking", Napi::Function::New(env, [backing](const Napi::CallbackInfo& info) {
            backing->at(info[1].As<Napi::Number>().Uint32Value()) = GetBytes(info[0]);
        }));
        env.Global().Set("blobBackingAlive", Napi::Function::New(env, [backing](const Napi::CallbackInfo& info) {
            return Napi::Boolean::New(info.Env(), !backing->at(info[0].As<Napi::Number>().Uint32Value()).expired());
        }));
        env.Global().Set("collectBlobGarbage", Napi::Function::New(env, [collect](const Napi::CallbackInfo& info) {
            collect(Napi::GetContext(info.Env()));
        }));
        return RunScript(env, R"JS(
            async function collectUntil(label, predicate) {
                for (var i = 0; i < 30; ++i) {
                    await new Promise(function (resolve) { setTimeout(resolve, 1); });
                    collectBlobGarbage();
                    if (predicate()) return;
                }
                throw new Error("GC did not reach the expected ownership state: " + label);
            }
            async function exercise(mode, kind) {
                var base = new Blob([new Uint8Array(131072)]);
                trackBlobBacking(base, 0);
                var blob = kind === "whole" ? base : kind === "slice" ? base.slice(1, 131071) :
                    kind === "multipart" ? new Blob([base.slice(0, 65536), "", new Blob(), base.slice(65536)]) : new Blob();
                var length = blob.size;
                trackBlobBacking(blob, 1);
                var weakBase = new WeakRef(base);
                var weak = new WeakRef(blob);
                var url = URL.createObjectURL(blob);
                var stream = blob.stream();
                var weakStream = new WeakRef(stream);
                base = null;
                blob = null;
                try {
                    await collectUntil(mode + "/" + kind + " Blob", function () { return weak.deref() === undefined && weakBase.deref() === undefined; });
                    check(blobBackingAlive(1), "Backing died while URL still owned it");
                    URL.revokeObjectURL(url);
                    if (kind !== "empty") check(blobBackingAlive(0), "Revocation invalidated the stream's segments");
                    if (kind !== "whole") await collectUntil(kind + " unused flat cache", function () { return !blobBackingAlive(1); });
                    if (mode === "cancel") await stream.cancel();
                    if (mode === "drain") {
                        var reader = stream.getReader({ mode: "byob" });
                        var count = 0;
                        while (true) {
                            var result = await reader.read(new Uint8Array(65536));
                            if (result.done) break;
                            count += result.value.length;
                            check(result.value.every(function (value) { return value === 0; }), "Backing corrupted after GC/revoke");
                        }
                        check(count === length, "GC/revoke truncated stream");
                        reader.releaseLock();
                        reader = null;
                    }
                    stream = null;
                    return weakStream;
                } finally { URL.revokeObjectURL(url); }
            }
            for (var mode of ["cancel", "drain", "abandon"]) {
                for (var kind of ["whole", "slice", "multipart", "empty"]) {
                    var weakStream = await exercise(mode, kind);
                    await collectUntil(mode + "/" + kind + " stream", function () { return weakStream.deref() === undefined; });
                    await collectUntil(mode + "/" + kind + " backing", function () { return !blobBackingAlive(0) && !blobBackingAlive(1); });
                }
            }
        )JS");
    });
#else
    GTEST_SKIP() << "Explicit garbage collection is currently exercised with Apple JavaScriptCore";
#endif
}
