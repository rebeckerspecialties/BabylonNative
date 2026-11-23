import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_profile_calls.js", import.meta.url), "utf8");

function fixture(supported = true) {
    let now = 0;
    let previousCalls = 0;
    const messages = [];
    const observable = () => ({
        observers: [],
        add(callback) { this.observers.push(callback); return callback; },
        remove(callback) { this.observers = this.observers.filter(value => value !== callback); },
        notify() { for (const callback of [...this.observers]) { callback(); } },
    });
    class UniformBuffer {
        updateUniform(value) { now += 2; return value; }
        update(value) { now += 3; return this.updateUniform(value); }
    }
    const original = Object.getOwnPropertyDescriptor(UniformBuffer.prototype, "update");
    const buffer = new UniformBuffer();
    const enginePrototype = { _draw(value) { now += 1; return buffer.update(value); } };
    const engine = Object.create(enginePrototype);
    const scene = {
        getEngine: () => engine,
        onBeforeRenderObservable: observable(), onAfterRenderObservable: observable(), onDisposeObservable: observable()
    };
    const context = vm.createContext({
        performance: { now: () => now }, BABYLON: supported ? { UniformBuffer } : {},
        __nativeArPortalStartProfile() { previousCalls++; }
    });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return {
        scene, engine, buffer, UniformBuffer, original, messages, previousCalls,
        advance(ms) { now += ms; },
        frame(callback = () => engine._draw(42)) {
            scene.onBeforeRenderObservable.notify();
            try { return callback(); } finally { scene.onAfterRenderObservable.notify(); }
        },
        reports() { return messages.filter(x => x.startsWith("native-xr-portal:cpu-calls:{")).map(x => JSON.parse(x.slice(x.indexOf("{")))); }
    };
}

test("chains the existing profiler and reports no measurement before a frame", () => {
    const f = fixture();
    assert.equal(f.previousCalls, 1);
    assert.equal(f.reports().length, 0);
});

test("missing optional Babylon classes and cache objects do not prevent profiling", () => {
    const f = fixture(false);
    f.advance(3000);
    assert.equal(f.frame(), 42);
    assert.equal(f.reports()[0].metrics.length, 1);
});

test("nested inclusive and self times preserve receiver, arguments and return values", () => {
    const f = fixture();
    f.advance(3000);
    const value = {};
    assert.equal(f.frame(() => f.engine._draw(value)), value);
    const report = f.reports()[0];
    assert.equal(report.frames, 1);
    const byName = Object.fromEntries(report.metrics.map(metric => [metric.name, metric]));
    assert.deepEqual(byName["engine._draw"], { name: "engine._draw", calls: 1, inclusiveMs: 6, selfMs: 1 });
    assert.deepEqual(byName["UniformBuffer.update"], { name: "UniformBuffer.update", calls: 1, inclusiveMs: 5, selfMs: 3 });
    assert.deepEqual(byName["UniformBuffer.updateUniform"], { name: "UniformBuffer.updateUniform", calls: 1, inclusiveMs: 2, selfMs: 2 });
});

test("samples one in thirty frames and resets the report window", () => {
    const f = fixture();
    for (let i = 0; i < 31; i++) { f.frame(); }
    f.advance(3000);
    f.frame();
    assert.equal(f.reports()[0].frames, 2);
    assert.equal(f.reports()[0].metrics.find(x => x.name === "engine._draw").calls, 1);
    f.advance(3000);
    for (let i = 0; i < 29; i++) { f.frame(); }
    assert.equal(f.reports()[1].frames, 1);
});

test("throws the original error and balances nested accounting after failure", () => {
    const f = fixture();
    const error = new Error("uniform upload failed");
    f.buffer.updateUniform = () => { f.advance(2); throw error; };
    f.advance(3000);
    assert.throws(() => f.frame(), value => value === error);
    const metrics = f.reports()[0].metrics;
    assert.equal(metrics.find(x => x.name === "engine._draw").selfMs, 1);
    assert.equal(metrics.find(x => x.name === "UniformBuffer.update").selfMs, 5);
});

test("disposal restores descriptors and inherited methods without clobbering later overrides", () => {
    const f = fixture();
    const replacement = () => 9;
    f.engine._draw = replacement;
    f.scene.onDisposeObservable.notify();
    assert.deepEqual(Object.getOwnPropertyDescriptor(f.UniformBuffer.prototype, "update"), f.original);
    assert.equal(f.engine._draw, replacement);
    assert.equal(f.scene.onBeforeRenderObservable.observers.length, 0);
    assert.equal(f.scene.onAfterRenderObservable.observers.length, 0);
    const g = fixture();
    g.scene.onDisposeObservable.notify();
    assert.equal(Object.hasOwn(g.engine, "_draw"), false);
});
