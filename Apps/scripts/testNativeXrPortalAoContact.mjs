import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_ao_contact_validation.js", import.meta.url), "utf8");
function observable() {
    const callbacks = new Set();
    return { add(fn) { callbacks.add(fn); return fn; }, remove(fn) { callbacks.delete(fn); }, notify() { callbacks.forEach(fn => fn()); } };
}
function fixture(available = true) {
    let now = 0;
    let priorCalls = 0;
    const pipeline = { name: "native-xr-portal-ssao2", totalStrength: 0.42, epsilon: 0.04, samples: 8 };
    const scene = { postProcessRenderPipelineManager: { supportedPipelines: available ? [pipeline] : [] },
        onBeforeRenderObservable: observable(), onDisposeObservable: observable() };
    const messages = [];
    const context = vm.createContext({ performance: { now: () => now }, __nativeArPortalStartProfile: () => priorCalls++ });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { pipeline, scene, messages, priorCalls, frame(time) { now = time; scene.onBeforeRenderObservable.notify(); } };
}
test("no AO leaves the scene unchanged", () => {
    const f = fixture(false);
    f.frame(20000);
    assert.equal(f.priorCalls, 1);
    assert.deepEqual(f.messages, ["native-xr-portal:ao-contact=unavailable"]);
    assert.equal(f.pipeline.totalStrength, 0.42);
});
test("compares current, off and contact shading without changing samples", () => {
    const f = fixture();
    for (const [time, strength, epsilon] of [[0, 0.42, 0.04], [14999, 0.42, 0.04], [15000, 0, 0.01],
        [30000, 1.2, 0.01], [45000, 0, 0.01], [60000, 1.2, 0.01], [90000, 1.2, 0.01]]) {
        f.frame(time);
        assert.equal(f.pipeline.totalStrength, strength);
        assert.equal(f.pipeline.epsilon, epsilon);
        assert.equal(f.pipeline.samples, 8);
    }
    assert.equal(f.messages.length, 5);
});
test("disposal removes pending comparison", () => {
    const f = fixture();
    f.scene.onDisposeObservable.notify();
    f.frame(30000);
    assert.equal(f.pipeline.totalStrength, 0.42);
    assert.deepEqual(f.messages, []);
});
