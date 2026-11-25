#include <napi/env.h>
#include <napi/js_native_api_types.h>
#include "js_native_api_v8.h"

#include <utility>
#ifdef __ANDROID__
#include <algorithm>
#include <array>
#include <android/log.h>
#include <dlfcn.h>
#include <malloc.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>
#endif

namespace
{
  // Runs finalizers queued by garbage collection on a posted task, as Node.js's node_napi_env__
  // does with SetImmediate.
  struct scheduled_napi_env__ : napi_env__
  {
    scheduled_napi_env__(v8::Local<v8::Context> context, std::function<void(std::function<void()>)> postTask)
      : napi_env__(context, NAPI_VERSION)
      , post_task{std::move(postTask)}
    {
    }

    void EnqueueFinalizer(v8impl::RefTracker* finalizer) override
    {
      napi_env__::EnqueueFinalizer(finalizer);
      if (!finalization_scheduled && !destructing)
      {
        finalization_scheduled = true;
        post_task([this]() {
          finalization_scheduled = false;
          DrainFinalizerQueue();
        });
      }
    }

    void DeleteMe() override
    {
      destructing = true;
      DrainFinalizerQueue();
      napi_env__::DeleteMe();
    }

  protected:
    virtual void DrainFinalizerQueue()
    {
      DrainWith([](v8impl::RefTracker* entry) { entry->Finalize(); });
    }

    template<class Visit>
    void DrainWith(Visit visit)
    {
      // A finalizer can queue or delete other finalizers, so take them one at a time.
      while (!pending_finalizers.empty())
      {
        v8impl::RefTracker* finalizer = *pending_finalizers.begin();
        pending_finalizers.erase(finalizer);
        visit(finalizer);
      }
    }

  private:
    std::function<void(std::function<void()>)> post_task;
    bool finalization_scheduled = false;
    bool destructing = false;
  };

#ifdef __ANDROID__
  bool DiagnosticEnabled(const char* name)
  {
    char value[PROP_VALUE_MAX]{};
    return __system_property_get(name, value) == 1 && value[0] == '1';
  }

  long long Micros(clockid_t clock)
  {
    timespec ts{};
    clock_gettime(clock, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
  }

  // Only diagnostic isolates install the epilogue callback or time destructor calls.
  struct profiled_napi_env__ final : scheduled_napi_env__
  {
    profiled_napi_env__(v8::Local<v8::Context> context, std::function<void(std::function<void()>)> post)
      : scheduled_napi_env__{context, std::move(post)}
    {
      isolate->AddGCEpilogueCallback(AfterGc, this);
    }

    ~profiled_napi_env__() override { isolate->RemoveGCEpilogueCallback(AfterGc, this); }

    void CallFinalizer(napi_finalize cb, void* data, void* hint) override
    {
      if (types && inDrain)
      {
        ++typedCallbacks;
        auto entry = std::find_if(mix.begin(), mix.end(), [cb](const Entry& e) { return !e.count || e.callback == cb; });
        if (entry != mix.end())
        {
          entry->callback = cb;
          ++entry->count;
        }
        else ++overflow;
      }
      if (!detail)
      {
        napi_env__::CallFinalizer(cb, data, hint);
        return;
      }
      const auto begin = Micros(CLOCK_MONOTONIC);
      napi_env__::CallFinalizer(cb, data, hint);
      const auto elapsed = Micros(CLOCK_MONOTONIC) - begin;
      if (elapsed >= maxUs) { maxUs = elapsed; slowest = cb; }
      ++calls;
    }

    void DrainFinalizerQueue() override
    {
      const auto queued = pending_finalizers.size();
      if (!queued) return;
      calls = 0;
      maxUs = 0;
      slowest = nullptr;
      // Bionic's allocator counters are process-wide. Keep their collection
      // outside the timed region and distinguish net bytes from allocator CPU.
      const auto allocated = heap ? mallinfo().uordblks : 0;
      const auto gc = gcEnd;
      const auto sequence = gcSequence;
      size_t popped{}, withoutCallback{};
      if (types) { mix.fill({}); overflow = typedCallbacks = 0; inDrain = true; }
      const auto begin = Micros(CLOCK_MONOTONIC);
      const auto cpu = Micros(CLOCK_THREAD_CPUTIME_ID);
      if (types)
      {
        DrainWith([&](v8impl::RefTracker* entry) {
          const auto before = typedCallbacks;
          entry->Finalize();
          ++popped;
          if (typedCallbacks == before) ++withoutCallback;
        });
      }
      else scheduled_napi_env__::DrainFinalizerQueue();
      const auto elapsed = Micros(CLOCK_MONOTONIC) - begin;
      const auto cpuUs = Micros(CLOCK_THREAD_CPUTIME_ID) - cpu;
      inDrain = false;
      const auto delta = heap ? static_cast<long long>(mallinfo().uordblks) - static_cast<long long>(allocated) : 0;
      Dl_info location{};
      const auto pc = reinterpret_cast<uintptr_t>(slowest);
      if (pc) dladdr(reinterpret_cast<void*>(pc), &location);
      __android_log_print(ANDROID_LOG_INFO, "BabylonFinalize",
        "tid=%d gc=%u gcEndUs=%lld startUs=%lld wallUs=%lld cpuUs=%lld queued=%zu callbackSample=%d callbacks=%zu maxCallbackUs=%lld module=%s offset=%zx heapSample=%d netHeapBytes=%lld typeSample=%d popped=%zu withoutCallback=%zu",
        gettid(), sequence, gc, begin, elapsed, cpuUs, queued, detail, calls, maxUs,
        location.dli_fname ? location.dli_fname : "unknown", static_cast<size_t>(pc - reinterpret_cast<uintptr_t>(location.dli_fbase)), heap, delta, types, popped, withoutCallback);
      if (types)
      {
        for (const auto& entry : mix)
        {
          if (!entry.count) break;
          Dl_info source{};
          const auto address = reinterpret_cast<uintptr_t>(entry.callback);
          dladdr(reinterpret_cast<void*>(address), &source);
          __android_log_print(ANDROID_LOG_INFO, "BabylonFinalType", "tid=%d gc=%u count=%zu module=%s offset=%zx",
            gettid(), sequence, entry.count, source.dli_fname ? source.dli_fname : "unknown",
            static_cast<size_t>(address - reinterpret_cast<uintptr_t>(source.dli_fbase)));
        }
        if (overflow)
          __android_log_print(ANDROID_LOG_INFO, "BabylonFinalType", "tid=%d gc=%u overflow=%zu", gettid(), sequence, overflow);
      }
    }
  private:
    static void AfterGc(v8::Isolate*, v8::GCType, v8::GCCallbackFlags, void* data)
    {
      auto& env = *static_cast<profiled_napi_env__*>(data);
      env.gcEnd = Micros(CLOCK_MONOTONIC);
      ++env.gcSequence;
    }
    const bool heap{DiagnosticEnabled("debug.babylon.fin_heap")};
    const bool detail{DiagnosticEnabled("debug.babylon.fin_detail")};
    struct Entry { napi_finalize callback{}; size_t count{}; };
    std::array<Entry, 64> mix{};
    size_t overflow{}, typedCallbacks{};
    bool inDrain{};
    const bool types{DiagnosticEnabled("debug.babylon.fin_types")};
    unsigned gcSequence{};
    long long gcEnd{}, maxUs{};
    size_t calls{};
    napi_finalize slowest{};
  };
#endif
}

namespace Napi
{
  Env Attach(v8::Local<v8::Context> context, std::function<void(std::function<void()>)> postTask)
  {
    if (!postTask)
    {
      // second argument is module version
      return {new napi_env__(context, NAPI_VERSION)};
    }

#ifdef __ANDROID__
    if (DiagnosticEnabled("debug.babylon.finalizers"))
      return {new profiled_napi_env__(context, std::move(postTask))};
#endif
    return {new scheduled_napi_env__(context, std::move(postTask))};
  }

  void Detach(Env env)
  {
    napi_env env_ptr{env};
    env_ptr->DeleteMe();
  }

  v8::Local<v8::Context> GetContext(Env env)
  {
    napi_env env_ptr{env};
    return env_ptr->context();
  }
}
