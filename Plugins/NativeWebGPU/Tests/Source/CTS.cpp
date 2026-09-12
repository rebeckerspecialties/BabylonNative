#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/WgpuInterop.h>
#include <Babylon/Plugins/NativeWebGPU.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/ScriptLoader.h>

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    struct Completion
    {
        std::atomic_bool Finished{};
        std::promise<int> Result;

        void Finish(int result)
        {
            if (!Finished.exchange(true))
            {
                Result.set_value(result);
            }
        }
    };
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: NativeWebGPUCtsRunner bundle.js [timeout-seconds [CTS-query...]]\n";
        return 2;
    }

    try
    {
        const auto timeout = std::chrono::seconds(argc >= 3 ? std::stoi(argv[2]) : 180);
        if (timeout.count() <= 0)
        {
            throw std::runtime_error{"Timeout must be positive"};
        }
        std::ifstream input{argv[1], std::ios::binary};
        if (!input)
        {
            throw std::runtime_error{"Cannot open CTS bundle"};
        }
        const std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};

        BabylonWgpuConfig config{};
        config.width = 64;
        config.height = 64;
        config.enable_validation = 1;
        std::unique_ptr<void, decltype(&babylon_wgpu_destroy)> backend{babylon_wgpu_create(&config), babylon_wgpu_destroy};
        if (!backend)
        {
            std::array<char, 2048> error{};
            babylon_wgpu_get_last_error(error.data(), error.size());
            throw std::runtime_error{std::string{"WGPU initialization failed: "} + error.data()};
        }
        BabylonWgpuInfo backendInfo{};
        if (!babylon_wgpu_get_info(backend.get(), &backendInfo))
        {
            throw std::runtime_error{"Cannot identify the WGPU adapter"};
        }
        std::cerr << "CTS adapter: " << backendInfo.adapter_name << "; backend=" << backendInfo.backend << std::endl;

        auto completion = std::make_shared<Completion>();
        auto result = completion->Result.get_future();
        Babylon::AppRuntime::Options options{};
        options.UnhandledExceptionHandler = [completion](const Napi::Error& error) {
            std::cerr << "CTS host exception: " << Napi::GetErrorString(error) << std::endl;
            completion->Finish(2);
        };

        // The runtime must drain and release JS resources before the backend is destroyed.
        Babylon::AppRuntime runtime{options};
        std::vector<std::string> queries;
        for (int index = 3; index < argc; ++index)
        {
            queries.emplace_back(argv[index]);
        }
        runtime.Dispatch([completion, queries = std::move(queries)](Napi::Env env) {
            Babylon::Polyfills::Window::Initialize(env);
            Babylon::Plugins::NativeWebGPU::Initialize(env);
            env.Global().Set("__ctsLog", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                if (info.Length() > 0)
                {
                    std::cout << info[0].ToString().Utf8Value() << std::endl;
                }
            }));
            env.Global().Set("__ctsDone", Napi::Function::New(env, [completion](const Napi::CallbackInfo& info) {
                completion->Finish(info.Length() > 0 && info[0].IsBoolean() && info[0].As<Napi::Boolean>().Value() ? 0 : 1);
            }));
            auto performance = Napi::Object::New(env);
            const auto start = std::chrono::steady_clock::now();
            performance.Set("now", Napi::Function::New(env, [start](const Napi::CallbackInfo& info) {
                const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                return Napi::Number::New(info.Env(), elapsed);
            }));
            env.Global().Set("performance", performance);
            if (!queries.empty())
            {
                auto queryArray = Napi::Array::New(env, queries.size());
                for (size_t index = 0; index < queries.size(); ++index)
                {
                    queryArray.Set(static_cast<uint32_t>(index), Napi::String::New(env, queries[index]));
                }
                env.Global().Set("__ctsQueries", queryArray);
            }
        });

        Babylon::ScriptLoader loader{runtime};
        loader.Eval(source, argv[1]);
        if (result.wait_for(timeout) != std::future_status::ready)
        {
            std::cerr << "CTS host timed out after " << timeout.count() << " seconds" << std::endl;
            completion->Finish(2);
        }
        return result.get();
    }
    catch (const std::exception& error)
    {
        std::cerr << "CTS host: " << error.what() << std::endl;
        return 2;
    }
}
