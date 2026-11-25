#pragma once

namespace Babylon
{
    class PointerEvent
    {
    public:
        static void Initialize(Napi::Env& env)
        {
            // Synthesized events contain JS values only. Give them a shared prototype
            // without registering a native destructor for every pointer move.
            env.Global().Set("PointerEvent", Napi::Eval(env, R"JS(
                (class PointerEvent {
                    constructor(type, init) {
                        this.pointerId = 0;
                        this.pointerType = "";
                        this.button = 0;
                        this.buttons = 0;
                        this.clientX = 0;
                        this.clientY = 0;
                        this.movementX = 0;
                        this.movementY = 0;
                        this.target = null;
                        this.defaultPrevented = false;
                        if (init !== null && typeof init === "object") Object.assign(this, init);
                        this.type = typeof type === "string" ? type : "";
                    }
                    preventDefault() { this.defaultPrevented = true; }
                    stopPropagation() {}
                    stopImmediatePropagation() {}
                })
            )JS", "native-xr-pointer-event.js"));
        }
    };
} // Babylon
