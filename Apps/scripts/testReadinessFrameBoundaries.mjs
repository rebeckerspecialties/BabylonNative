import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import test from 'node:test';

const source = readFileSync(new URL('../Playground/Scripts/validation_native.js', import.meta.url), 'utf8');
const start = source.indexOf('    function startSceneReadinessRenderPump(');
const end = source.indexOf('    function installWebGPUPreviousWorldBufferFrameOrderShim(', start);
assert(start >= 0 && end > start);
const pumpSource = source.slice(start, end);

function runPump(isWebGPU, renderThrows = false) {
    const calls = [];
    const pending = [];
    const engine = { isWebGPU, beginFrame() { calls.push('begin'); }, endFrame() { calls.push('end'); } };
    const scene = {
        activeCamera: {}, getEngine() { return engine; },
        render(updateCameras, ignoreAnimations) {
            assert.equal(updateCameras, true);
            assert.equal(ignoreAnimations, true);
            calls.push('render');
            if (renderThrows) throw new Error('render failed');
        },
    };
    const context = {
        setTimeout(callback) { pending.push(callback); }, console: { log() {}, error() {} },
        logNativeWebGPUStats() {}, findUnsupportedNativeWebGPUEffectInSceneFamily() {},
        formatLogArgument: String,
    };
    runInNewContext(pumpSource, context);
    const pump = context.startSceneReadinessRenderPump(scene, 'test', () => calls.push('failed'));
    pending.shift()();
    pump.stop();
    for (const callback of pending) callback();
    return calls;
}

test('WebGPU readiness frames are submitted independently', () => {
    assert.deepEqual(runPump(true), ['begin', 'render', 'end']);
});
test('WebGPU frame cleanup runs even when readiness rendering throws', () => {
    assert.deepEqual(runPump(true, true), ['begin', 'render', 'end', 'failed']);
});
test('legacy readiness rendering is unchanged', () => {
    assert.deepEqual(runPump(false), ['render']);
});

test('Simple refraction preserves its render-observer animation frame', () => {
    const config = JSON.parse(readFileSync(new URL('../Playground/Scripts/config.json', import.meta.url), 'utf8'));
    const fixture = config.tests.find(({ title }) => title === 'Simple refraction');
    // #22KZUW#652 rotates both spheres in registerBeforeRender, even when
    // scene.render ignores animations. Extra readiness frames change the image.
    assert.equal(fixture.playgroundId, '#22KZUW#652');
    assert.equal(fixture.renderReadinessPump, false);
});
