#pragma once

#include <android/log.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <v8-profiler.h>
#pragma clang diagnostic pop

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Babylon::Internal
{
    // Diagnostic only. Sample after startup; ordinary isolates never post either task.
    class V8AllocationSample final : public v8::Task
    {
    public:
        explicit V8AllocationSample(std::weak_ptr<v8::TaskRunner> runner)
            : m_runner{std::move(runner)}
        {
        }

        V8AllocationSample() = default;

        void Run() override
        {
            auto* isolate = v8::Isolate::GetCurrent();
            v8::HandleScope scope{isolate};
            auto* profiler = isolate->GetHeapProfiler();
            if (m_runner)
            {
                const auto runner = m_runner->lock();
                if (!runner) return;
                using Profiler = v8::HeapProfiler;
                const auto flags = static_cast<Profiler::SamplingFlags>(
                    Profiler::kSamplingIncludeObjectsCollectedByMinorGC | Profiler::kSamplingIncludeObjectsCollectedByMajorGC);
                if (profiler->StartSamplingHeapProfiler(64 * 1024, 8, flags))
                {
                    runner->PostDelayedTask(std::make_unique<V8AllocationSample>(), 30);
                    __android_log_print(ANDROID_LOG_INFO, "BabylonAlloc", "sample start");
                }
                return;
            }

            std::unique_ptr<v8::AllocationProfile> profile{profiler->GetAllocationProfile()};
            if (!profile) return;
            using Node = v8::AllocationProfile::Node;
            struct Site
            {
                const Node* node;
                const Node* caller;
                uint64_t bytes;
            };
            std::vector<Site> sites;
            const auto visit = [&](auto& self, const Node* node, const Node* caller) -> void {
                uint64_t bytes{};
                for (const auto& allocation : node->allocations)
                    bytes += static_cast<uint64_t>(allocation.size) * allocation.count;
                if (bytes) sites.push_back({node, caller, bytes});
                for (const auto* child : node->children) self(self, child, node);
            };
            visit(visit, profile->GetRootNode(), nullptr);
            const auto n = std::min<size_t>(sites.size(), 24);
            std::partial_sort(sites.begin(), sites.begin() + n, sites.end(), [](const Site& a, const Site& b) { return a.bytes > b.bytes; });
            for (size_t i = 0; i < n; ++i)
            {
                const auto [node, caller, bytes] = sites[i];
                const v8::String::Utf8Value name{isolate, node->name};
                const v8::String::Utf8Value script{isolate, node->script_name};
                const v8::String::Utf8Value parent{isolate, caller ? caller->name : v8::String::Empty(isolate)};
                __android_log_print(ANDROID_LOG_INFO, "BabylonAlloc", "estimatedBytes=%llu site=%s %s:%d:%d caller=%s:%d:%d",
                    static_cast<unsigned long long>(bytes), *name, *script, node->line_number, node->column_number, *parent,
                    caller ? caller->line_number : 0, caller ? caller->column_number : 0);
            }
            profile.reset();
            profiler->StopSamplingHeapProfiler();
            __android_log_print(ANDROID_LOG_INFO, "BabylonAlloc", "sample end sites=%zu", sites.size());
        }

    private:
        std::optional<std::weak_ptr<v8::TaskRunner>> m_runner;
    };
}
