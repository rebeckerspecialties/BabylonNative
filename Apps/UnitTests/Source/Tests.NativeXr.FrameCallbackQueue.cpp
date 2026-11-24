#include "../../../Plugins/NativeXr/Source/FrameCallbackQueue.h"
#include <gtest/gtest.h>
#include <functional>
#include <memory>
#include <stdexcept>

namespace
{
    using Queue = Babylon::Plugins::FrameCallbackQueue<std::function<void(int)>>;

    TEST(XrFrameCallbackQueue, EnqueuesDuringDispatchWaitForAnotherFrame)
    {
        Queue queue;
        std::vector<int> times;
        queue.Dispatch(0);
        queue.Enqueue([&](int t) {
            times.push_back(t);
            queue.Enqueue([&](int next) { times.push_back(next); });
        });
        queue.Enqueue([&](int t) { times.push_back(t); });
        queue.Dispatch(1);
        EXPECT_EQ(times, (std::vector<int>{1, 1}));
        queue.Dispatch(2);
        EXPECT_EQ(times, (std::vector<int>{1, 1, 2}));
        EXPECT_EQ(queue.PendingCount(), 0u);
    }

    TEST(XrFrameCallbackQueue, ExceptionReleasesActiveReferencesButNotNewRequests)
    {
        Queue queue;
        auto token = std::make_shared<int>(0);
        std::weak_ptr<int> ref = token;
        queue.Enqueue([&, token = std::move(token)](int) {
            queue.Enqueue([](int) {});
            throw std::runtime_error("callback");
        });
        EXPECT_THROW(queue.Dispatch(1), std::runtime_error);
        EXPECT_TRUE(ref.expired());
        EXPECT_EQ(queue.PendingCount(), 1u);
        queue.ClearPending();
        queue.Dispatch(2);
    }

    TEST(XrFrameCallbackQueue, ClearingPendingDuringDispatchDoesNotInvalidateIteration)
    {
        Queue queue;
        int invoked{};
        queue.Enqueue([&](int) {
            queue.Enqueue([](int) { FAIL() << "Cleared request"; });
            queue.ClearPending();
        });
        queue.Enqueue([&](int) { ++invoked; });
        queue.Dispatch(1);
        queue.Dispatch(2);
        EXPECT_EQ(invoked, 1);
    }
}
