#pragma once

#include "DelayedTaskScheduler.h"
#include <napi/env.h>
#include <v8-version.h>

#include <functional>
#include <memory>

namespace Babylon::Internal
{
    class V8ForegroundTaskRunner final : public v8::TaskRunner
    {
    public:
        using DispatchFunction = std::function<void(std::shared_ptr<v8::Task>)>;
        using ScheduleFunction = std::function<DelayedTaskScheduler::Id(DelayedTaskScheduler::TimePoint, DelayedTaskScheduler::Callback)>;
        using CancelFunction = std::function<void(DelayedTaskScheduler::Id)>;

        V8ForegroundTaskRunner(DispatchFunction dispatch, ScheduleFunction schedule, CancelFunction cancel);
        ~V8ForegroundTaskRunner() override;

        static DelayedTaskScheduler::TimePoint GetScheduledTime(std::chrono::steady_clock::time_point now, double delayInSeconds);

        // After shutdown, even references retained by V8 discard new posts.
        void Shutdown();

#if V8_MAJOR_VERSION < 14
        void PostTask(std::unique_ptr<v8::Task> task) override { DispatchTask(std::move(task)); }
        void PostNonNestableTask(std::unique_ptr<v8::Task> task) override { DispatchTask(std::move(task)); }
        void PostDelayedTask(std::unique_ptr<v8::Task> task, double delay) override { ScheduleTask(std::move(task), delay); }
        void PostNonNestableDelayedTask(std::unique_ptr<v8::Task> task, double delay) override { ScheduleTask(std::move(task), delay); }
        void PostIdleTask(std::unique_ptr<v8::IdleTask>) override {}
#endif
        bool IdleTasksEnabled() override;
        bool NonNestableTasksEnabled() const override;
        bool NonNestableDelayedTasksEnabled() const override;

    private:
#if V8_MAJOR_VERSION >= 14
        void PostTaskImpl(std::unique_ptr<v8::Task> task, const v8::SourceLocation&) override { DispatchTask(std::move(task)); }
        void PostNonNestableTaskImpl(std::unique_ptr<v8::Task> task, const v8::SourceLocation&) override { DispatchTask(std::move(task)); }
        void PostDelayedTaskImpl(std::unique_ptr<v8::Task> task, double delay, const v8::SourceLocation&) override { ScheduleTask(std::move(task), delay); }
        void PostNonNestableDelayedTaskImpl(std::unique_ptr<v8::Task> task, double delay, const v8::SourceLocation&) override { ScheduleTask(std::move(task), delay); }
#endif
        void DispatchTask(std::unique_ptr<v8::Task> task);
        void ScheduleTask(std::unique_ptr<v8::Task> task, double delayInSeconds);
        struct State;
        std::shared_ptr<State> m_state;
    };
}
