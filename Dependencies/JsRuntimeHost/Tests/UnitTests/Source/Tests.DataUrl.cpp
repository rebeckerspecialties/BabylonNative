#include <Babylon/AppRuntime.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/Compression.h>
#include <Babylon/Polyfills/DataUrl.h>
#include <Babylon/Polyfills/Fetch.h>
#include <Babylon/Polyfills/TextDecoder.h>
#include <Babylon/Polyfills/URL.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <UrlLib/UrlLib.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    struct DataUrlCase
    {
        const char* url;
        const char* contentType;
        std::vector<uint8_t> bytes;
    };

    const std::vector<DataUrlCase> DecodeCases{
        {"data:,", "text/plain;charset=US-ASCII", {}},
        {"data:,a%20b+c%GG%2", "text/plain;charset=US-ASCII", {'a', ' ', 'b', '+', 'c', '%', 'G', 'G', '%', '2'}},
        {"data:;charset=UTF-8,hi", "text/plain;charset=UTF-8", {'h', 'i'}},
        {"data:application/octet-stream,%00%01%02%ff", "application/octet-stream", {0, 1, 2, 255}},
        {"DaTa:application/octet-stream;BaSe64,AAEC%2Fw%3D%3D#ignored", "application/octet-stream", {0, 1, 2, 255}},
        {"data:text/plain;base64,SG%20VsbG8=%0A", "text/plain", {'H', 'e', 'l', 'l', 'o'}},
        {"data:text/plain;base64,QQ", "text/plain", {'A'}},
        {"data:text/plain,a%23b#ignored", "text/plain", {'a', '#', 'b'}},
    };

    const std::vector<const char*> InvalidUrls{
        "data:text/plain",
        "data:;base64,A",
        "data:;base64,AA===",
        "data:;base64,A=AA",
        "data:;base64,@@@",
        "data:;base64,AA-_",
    };

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

    void RunDataUrlScript(const char* script)
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
                Babylon::Polyfills::URL::Initialize(env);
                Babylon::Polyfills::Compression::Initialize(env);
                Babylon::Polyfills::Fetch::Initialize(env);
                Babylon::Polyfills::XMLHttpRequest::Initialize(env);
                Babylon::Polyfills::TextDecoder::Initialize(env);
                env.Global().Set("__dataUrlDone", Napi::Function::New(env, [completion](const Napi::CallbackInfo&) {
                    completion->Finish();
                }));
                env.Global().Set("__dataUrlFailed", Napi::Function::New(env, [completion](const Napi::CallbackInfo& info) {
                    completion->Finish(std::make_exception_ptr(std::runtime_error{info[0].ToString().Utf8Value()}));
                }));
                const std::string source = std::string{R"JS(
                    (async function () {
                        function check(condition, message) {
                            if (!condition) { throw new Error(message); }
                        }
                        function equal(actual, expected) {
                            check(actual.length === expected.length, "byte length mismatch");
                            for (let i = 0; i < expected.length; ++i) {
                                check(actual[i] === expected[i], "byte mismatch at " + i);
                            }
                        }
                        function xhr(url, responseType) {
                            return new Promise(resolve => {
                                const request = new XMLHttpRequest();
                                request.open("GET", url);
                                request.responseType = responseType;
                                request.addEventListener("loadend", () => resolve(request));
                                request.send();
                            });
                        }
                )JS"} + script + R"JS(
                    })().then(__dataUrlDone, error => __dataUrlFailed(String(error) + "\n" + (error && error.stack || "")));
                )JS";
                Napi::Eval(env, source.c_str(), "data-url-regression.js");
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
            throw std::runtime_error{"data URL test timed out"};
        }
        future.get();
    }

    void InitializeUrlResolver()
    {
        Babylon::AppRuntime runtime;
        std::promise<void> completion;
        auto future = completion.get_future();
        runtime.Dispatch([&completion](Napi::Env env) {
            try
            {
                Babylon::Polyfills::URL::Initialize(env);
                completion.set_value();
            }
            catch (const Napi::Error& error)
            {
                completion.set_exception(std::make_exception_ptr(std::runtime_error{Napi::GetErrorString(error)}));
            }
            catch (...)
            {
                completion.set_exception(std::current_exception());
            }
        });
        future.get();
    }

    bool SendSynchronouslyAndWait(UrlLib::UrlRequest& request)
    {
        auto completion = std::make_shared<std::promise<void>>();
        auto future = completion->get_future();
        request.SendAsync().then(arcana::inline_scheduler, arcana::cancellation::none(),
            [completion](const arcana::expected<void, std::exception_ptr>&) { completion->set_value(); });
        const bool synchronous = future.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
        // Drain the pre-fix platform request before its implementation is destroyed.
        future.get();
        return synchronous;
    }
}

TEST(DataUrl, SharedDecoderMatchesByteAndMetadataMatrix)
{
    for (const auto& test : DecodeCases)
    {
        SCOPED_TRACE(test.url);
        const auto response = Babylon::Polyfills::DataUrl::Parse(test.url);
        ASSERT_TRUE(response.has_value());
        EXPECT_EQ(response->body, test.bytes);
        EXPECT_EQ(response->contentType, test.contentType);
        const std::string url{test.url};
        EXPECT_EQ(response->url, url.substr(0, url.find('#')));
    }
}

TEST(DataUrl, SharedDecoderRejectsMalformedInput)
{
    for (const auto* url : InvalidUrls)
    {
        SCOPED_TRACE(url);
        EXPECT_THROW(Babylon::Polyfills::DataUrl::Parse(url), std::runtime_error);
    }
}

TEST(DataUrl, SharedDecoderDeclinesOtherSchemes)
{
    for (const auto* url : {"", "data", "database:,hello", "https://example.com", "blob:null/test"})
    {
        SCOPED_TRACE(url);
        EXPECT_FALSE(Babylon::Polyfills::DataUrl::Parse(url).has_value());
    }
}

TEST(DataUrl, RegisteredResolverDecodesWithoutPlatformTransport)
{
    InitializeUrlResolver();
    for (const auto& test : DecodeCases)
    {
        SCOPED_TRACE(test.url);
        UrlLib::UrlRequest request;
        request.Open(UrlLib::UrlMethod::Get, test.url);
        request.ResponseType(UrlLib::UrlResponseType::Buffer);
        EXPECT_TRUE(SendSynchronouslyAndWait(request)) << "registered schemes must settle inline";
        ASSERT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::Ok);
        EXPECT_EQ(request.GetResponseHeader("content-type"), test.contentType);
        EXPECT_TRUE(request.ErrorSymbol().empty());
        const auto bytes = request.ResponseBuffer();
        ASSERT_EQ(bytes.size(), test.bytes.size());
        for (size_t i = 0; i < test.bytes.size(); ++i)
        {
            EXPECT_EQ(std::to_integer<uint8_t>(bytes[i]), test.bytes[i]);
        }
    }
}

TEST(DataUrl, MalformedInputUsesRegisteredResolverError)
{
    InitializeUrlResolver();
    for (const auto* url : InvalidUrls)
    {
        SCOPED_TRACE(url);
        UrlLib::UrlRequest request;
        request.Open(UrlLib::UrlMethod::Get, url);
        request.ResponseType(UrlLib::UrlResponseType::Buffer);
        EXPECT_TRUE(SendSynchronouslyAndWait(request));
        EXPECT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::None);
        // NSURLSession can decode data: too; this exact error proves common resolver dispatch.
        EXPECT_EQ(request.ErrorSymbol(), "SchemeResolverNotFound");
        EXPECT_TRUE(request.ResponseBuffer().empty());
    }
}

TEST(DataUrl, FetchAndXhrDecodeMatrix)
{
    RunDataUrlScript(R"JS(
        const cases = [
            ["data:,", "text/plain;charset=US-ASCII", []],
            ["data:text/plain;charset=utf-8,hello%20world", "text/plain;charset=utf-8", [104,101,108,108,111,32,119,111,114,108,100]],
            ["data:application/octet-stream,%00%01%02%ff", "application/octet-stream", [0,1,2,255]],
            ["data:application/octet-stream;base64,AAEC/w==", "application/octet-stream", [0,1,2,255]],
            ["data:text/plain;base64,QQ", "text/plain", [65]],
        ];
        for (const [url, type, expected] of cases) {
            const response = await fetch(url);
            check(response.status === 200, "fetch status for " + url);
            check(response.headers.get("content-type") === type, "fetch content type");
            equal(new Uint8Array(await response.arrayBuffer()), expected);
            const request = await xhr(url, "arraybuffer");
            check(request.status === 200, "XHR status for " + url);
            check(request.getResponseHeader("content-type") === type, "XHR content type");
            equal(new Uint8Array(request.response), expected);
        }
        const text = await xhr("data:text/plain,hello%20world", "text");
        check(text.responseText === "hello world", "XHR text");
        for (const url of ["data:text/plain", "data:;base64,A", "data:;base64,@@@", "data:;base64,AA==="]) {
            check(await fetch(url).catch(error => error) instanceof TypeError, "fetch must reject malformed input");
            check((await xhr(url, "arraybuffer")).status === 0, "XHR must report a network error");
        }
    )JS");
}

TEST(DataUrl, BlobObjectUrlRoundTrips)
{
    RunDataUrlScript(R"JS(
        for (const dataUrl of ["data:application/octet-stream,%00%01%02%ff", "data:application/octet-stream;base64,AAEC/w=="]) {
            const blob = await (await fetch(dataUrl)).blob();
            check(blob.size === 4 && blob.type === "application/octet-stream", "Blob metadata");
            const objectUrl = URL.createObjectURL(blob);
            try {
                equal(new Uint8Array(await (await fetch(objectUrl)).arrayBuffer()), [0,1,2,255]);
                const request = await xhr(objectUrl, "arraybuffer");
                check(request.status === 200, "Blob XHR status");
                equal(new Uint8Array(request.response), [0,1,2,255]);
            } finally {
                URL.revokeObjectURL(objectUrl);
            }
        }
    )JS");
}

TEST(DataUrl, GzipThroughFetchAndXhrBlobStreams)
{
    RunDataUrlScript(R"JS(
        const url = "data:application/gzip;base64,H4sIAAAAAAAC/8tIzcnJV0ivyiwAABlq0t8KAAAA";
        const response = await fetch(url);
        check(response.headers.get("content-type") === "application/gzip", "gzip content type");
        const direct = response.body.pipeThrough(new DecompressionStream("gzip"));
        check(await new Response(direct).text() === "hello gzip", "fetch gzip");
        const request = await xhr(url, "arraybuffer");
        check(request.status === 200, "gzip XHR status");
        const blob = new Blob([request.response], {type: "application/gzip"});
        const decoded = blob.stream().pipeThrough(new DecompressionStream("gzip"));
        check(await new Response(decoded).text() === "hello gzip", "XHR Blob gzip");
    )JS");
}
