import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_profile.js", import.meta.url), "utf8");

function fixture(supported = true) {
    let now = 0;
    const messages = [];
    const observable = () => ({
        observers: [],
        add(callback) { this.observers.push(callback); return callback; },
        remove(callback) { this.observers = this.observers.filter(value => value !== callback); },
        notify() { for (const callback of [...this.observers]) { callback(); } },
    });
    const scene = { onAfterRenderObservable: observable(), onDisposeObservable: observable() };
    let instrumentation;
    class SceneInstrumentation {
        constructor() {
            instrumentation = this;
            for (const name of ["frame", "activeMeshesEvaluation", "renderTargetsRender", "render", "particlesRender", "animations"]) {
                this[`${name}TimeCounter`] = { current: 2 };
            }
        }
        dispose() { this.disposed = true; }
    }
    const context = vm.createContext({ performance: { now: () => now }, BABYLON: supported ? { SceneInstrumentation } : {} });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { scene, messages, instrumentation, frame(time) { now = time; scene.onAfterRenderObservable.notify(); } };
}

test("unsupported instrumentation leaves rendering untouched", () => {
    const f = fixture(false);
    assert.equal(f.scene.onAfterRenderObservable.observers.length, 0);
    assert.deepEqual(f.messages, ["native-xr-portal:cpu-phases=unavailable"]);
});

test("no frames produce no measurement", () => {
    assert.deepEqual(fixture().messages, []);
});

test("one frame reports CPU wall time, not GPU duration", () => {
    const f = fixture();
    f.frame(2000);
    assert.deepEqual(f.messages, ["native-xr-portal:cpu-phases:frames=1:frameMs=2.000:activeMeshesMs=2.000:renderTargetsMs=2.000:drawMs=2.000:particlesMs=2.000:animationsMs=2.000"]);
});

test("averages only frames in the current window", () => {
    const f = fixture();
    f.frame(1000);
    f.instrumentation.frameTimeCounter.current = 6;
    f.frame(2000);
    assert.match(f.messages[0], /:frames=2:frameMs=4.000:/);
    f.instrumentation.frameTimeCounter.current = 10;
    f.frame(4000);
    assert.match(f.messages[1], /:frames=1:frameMs=10.000:/);
});

test("scene disposal removes sampling and disposes instrumentation", () => {
    const f = fixture();
    f.scene.onDisposeObservable.notify();
    f.frame(2000);
    assert.equal(f.instrumentation.disposed, true);
    assert.equal(f.scene.onAfterRenderObservable.observers.length, 0);
    assert.deepEqual(f.messages, []);
});
