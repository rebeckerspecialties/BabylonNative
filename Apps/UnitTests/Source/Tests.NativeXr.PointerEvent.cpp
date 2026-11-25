#include <Babylon/AppRuntime.h>
#include <napi/napi.h>
#include "../../../Plugins/NativeXr/Source/PointerEvent.h"

#include <future>
#include <gtest/gtest.h>

TEST(NativeXrPointerEvent, RetainedMovesHaveIndependentDataAndSharedMethods)
{
    Babylon::AppRuntime runtime{};
    std::promise<bool> result;
    runtime.Dispatch([&](Napi::Env env) {
        try
        {
            Babylon::PointerEvent::Initialize(env);
            result.set_value(Napi::Eval(env, R"JS(
                (() => {
                    const init = {clientX: 7, pointerId: 42, type: "ignored"};
                    const first = new PointerEvent("pointermove", init);
                    init.clientX = 15;
                    const next = new PointerEvent("pointermove", init);
                    first.preventDefault();
                    next.stopPropagation();
                    next.stopImmediatePropagation();
                    return first !== next && first instanceof PointerEvent && first.type === "pointermove" &&
                        first.clientX === 7 && next.clientX === 15 && first.pointerId === 42 && next.buttons === 0 &&
                        first.defaultPrevented && !next.defaultPrevented && first.preventDefault === next.preventDefault &&
                        Object.hasOwn(next, "target") && next.target === null;
                })()
            )JS", "pointer-event-retention.js").As<Napi::Boolean>().Value());
        }
        catch (...) { result.set_exception(std::current_exception()); }
    });
    EXPECT_TRUE(result.get_future().get());
}
