import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { runInNewContext } from 'node:vm';
import test from 'node:test';

const source = readFileSync(new URL('../Playground/Scripts/validation_native.js', import.meta.url), 'utf8');
const start = source.indexOf('    function processLoadedScene(');
const end = source.indexOf('    function loadPlayground(', start);
assert(start >= 0 && end > start);
const loadedSceneSource = source.slice(start, end);

function completeLoad(active, sameTest) {
    const testCase = {};
    const oldScene = {};
    let disposed = 0;
    let processed = 0;
    const loaded = { dispose() { disposed++; } };
    const context = {
        currentValidationTest: sameTest ? testCase : {}, currentScene: oldScene,
        processCurrentScene() { processed++; },
    };
    const done = () => {};
    done.isActive = () => active;
    runInNewContext(loadedSceneSource, context);
    context.processLoadedScene(testCase, loaded, done);
    return { disposed, processed, accepted: context.currentScene === loaded, preserved: context.currentScene === oldScene };
}

test('a resolved active scene enters validation', () => {
    assert.deepEqual(completeLoad(true, true), { disposed: 0, processed: 1, accepted: true, preserved: false });
});
test('a late scene cannot replace the next test scene', () => {
    assert.deepEqual(completeLoad(false, false), { disposed: 1, processed: 0, accepted: false, preserved: true });
});
test('a timed-out run cannot replace a repeated run of the same test', () => {
    assert.deepEqual(completeLoad(false, true), { disposed: 1, processed: 0, accepted: false, preserved: true });
});
test('validation does not shorten an already-running Babylon readiness timer', () => {
    const require = createRequire(import.meta.url);
    const BABYLON = require('babylonjs');
    const engine = new BABYLON.NullEngine();
    const scene = new BABYLON.Scene(engine);
    let ready = 0;
    let timedOut = 0;
    try {
        const policy = source.match(/currentScene\.onReadyTimeoutDuration = (\d+);/);
        assert(policy);
        scene.onReadyTimeoutDuration = Number(policy[1]);
        scene._timeoutChecksStartTime = BABYLON.PrecisionDate.Now - 60000;
        scene.isReady = () => true;
        scene.onReadyTimeoutObservable.add(() => timedOut++);
        scene.executeWhenReady(() => ready++, true);
        assert.equal(ready, 1);
        assert.equal(timedOut, 0);
    } finally {
        scene.dispose();
        engine.dispose();
    }
});
