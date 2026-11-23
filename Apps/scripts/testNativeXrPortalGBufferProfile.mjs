import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_profile_gbuffer.js", import.meta.url), "utf8");
function observable() {
    const callbacks = new Set();
    return { add(fn) { callbacks.add(fn); return fn; }, remove(fn) { callbacks.delete(fn); }, notify(value) { callbacks.forEach(fn => fn(value)); } };
}
function fixture(available = true) {
    let now = 0;
    let priorCalls = 0;
    let size = { width: 4239, height: 2384 };
    const resizes = [];
    const gbuffer = { getSize: () => size, resize(value) { size = value; resizes.push({ ...value }); } };
    const scene = { geometryBufferRenderer: available ? { getGBuffer: () => gbuffer } : null,
        onBeforeCameraRenderObservable: observable(), onDisposeObservable: observable() };
    const messages = [];
    const context = vm.createContext({ performance: { now: () => now }, __nativeArPortalStartProfile: () => priorCalls++ });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { scene, resizes, messages, priorCalls, frame(time, dimensions = { width: 1242, height: 2688 }) {
        now = time;
        scene.onBeforeCameraRenderObservable.notify({ outputRenderTarget: dimensions && { getSize: () => dimensions } });
    } };
}
test("missing geometry preserves prior profiling without allocating targets", () => {
    const f = fixture(false);
    f.frame(45000);
    assert.equal(f.priorCalls, 1);
    assert.deepEqual(f.resizes, []);
    assert.deepEqual(f.messages, ["native-xr-portal:gbuffer-comparison=unavailable"]);
});
test("no output target leaves ordinary and parent cameras unchanged", () => {
    const f = fixture();
    f.frame(45000, null);
    assert.deepEqual(f.resizes, []);
    assert.deepEqual(f.messages, []);
});
test("compares original and XR sizes twice without per-frame reallocation", () => {
    const f = fixture();
    for (const time of [0, 29999, 30000, 30001, 59999, 60000, 89999, 90000, 120000]) f.frame(time);
    assert.deepEqual(f.resizes, [{ width: 1242, height: 2688 }, { width: 4239, height: 2384 }, { width: 1242, height: 2688 }]);
    assert.equal(f.messages.length, 4);
});
test("tracks rotated XR targets without altering pixel count", () => {
    const f = fixture();
    f.frame(30000);
    f.frame(31000, { width: 2688, height: 1242 });
    f.frame(32000, { width: 2688, height: 1242 });
    assert.deepEqual(f.resizes, [{ width: 1242, height: 2688 }, { width: 2688, height: 1242 }]);
});
test("invalid target dimensions fail before resizing", () => {
    for (const dimensions of [{ width: 0, height: 1 }, { width: 1.5, height: 2 }, { width: 1, height: NaN }]) {
        const f = fixture();
        assert.throws(() => f.frame(30000, dimensions), /positive integer XR render-target size/);
        assert.deepEqual(f.resizes, []);
    }
});
test("disposal removes the comparison observer", () => {
    const f = fixture();
    f.scene.onDisposeObservable.notify();
    f.frame(45000);
    assert.deepEqual(f.resizes, []);
});
