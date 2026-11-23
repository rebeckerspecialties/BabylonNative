import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_jsc_sampling.js", import.meta.url), "utf8");

test("the diagnostic helper is packaged with Playground", () => {
    const cmake = readFileSync(new URL("../Playground/CMakeLists.txt", import.meta.url), "utf8");
    assert.match(cmake, /"Scripts\/native_xr_portal_jsc_sampling\.js"/);
});

function setup(missing) {
    const calls = [], messages = [], timers = new Map();
    let nextTimer = 0, emit, dispose;
    const context = {
        __enableSamplingProfiler() { calls.push("enable"); },
        __disableSamplingProfiler() { calls.push("disable"); },
        __dumpAndClearSamplingProfilerSamples(prefix) { calls.push(["dump", prefix]); },
        __nativeArPortalStartProfile(_scene, status) { emit = status; },
        setTimeout(callback, ms) { const id = ++nextTimer; timers.set(id, { callback, ms }); return id; },
        clearTimeout(id) { timers.delete(id); },
    };
    if (missing) delete context[missing];
    vm.runInNewContext(source, context);
    context.__nativeArPortalStartProfile({ onDisposeObservable: { addOnce(callback) { dispose = callback; } } }, message => messages.push(message));
    return { context, calls, messages, timers, emit: message => emit(message), dispose: () => dispose(),
        tick() { const [id, timer] = timers.entries().next().value; timers.delete(id); timer.callback(); return timer.ms; } };
}

test("sampling waits for moving timing and excludes screenshot/warmup work", () => {
    const run = setup();
    run.emit("moving-benchmark:start={}");
    run.emit("moving-benchmark:capture={}");
    assert.equal(run.timers.size, 0);
    run.emit("moving-benchmark:timing={}");
    run.emit("moving-benchmark:timing={}");
    assert.equal(run.timers.size, 1);
    assert.equal(run.tick(), 5000);
    assert.deepEqual(run.calls, ["enable"]);
    assert.equal(run.tick(), 20000);
    assert.deepEqual(run.calls.slice(0, 2), ["enable", "disable"]);
    assert.equal(run.calls[2][0], "dump");
    assert.match(run.calls[2][1], /^bn-portal-jsc-\d+$/);
    assert.ok(run.messages.some(message => message.includes('"complete":true')));
    assert.match(run.context.__nativeArPortalBenchmarkSuffix, /-jsc-diagnostic$/);
    run.dispose();
    assert.equal(run.calls.length, 3);
});

test("disposal before sampling cancels all work", () => {
    const run = setup();
    run.emit("moving-benchmark:timing={}");
    run.dispose();
    run.emit("moving-benchmark:timing={}");
    assert.equal(run.timers.size, 0);
    assert.deepEqual(run.calls, []);
});

test("disposal during sampling retains a labeled incomplete capture", () => {
    const run = setup();
    run.emit("moving-benchmark:timing={}");
    run.tick();
    run.dispose();
    assert.equal(run.timers.size, 0);
    assert.deepEqual(run.calls.slice(0, 2), ["enable", "disable"]);
    assert.ok(run.messages.some(message => message.includes('"complete":false')));
});

for (const missing of ["__enableSamplingProfiler", "__disableSamplingProfiler", "__dumpAndClearSamplingProfilerSamples", "__nativeArPortalStartProfile"]) {
    test(`missing ${missing} fails before starting the scene`, () => {
        assert.throws(() => setup(missing), /JSC sampling/);
    });
}
