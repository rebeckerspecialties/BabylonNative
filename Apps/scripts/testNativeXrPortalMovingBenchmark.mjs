import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_moving_benchmark.js", import.meta.url), "utf8");

function setup(optimized = false, duration) {
    let now = 0;
    let before;
    let after;
    let dispose;
    let readback;
    let ready = true;
    let disposed = false;
    let statsCalls = 0;
    let timerEnabled = false;
    let threadCpuCalls = 0;
    const exits = [];
    const messages = [];
    const images = [];
    const camera = { maxZ: 1000, detachControl() {}, inputs: { clear() {} }, setTarget() {}, unfreezeProjectionMatrix() {},
        getViewMatrix() { return { m: [this.alpha, 0, this.beta, 0, 0, this.radius, 0, 0, 0, 0, 0, 0, this.alpha, this.beta, this.radius] }; } };
    const scene = {
        meshes: [], activeCamera: camera, isReady: () => ready,
        getEngine: () => ({ getRenderWidth: () => 1170, getRenderHeight: () => 2532,
            getRenderingCanvas: () => ({ getContext: () => ({ getCurrentTexture: () => ({ width: 1170, height: 2532 }) }) }) }),
        getActiveMeshes: () => ({ length: 57 }),
        onBeforeRenderObservable: { add(callback) { before = callback; } },
        onAfterRenderObservable: { add(callback) { after = callback; } },
        onDisposeObservable: { add(callback) { dispose = callback; } },
    };
    const context = vm.createContext({
        console: { log() {} }, performance: { now: () => now },
        __nativeArPortalBenchmarkDurationMs: duration,
        __nativeValidationSetFrameTimerEnabled(value) { timerEnabled = value; },
        navigator: { gpu: { _backendStats() { statsCalls++; return { estimatedGpuMemoryBytes: 123 }; } } },
        BABYLON: {
            Vector3: class { add() { return this; } scale() { return this; } },
            Plane: { prototype: { dotCoordinate: optimized ? function () { return this._x; } : function () { return this.x; } } },
            SceneInstrumentation: class {
                frameTimeCounter = { current: 8 };
                activeMeshesEvaluationTimeCounter = { current: 1 };
                renderTimeCounter = { current: 6 };
                renderTargetsRenderTimeCounter = { current: 0.5 };
                dispose() { disposed = true; }
            },
        },
        TestUtils: {
            getCurrentThreadCpuTime() { threadCpuCalls++; return now / 2; },
            exit(code) { exits.push(code); },
            getOutputDirectory: () => "/tmp/benchmark",
            getFrameBufferData(callback) { readback = callback; },
            writePNG(_data, _width, _height, path) { images.push(path); },
        },
    });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return {
        context, camera, messages, images, exits,
        tick(ms = 16) { now += ms; before(); after(); },
        capture(data = new Uint8Array(4), width = 1, height = 1) { assert.ok(readback); const callback = readback; readback = undefined; callback(data, width, height); },
        ready(value) { ready = value; },
        dispose() { dispose(); return disposed; },
        get pending() { return !!readback; },
        get statsCalls() { return statsCalls; },
        get timerEnabled() { return timerEnabled; },
        get threadCpuCalls() { return threadCpuCalls; },
    };
}

for (const optimized of [false, true]) {
    test(`${optimized ? "optimized" : "control"} moving run separates readiness, readback, warmup and timing`, () => {
        const run = setup(optimized);
        assert.equal(run.timerEnabled, true);
        assert.equal(run.context.__nativeArPortalForceNonXrStress, true);
        assert.equal(run.context.__nativeArPortalSSAOUsePrePass, true);
        run.ready(false);
        for (let i = 0; i < 200; i++) run.tick();
        assert.equal(run.pending, false);
        run.ready(true);
        run.tick();
        assert.equal(run.pending, true);
        const pose = [run.camera.alpha, run.camera.beta, run.camera.radius];
        for (let i = 0; i < 50; i++) run.tick(100);
        assert.deepEqual([run.camera.alpha, run.camera.beta, run.camera.radius], pose);
        assert.equal(run.statsCalls, 0);
        run.capture();
        while (run.images.length < 4) {
            run.tick();
            if (run.pending) run.capture();
        }
        assert.equal(new Set(run.images).size, 4);
        assert.ok(run.images.every(path => path.includes(optimized ? "optimized" : "control")));
        while (run.statsCalls === 0) run.tick();
        const initialPose = [run.camera.alpha, run.camera.beta, run.camera.radius];
        run.tick(1000);
        assert.notDeepEqual([run.camera.alpha, run.camera.beta, run.camera.radius], initialPose);
        while (!run.messages.some(message => message.startsWith("moving-benchmark:result="))) run.tick(1000);
        const results = run.messages.filter(message => message.startsWith("moving-benchmark:result="));
        const result = JSON.parse(results[0].split("=")[1]);
        assert.equal(result.durationMs, 45000);
        assert.equal(result.frames, 45);
        assert.equal(result.cpuMs, 8);
        assert.equal(result.threadCpuMs, 22500);
        assert.equal(run.threadCpuCalls, result.motion.length + 2); // start, motion receipts, end; never per draw/frame
        assert.ok(result.motion.every(sample => sample.threadCpuMs === sample.elapsedMs / 2));
        assert.equal(result.activeCpuMs, 1);
        assert.equal(result.renderCpuMs, 6);
        assert.equal(result.targetsCpuMs, 0.5);
        assert.equal(result.activeMin, 57);
        assert.equal(result.activeMax, 57);
        assert.ok(result.motion.length >= 9);
        assert.ok(result.motion.every((sample, index) => sample.frames > 0 && (index === 0 || sample.frames > result.motion[index - 1].frames)));
        assert.notDeepEqual(result.motion[0].view, result.motion[1].view);
        assert.equal(run.statsCalls, 2);
        const completedPose = [run.camera.alpha, run.camera.beta, run.camera.radius];
        run.tick(1000);
        assert.notDeepEqual([run.camera.alpha, run.camera.beta, run.camera.radius], completedPose);
        assert.equal(run.messages.filter(message => message.startsWith("moving-benchmark:result=")).length, 1);
        assert.equal(run.dispose(), true);
    });
}

test("benchmark does not install per-call probes or freeze scene/material visibility", () => {
    assert.doesNotMatch(source, /freezeActiveMeshes|\.freeze\(|profile_calls|profileCalls|Object\.defineProperty/);
    assert.match(source, /camera\.unfreezeProjectionMatrix\(\)/);
});

test("unsupported and absent CPU timers do not turn frame waits into CPU measurements", () => {
    for (const absent of [true, false]) {
        const run = setup(true, 1000);
        if (absent) delete run.context.TestUtils.getCurrentThreadCpuTime;
        else run.context.TestUtils.getCurrentThreadCpuTime = () => undefined;
        while (run.statsCalls === 0) { run.tick(); if (run.pending) run.capture(); }
        run.tick(1000);
        const result = JSON.parse(run.messages.find(message => message.startsWith("moving-benchmark:result=")).split("=")[1]);
        assert.equal(result.threadCpuMs, undefined);
    }
});

test("capture rejects stale logical dimensions rather than silently writing a cropped image", () => {
    for (const [length, width, height] of [[16, 1, 1], [4, 2, 2], [0, 0, 0]]) {
        const run = setup(true);
        while (!run.pending) run.tick();
        assert.throws(() => run.capture(new Uint8Array(length), width, height), /dimensions do not match/);
        assert.equal(run.images.length, 0);
        assert.equal(run.statsCalls, 0);
    }
    const run = setup(true);
    while (!run.pending) run.tick();
    run.capture(new Uint8Array(24), 3, 2);
    const capture = JSON.parse(run.messages.find(message => message.startsWith("moving-benchmark:capture=")).split("=")[1]);
    assert.equal(capture.width, 3);
    assert.equal(capture.height, 2);
    assert.equal(run.images.length, 1);
});

test("capture-only exits after four ready poses without entering the timing window", () => {
    const run = setup(true);
    run.context.__nativeArPortalBenchmarkCaptureOnly = true;
    while (run.images.length < 4) {
        run.tick();
        if (run.pending) run.capture();
    }
    for (let i = 0; i < 600; i++) run.tick();
    assert.deepEqual(run.exits, [0]);
    assert.equal(run.statsCalls, 0);
    assert.equal(run.messages.some(message => message.startsWith("moving-benchmark:result=")), false);
});

test("requested backing size must match the canvas texture before capture or timing", () => {
    const run = setup(true);
    run.context.__nativeArPortalBenchmarkExpectedSize = [3, 2];
    for (let i = 0; i < 179; i++) run.tick();
    assert.throws(() => run.tick(), /requested render size/);
    assert.equal(run.pending, false);
    assert.equal(run.images.length, 0);
    assert.equal(run.statsCalls, 0);
});

test("presentation capture size may differ from the verified render backing", () => {
    const run = setup(true);
    run.context.__nativeArPortalBenchmarkExpectedSize = [1170, 2532];
    while (!run.pending) run.tick();
    run.capture(new Uint8Array(24), 3, 2);
    assert.equal(run.images.length, 1);
    assert.ok(run.messages.includes("moving-benchmark:backing=" + JSON.stringify({ width: 1170, height: 2532 })));
    assert.equal(run.statsCalls, 0);
});

test("bounded timing exits only after producing a result", () => {
    const run = setup(true, 1000);
    run.context.__nativeArPortalBenchmarkExitOnComplete = true;
    while (run.statsCalls === 0) {
        run.tick();
        if (run.pending) run.capture();
    }
    assert.deepEqual(run.exits, []);
    run.tick(1000);
    assert.deepEqual(run.exits, [0]);
    assert.ok(run.messages.some(message => message.startsWith("moving-benchmark:result=")));
    run.tick(1000);
    assert.deepEqual(run.exits, [0]);
});

test("long profiling duration keeps the camera moving beyond the ordinary timing window", () => {
    const run = setup(true, 180000);
    assert.equal(run.context.__nativeArPortalExitDelayMs, 225000);
    while (run.statsCalls === 0) {
        run.tick();
        if (run.pending) run.capture();
    }
    for (let i = 0; i < 60; i++) run.tick(1000);
    assert.equal(run.messages.some(message => message.startsWith("moving-benchmark:result=")), false);
    const pose = [run.camera.alpha, run.camera.beta, run.camera.radius];
    run.tick(1000);
    assert.notDeepEqual([run.camera.alpha, run.camera.beta, run.camera.radius], pose);
    for (let i = 61; i < 180; i++) run.tick(1000);
    const result = JSON.parse(run.messages.find(message => message.startsWith("moving-benchmark:result=")).split("=")[1]);
    assert.equal(result.durationMs, 180000);
    assert.equal(result.frames, 180);
    assert.equal(result.motion.length, 12);
    assert.ok(result.motion[0].elapsedMs > 45000);
    assert.notDeepEqual(result.motion[0].view, result.motion[1].view);
    assert.ok(run.messages.filter(message => message.startsWith("moving-benchmark:motion=")).length > 12);
    const windows = run.messages.filter(message => message.startsWith("moving-benchmark:window="))
        .map(message => JSON.parse(message.split("=")[1]));
    assert.equal(windows.length, 6);
    assert.ok(windows.every(window => window.frames === 30 && window.durationMs === 30000 && window.cpuMs === 8));
    assert.equal(windows.reduce((sum, window) => sum + window.frames, 0), result.frames);
});

test("window reporting includes the final short window without counting capture or warmup", () => {
    const run = setup(true, 45000);
    while (run.statsCalls === 0) {
        run.tick();
        if (run.pending) run.capture();
    }
    assert.equal(run.messages.some(message => message.startsWith("moving-benchmark:window=")), false);
    for (let i = 0; i < 45; i++) run.tick(1000);
    const windows = run.messages.filter(message => message.startsWith("moving-benchmark:window="))
        .map(message => JSON.parse(message.split("=")[1]));
    assert.deepEqual(windows.map(window => window.frames), [30, 15]);
    assert.ok(windows.every(window => window.fps === 1 && window.maxMs === 1000));
});
