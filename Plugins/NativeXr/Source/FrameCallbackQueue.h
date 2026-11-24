#pragma once

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
        void Enqueue(Callback callback) { m_pending.emplace_back(std::move(callback)); }
        void ClearPending() { m_pending.clear(); }
        auto PendingCount() const { return m_pending.size(); }

        template<typename... Args>
        void Dispatch(Args&&... args)
        {
            m_active.swap(m_pending);
            struct ClearOnExit
            {
                std::vector<Callback>& Callbacks;
                ~ClearOnExit() { Callbacks.clear(); }
            } clear{m_active};
            for (auto& callback : m_active) callback(args...);
        }

    private:
        std::vector<Callback> m_pending, m_active;
    };
}
