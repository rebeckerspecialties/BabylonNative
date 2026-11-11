#pragma once

#include <napi/napi.h>

namespace Babylon
{
    /**
     * Is `value` an instance of the class registered as the global `className`?
     *
     * JavaScriptCore exposes a class created by `Napi::ObjectWrap::DefineClass`
     * as a constructible JS *object* rather than a JS function: `typeof` reports
     * "object" and `Napi::Value::IsFunction()` is false, even though `instanceof`
     * works on it. So the natural spelling -- guard with `IsFunction()`, then call
     * `Napi::Object::InstanceOf()`, which only accepts a `Napi::Function` --
     * rejects every instance under JavaScriptCore, which is what the iOS, tvOS,
     * and macOS runtimes use. `napi_instanceof` accepts any constructible value
     * and matches what `instanceof` does in script, on every engine.
     */
    inline bool IsInstanceOfGlobalClass(const Napi::Object& value, const char* className)
    {
        const Napi::Env env{value.Env()};
        const auto constructor = env.Global().Get(className);
        if (constructor.IsUndefined() || constructor.IsNull())
        {
            return false;
        }

        bool result{false};
        return napi_instanceof(env, value, constructor, &result) == napi_ok && result;
    }
}
