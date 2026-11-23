import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_profile_materials.js", import.meta.url), "utf8");
function observable() {
    const callbacks = new Set();
    return { add(fn) { callbacks.add(fn); return fn; }, remove(fn) { callbacks.delete(fn); }, notify() { callbacks.forEach(fn => fn()); } };
}
function material(frozen = false) {
    return { isFrozen: frozen, freezes: 0, unfreezes: 0,
        freeze() { this.isFrozen = true; this.freezes++; }, unfreeze() { this.isFrozen = false; this.unfreezes++; } };
}
function fixture(materials) {
    let now = 0;
    let priorCalls = 0;
    const messages = [];
    const scene = { materials, onBeforeRenderObservable: observable(), onDisposeObservable: observable() };
    const context = vm.createContext({ performance: { now: () => now }, __nativeArPortalStartProfile: () => priorCalls++ });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { scene, messages, priorCalls, frame(time) { now = time; scene.onBeforeRenderObservable.notify(); } };
}
test("zero eligible materials preserves the profiling chain", () => {
    const f = fixture([null, {}, { freeze() {} }]);
    f.frame(0);
    f.frame(30000);
    f.frame(60000);
    assert.equal(f.priorCalls, 1);
    assert.equal(f.messages.length, 3);
    assert.ok(f.messages.every(message => message.endsWith(":materials=0")));
});
test("one and many materials follow A/B/A without per-frame invalidation", () => {
    for (const count of [1, 3]) {
        const materials = Array.from({ length: count }, () => material());
        const f = fixture(materials);
        for (const time of [0, 29999]) f.frame(time);
        assert.ok(materials.every(value => value.freezes === 0));
        for (const time of [30000, 30001, 59999]) f.frame(time);
        assert.ok(materials.every(value => value.freezes === 1 && value.isFrozen));
        for (const time of [60000, 60001, 90000]) f.frame(time);
        assert.ok(materials.every(value => value.unfreezes === 1 && !value.isFrozen));
        assert.equal(f.messages.length, 3);
    }
});
test("originally frozen and newly added materials remain untouched", () => {
    const original = material(true);
    const added = material();
    const f = fixture([original]);
    f.scene.materials.push(added);
    f.frame(30000);
    f.frame(60000);
    assert.equal(original.isFrozen, true);
    assert.equal(original.freezes + original.unfreezes + added.freezes + added.unfreezes, 0);
});
test("disposal restores only a comparison that froze materials", () => {
    for (const time of [0, 30000, 60000]) {
        const value = material();
        const f = fixture([value]);
        f.frame(time);
        f.scene.onDisposeObservable.notify();
        f.frame(30000);
        assert.equal(value.isFrozen, false);
        assert.equal(value.unfreezes, time === 30000 ? 1 : 0);
    }
});
