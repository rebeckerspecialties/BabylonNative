import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_profile_ao.js", import.meta.url), "utf8");

function fixture({ pipelinePresent = true, geometryPresent = true, refreshRate = 1 } = {}) {
    let now = 0;
    const messages = [];
    const calls = [];
    const observable = () => ({
        observers: [],
        add(callback) { this.observers.push(callback); return callback; },
        remove(callback) { this.observers = this.observers.filter(value => value !== callback); },
        notify() { for (const callback of [...this.observers]) { callback(); } },
    });
    const pipeline = { name: "native-xr-portal-ssao2", totalStrength: 0.42, base: 0.76 };
    const buffer = { refreshRate };
    const camera = {};
    const scene = {
        activeCamera: camera,
        geometryBufferRenderer: geometryPresent ? { getGBuffer: () => buffer } : null,
        postProcessRenderPipelineManager: {
            supportedPipelines: pipelinePresent ? [pipeline] : [],
            detachCamerasFromRenderPipeline(name, cameras) { calls.push(["detach", name, cameras]); },
            attachCamerasToRenderPipeline(name, cameras) { calls.push(["attach", name, cameras]); },
        },
        onBeforeRenderObservable: observable(),
        onDisposeObservable: observable(),
    };
    let profileCalls = 0;
    const context = vm.createContext({
        performance: { now: () => now },
        __nativeArPortalStartProfile() { profileCalls++; },
    });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { scene, camera, buffer, messages, calls, profileCalls, frame(time) { now = time; scene.onBeforeRenderObservable.notify(); } };
}

test("starts with normal profiling and leaves AO unchanged for the baseline window", () => {
    const f = fixture();
    f.frame(29999);
    assert.equal(f.profileCalls, 1);
    assert.equal(f.buffer.refreshRate, 1);
    assert.deepEqual(f.calls, []);
    assert.match(f.messages[0], /phase=baseline:strength=0.42:base=0.76:minimumFactor=1.000/);
});

for (const missing of ["pipelinePresent", "geometryPresent"]) {
    test(`missing ${missing} does not alter the scene`, () => {
        const f = fixture({ [missing]: false });
        f.frame(90000);
        assert.deepEqual(f.calls, []);
        assert.equal(f.scene.onBeforeRenderObservable.observers.length, 0);
        assert.deepEqual(f.messages, ["native-xr-portal:ao-comparison=unavailable"]);
    });
}

test("runs baseline, bypass, restored baseline, then removes the transition observer", () => {
    const f = fixture({ refreshRate: 2 });
    f.frame(30000);
    assert.equal(f.buffer.refreshRate, 0);
    assert.equal(f.calls[0][0], "detach");
    assert.equal(f.calls[0][1], "native-xr-portal-ssao2");
    assert.equal(f.calls[0][2][0], f.camera);
    f.frame(59999);
    assert.equal(f.calls.length, 1);
    f.frame(60000);
    assert.equal(f.buffer.refreshRate, 2);
    assert.equal(f.calls[1][0], "attach");
    assert.equal(f.calls[1][2][0], f.camera);
    f.frame(90000);
    f.frame(120000);
    assert.equal(f.calls.length, 2);
    assert.equal(f.scene.onBeforeRenderObservable.observers.length, 0);
    assert.match(f.messages.at(-1), /phase=complete/);
});

test("a delayed frame still gives each comparison a full window", () => {
    const f = fixture();
    f.frame(100000);
    assert.equal(f.calls.length, 1);
    f.frame(100001);
    assert.equal(f.calls.length, 1);
    f.frame(130000);
    assert.equal(f.calls.length, 2);
});

test("disposal stops transitions without reattaching resources to a disposed scene", () => {
    const f = fixture();
    f.frame(30000);
    f.scene.onDisposeObservable.notify();
    f.frame(90000);
    assert.equal(f.calls.length, 1);
    assert.equal(f.scene.onBeforeRenderObservable.observers.length, 0);
});
