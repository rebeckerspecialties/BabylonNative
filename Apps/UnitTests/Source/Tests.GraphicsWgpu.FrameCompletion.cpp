#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Graphics/DeviceContext.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

extern Babylon::Graphics::Configuration g_deviceConfig;

TEST(GraphicsWgpuFrameCompletion, PresentationWaitsForOneOrMultipleCallbackScopes)
{
    for (const size_t count : {0u, 1u, 4u})
    {
        Babylon::Graphics::Device device{g_deviceConfig};
        device.EnableRendering();
        device.StartRenderingCurrentFrame();
        Babylon::AppRuntime runtime{};
        std::promise<void> acquired;
        std::promise<void> release;
        std::promise<void> completed;
        auto released = release.get_future();
        runtime.Dispatch([&](Napi::Env env) {
            device.AddToJavaScript(env);
            auto& context = Babylon::Graphics::DeviceContext::GetFromJavaScript(env);
            std::vector<Babylon::Graphics::FrameCompletionScope> scopes;
            for (size_t i = 0; i < count; ++i)
            {
                auto scope = context.TryAcquireFrameCompletionScope();
                EXPECT_TRUE(scope.has_value());
                if (scope) scopes.push_back(std::move(*scope));
            }
            acquired.set_value();
            released.wait();
            scopes.clear();
            completed.set_value();
        });
        acquired.get_future().wait();

        std::atomic_bool finished{};
        bool finishedBeforeRelease{};
        std::thread unblock{[&] {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            finishedBeforeRelease = finished.load();
            release.set_value();
        }};
        device.FinishRenderingCurrentFrame();
        finished.store(true);
        unblock.join();
        completed.get_future().wait();
        if (count != 0)
        {
            EXPECT_FALSE(finishedBeforeRelease) << "Presentation ended inside a callback, scopes=" << count;
        }
    }
}

TEST(GraphicsWgpuFrameCompletion, ExceptionUnwindsTheCallbackScope)
{
    Babylon::Graphics::Device device{g_deviceConfig};
    device.EnableRendering();
    device.StartRenderingCurrentFrame();
    Babylon::AppRuntime runtime{};
    std::promise<void> completed;
    runtime.Dispatch([&](Napi::Env env) {
        device.AddToJavaScript(env);
        try
        {
            auto scope = Babylon::Graphics::DeviceContext::GetFromJavaScript(env).TryAcquireFrameCompletionScope();
            EXPECT_TRUE(scope.has_value());
            throw std::runtime_error{"callback failure"};
        }
        catch (const std::runtime_error&)
        {
            completed.set_value();
        }
    });
    completed.get_future().wait();
    device.FinishRenderingCurrentFrame();
}

TEST(GraphicsWgpuFrameCompletion, CallbackBetweenFramesWaitsForTheNextFrame)
{
    Babylon::Graphics::Device device{g_deviceConfig};
    device.EnableRendering();
    device.StartRenderingCurrentFrame();
    device.FinishRenderingCurrentFrame();
    Babylon::AppRuntime runtime{};
    std::promise<void> entered;
    std::promise<void> acquired;
    auto result = acquired.get_future();
    runtime.Dispatch([&](Napi::Env env) {
        device.AddToJavaScript(env);
        auto& context = Babylon::Graphics::DeviceContext::GetFromJavaScript(env);
        entered.set_value();
        auto scope = context.AcquireFrameCompletionScope();
        acquired.set_value();
    });
    entered.get_future().wait();
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds{100}), std::future_status::timeout);
    device.StartRenderingCurrentFrame();
    EXPECT_EQ(result.wait_for(std::chrono::seconds{30}), std::future_status::ready);
    device.FinishRenderingCurrentFrame();
}

TEST(GraphicsWgpuFrameCompletion, NonblockingAcquisitionDefersClosedFramesWithoutStrandingScopes)
{
    Babylon::Graphics::Device device{g_deviceConfig};
    device.EnableRendering();
    Babylon::AppRuntime runtime{};
    for (const bool open : {false, true, false, true, false})
    {
        if (open) device.StartRenderingCurrentFrame();
        std::promise<void> completed;
        runtime.Dispatch([&](Napi::Env env) {
            device.AddToJavaScript(env);
            auto scope = Babylon::Graphics::DeviceContext::GetFromJavaScript(env).TryAcquireFrameCompletionScope();
            EXPECT_EQ(scope.has_value(), open);
            completed.set_value();
        });
        completed.get_future().wait();
        if (open) device.FinishRenderingCurrentFrame();
    }
}
