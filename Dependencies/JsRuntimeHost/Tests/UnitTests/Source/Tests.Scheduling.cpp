#include <Babylon/AppRuntime.h>
#include <gtest/gtest.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/Performance.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

TEST(Performance, TimeOriginIsPerEnvironment)
{
    const auto now = [](Babylon::AppRuntime& runtime, bool initialize) {
        auto result = std::make_shared<std::promise<double>>();
        auto future = result->get_future();
        runtime.Dispatch([result, initialize](Napi::Env env) {
            try
            {
                if (initialize)
                {
                    Babylon::Polyfills::Performance::Initialize(env);
                }
                auto performance = env.Global().Get("performance").As<Napi::Object>();
                result->set_value(performance.Get("now").As<Napi::Function>().Call(performance, {}).As<Napi::Number>().DoubleValue());
            }
            catch (...)
            {
                result->set_exception(std::current_exception());
            }
        });
        if (future.wait_for(std::chrono::seconds{10}) != std::future_status::ready)
        {
            throw std::runtime_error{"performance.now() dispatch timed out"};
        }
        return future.get();
    };

    Babylon::AppRuntime first{};
    now(first, true);
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    const double before = now(first, false);

    Babylon::AppRuntime second{};
    now(second, true);
    EXPECT_GE(now(first, false), before);
    // Re-initializing the same environment must also preserve its origin.
    EXPECT_GE(now(first, true), before);
}

// The unit test host's UnhandledExceptionHandler fails the whole JavaScript
// suite, so a throwing timer callback cannot be exercised from the script tests. This
// covers it natively instead.
TEST(Scheduling, IntervalSurvivesThrowingCallback)
{
    // Regression: repeating timeouts are re-armed after their callback returns
    // rather than before it runs, so an exception escaping a tick must not
    // silently stop the interval. Browsers keep the interval running and report
    // the error, and that is also what this dispatcher did previously.
    std::promise<int32_t> tickCountPromise;
    std::atomic<int32_t> unhandledErrorCount{0};

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [&unhandledErrorCount](const Napi::Error&) {
        ++unhandledErrorCount;
    };

    Babylon::AppRuntime runtime{options};

    runtime.Dispatch([&tickCountPromise](Napi::Env env) {
        Babylon::Polyfills::Scheduling::Initialize(env);

        auto reportTicks = Napi::Function::New(
            env, [&tickCountPromise](const Napi::CallbackInfo& info) {
                tickCountPromise.set_value(info[0].As<Napi::Number>().Int32Value());
            },
            "reportTicks");
        env.Global().Set("reportTicks", reportTicks);
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        var ticks = 0;
        var id = setInterval(function () {
            ticks++;
            if (ticks === 3) {
                clearInterval(id);
                reportTicks(ticks);
                return;
            }
            throw new Error('tick failed');
        }, 1);
    )",
        "");

    auto tickCountFuture{tickCountPromise.get_future()};
    ASSERT_EQ(tickCountFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "the interval stopped after a tick threw";
    EXPECT_EQ(tickCountFuture.get(), 3);

    // The first two ticks threw, and those errors must still be surfaced.
    EXPECT_EQ(unhandledErrorCount.load(), 2);
}
