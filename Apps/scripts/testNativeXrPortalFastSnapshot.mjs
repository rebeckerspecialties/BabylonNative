import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const B = createRequire(import.meta.url)("babylonjs/babylon.max.js");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_fast_snapshot_validation.js", import.meta.url), "utf8");

function observable() {
    const callbacks = new Set();
    return { callbacks, add: (callback) => { callbacks.add(callback); return callback; }, remove: (callback) => callbacks.delete(callback), notify: () => [...callbacks].forEach((callback) => callback()) };
}

function fixture(engineOverrides = {}) {
    const events = [];
    const beginFrame = observable();
    const afterRender = observable();
    const engine = {
        isWebGPU: true, compatibilityMode: false, _enabled: false, _mode: B.Constants.SNAPSHOTRENDERING_STANDARD,
        onBeginFrameObservable: beginFrame,
        get snapshotRendering() { return this._enabled; },
        set snapshotRendering(value) { this._enabled = value; events.push("enabled:" + this._mode); },
        get snapshotRenderingMode() { return this._mode; },
        set snapshotRenderingMode(value) { this._mode = value; },
        ...engineOverrides,
    };
    const scene = {
        ready: true, meshes: [{}, {}, {}],
        getEngine: () => engine, isReady() { return this.ready; },
        onAfterRenderObservable: afterRender,
    };
    const context = vm.createContext({ BABYLON: { Constants: B.Constants }, __nativeArPortalStartProfile: () => events.push("setup") });
    vm.runInContext(source, context);
    // One host frame: the engine begins it, the scene renders.
    const frames = (count) => { for (let i = 0; i < count; i++) { beginFrame.notify(); afterRender.notify(); } };
    return { engine, scene, context, events, frames, beginFrame, afterRender, start: () => context.__nativeArPortalStartProfile(scene, (message) => events.push(message)) };
}

test("opts into the render-bundle path and labels the run", () => {
    const f = fixture();
    assert.equal(f.context.__nativeArPortalUseWebGPUFastPath, true);
    assert.equal(f.context.__nativeArPortalBenchmarkSuffix, "-fast-snapshot");
    f.start();
    assert.deepEqual(f.events, ["setup"]);
});

test("selects every mesh, then enables fast mode at the second frame boundary after that", () => {
    const f = fixture();
    f.start();
    f.scene.ready = false;
    f.frames(100);
    f.scene.ready = true;
    f.frames(59);
    assert.ok(f.scene.meshes.every((mesh) => mesh.alwaysSelectAsActiveMesh === undefined));
    // The sixtieth ready frame selects the meshes inside the frame; nothing is enabled there.
    f.frames(1);
    assert.ok(f.scene.meshes.every((mesh) => mesh.alwaysSelectAsActiveMesh === true));
    assert.equal(f.engine.snapshotRendering, false);
    // The next frame renders with every mesh selected and is not recorded.
    f.beginFrame.notify();
    assert.equal(f.engine.snapshotRendering, false);
    f.afterRender.notify();
    // The frame after that is recorded from its beginning.
    f.beginFrame.notify();
    assert.equal(f.engine.snapshotRendering, true);
    assert.deepEqual(f.events, ["setup", "enabled:" + B.Constants.SNAPSHOTRENDERING_FAST, "native-xr-portal:snapshot-rendering=fast:meshes=3"]);
    assert.equal(f.beginFrame.callbacks.size, 0);
    assert.equal(f.afterRender.callbacks.size, 0);
    f.frames(10);
    assert.equal(f.events.length, 3);
});

test("rejects fallback engines, compatibility mode and a snapshot that is already enabled", () => {
    for (const overrides of [{ isWebGPU: false }, { compatibilityMode: true }, { _enabled: true }]) {
        const f = fixture(overrides);
        assert.throws(f.start, /WebGPU render-bundle path/);
        assert.equal(f.afterRender.callbacks.size, 0);
    }
});
