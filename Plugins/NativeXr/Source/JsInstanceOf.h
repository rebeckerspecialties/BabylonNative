#pragma once

#include <napi/env.h>
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

    /**
     * Publish a class as the global `className` in a form script-side feature
     * detection accepts on every engine.
     *
     * Babylon.js only takes its WebGPU XR path when
     * `typeof XRGPUBinding === "function"` (and the same for `XRGPUSubImage`),
     * which is false for the constructible *object* JavaScriptCore produces (see
     * above), so every WebGPU XR session was refused there with "required
     * feature not compatible". Where the constructor is not a function, publish
     * a function that constructs through it and shares its prototype: `typeof`,
     * `new`, `instanceof` and prototype probes then behave as they do on engines
     * whose classes are functions, and instances are still the native objects.
     */
    inline void SetGlobalClass(Napi::Env env, const char* className, const Napi::Value& constructor)
    {
        // Decide in script: napi_typeof and `typeof` do not agree about these
        // constructors on every engine build, and `typeof` is what callers probe.
        const auto wrap = Napi::Eval(env, R"JS(
            ((nativeConstructor, name) => {
                if (typeof nativeConstructor === "function") {
                    return nativeConstructor;
                }
                const constructor = function (...args) {
                    return new nativeConstructor(...args);
                };
                Object.defineProperty(constructor, "name", { value: name, configurable: true });
                constructor.prototype = nativeConstructor.prototype;
                return constructor;
            })
        )JS", "babylon-native://plugins/NativeXr/globalClass.js").As<Napi::Function>();
        env.Global().Set(className, wrap.Call({constructor, Napi::String::New(env, className)}));
    }
}
