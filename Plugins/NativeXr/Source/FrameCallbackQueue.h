#pragma once

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

namespace Babylon::Plugins
{
    // One JS dispatch at a time. Requests made from a running callback are
    // pending for the next XR frame; ending a session clears only those.
    template<typename Callback>
    class FrameCallbackQueue
    {
    public:
        void Enqueue(Callback callback)
        {
            m_pending.emplace_back(std::move(callback));
            m_pendingCount.store(m_pending.size(), std::memory_order_relaxed);
        }
        void ClearPending()
        {
            m_pending.clear();
            m_pendingCount.store(0, std::memory_order_relaxed);
        }
        // Only this snapshot is read on the render thread. Callback storage and
        // destruction stay on the JS thread; publication of the frame task is separate.
        size_t PendingCount() const { return m_pendingCount.load(std::memory_order_relaxed); }

        template<typename... Args>
        void Dispatch(Args&&... args)
        {
            m_active.swap(m_pending);
            m_pendingCount.store(0, std::memory_order_relaxed);
            struct ClearOnExit
            {
                std::vector<Callback>& Callbacks;
                ~ClearOnExit() { Callbacks.clear(); }
            } clear{m_active};
            for (auto& callback : m_active) callback(args...);
        }

    private:
        std::vector<Callback> m_pending, m_active;
        std::atomic<size_t> m_pendingCount{};
    };
}
