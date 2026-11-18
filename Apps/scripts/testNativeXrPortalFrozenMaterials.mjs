import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_frozen_materials_validation.js", import.meta.url), "utf8");

function fixture(engine = { isWebGPU: true, compatibilityMode: false }) {
    const observers = new Set();
    const frozen = [];
    const events = [];
    const scene = {
        ready: false,
        materials: ["a", "b", "c"].map((name) => ({ freeze: () => frozen.push(name) })),
        getEngine: () => engine,
        isReady() { return this.ready; },
        onAfterRenderObservable: { add: (callback) => { observers.add(callback); return callback; }, remove: (callback) => observers.delete(callback) },
    };
    const context = vm.createContext({ __nativeArPortalStartProfile: () => events.push("setup") });
    vm.runInContext(source, context);
    const render = (frames) => { for (let i = 0; i < frames; i++) observers.forEach((callback) => callback()); };
    return { scene, context, frozen, events, observers, render, start: () => context.__nativeArPortalStartProfile(scene, (message) => events.push(message)) };
}

test("opts into the render-bundle path and labels the run", () => {
    const f = fixture();
    assert.equal(f.context.__nativeArPortalUseWebGPUFastPath, true);
    assert.equal(f.context.__nativeArPortalBenchmarkSuffix, "-frozen-materials");
    f.start();
    assert.deepEqual(f.events, ["setup"]);
});

test("freezes every material once, after sixty consecutive ready frames", () => {
    const f = fixture();
    f.start();
    f.render(200);
    assert.deepEqual(f.frozen, []);
    f.scene.ready = true;
    f.render(59);
    // A frame that is not ready restarts the count.
    f.scene.ready = false;
    f.render(1);
    f.scene.ready = true;
    f.render(59);
    assert.deepEqual(f.frozen, []);
    f.render(1);
    assert.deepEqual(f.frozen, ["a", "b", "c"]);
    assert.deepEqual(f.events, ["setup", "native-xr-portal:materials-frozen=3"]);
    assert.equal(f.observers.size, 0);
    f.render(100);
    assert.equal(f.frozen.length, 3);
});

test("rejects fallback engines, compatibility mode and snapshots", () => {
    for (const engine of [{}, { isWebGPU: true, compatibilityMode: true }, { isWebGPU: true, compatibilityMode: false, snapshotRendering: true }]) {
        const f = fixture(engine);
        assert.throws(f.start, /dynamic WebGPU rendering without snapshots/);
        assert.equal(f.observers.size, 0);
    }
});
