#pragma once

#include <atomic>

namespace Babylon::Embedding
{
    // One queued/running callback and at most one newer display opportunity.
    // A stopped gate is never reused; resume creates a new lifetime.
    class AnimationFrameGate
    {
    public:
        bool Request(bool rememberBusyTick = true)
        {
            auto state = m_state.load();
            for (;;)
            {
                if (state == State::Stopped || state == State::Queued || state == State::Pending ||
                    (state == State::Running && !rememberBusyTick))
                {
                    return false;
                }
                const auto next = state == State::Idle ? State::Queued : State::Pending;
                if (m_state.compare_exchange_weak(state, next))
                {
                    return next == State::Queued;
                }
            }
        }

        bool Begin()
        {
            auto expected = State::Queued;
            return m_state.compare_exchange_strong(expected, State::Running);
        }

        bool Complete()
        {
            auto state = m_state.load();
            while (state == State::Running || state == State::Pending)
            {
                const auto next = state == State::Pending ? State::Queued : State::Idle;
                if (m_state.compare_exchange_weak(state, next))
                {
                    return next == State::Queued;
                }
            }
            return false;
        }

        void Abort()
        {
            auto state = m_state.load();
            while (state != State::Stopped && !m_state.compare_exchange_weak(state, State::Idle)) {}
        }

        void Stop()
        {
            m_state.store(State::Stopped);
        }

    private:
        enum class State { Idle, Queued, Running, Pending, Stopped };
        std::atomic<State> m_state{State::Idle};
    };
}
