import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import test from "node:test";

const require = createRequire(import.meta.url);
const { PostProcessManager, Scene } = require(process.env.BABYLONJS_BUNDLE ? resolve(process.env.BABYLONJS_BUNDLE) : "babylonjs/babylon.max.js");

function fixture() {
    const binds = [];
    const engine = {
        bindFramebuffer: (...args) => binds.push(args),
        restoreDefaultFramebuffer: () => binds.push("screen"),
        setDepthBuffer() {}, setDepthWrite() {}, setAlphaMode() {},
    };
    const camera = { _postProcesses: [{ apply: () => null }] };
    const scene = Object.assign(Object.create(Scene.prototype), { _activeCamera: camera, postProcessesEnabled: true, getEngine: () => engine });
    const manager = new PostProcessManager(scene);
    function aim(layer) {
        const renderTarget = {};
        camera.outputRenderTarget = { renderTarget, getViewCount: () => 1, _bindFrameBuffer: (_face, _layer, fullscreen = false) => engine.bindFramebuffer(renderTarget, undefined, undefined, undefined, fullscreen, 0, layer) };
        return renderTarget;
    }
    return { manager, camera, binds, aim };
}

test("the final post-process follows the camera's current array slice, including after a view change", () => {
    const { aim, binds, manager, camera } = fixture();
    for (const layer of [0, 1, 0]) {
        const target = aim(layer);
        manager._finalizeFrame(false, target);
        assert.equal(binds.at(-1)[6] ?? 0, layer);
        assert.equal(camera._postProcesses[0]._outputTexture, target);
    }
});

test("fullscreen keeps the eye slice; explicit faces and independent targets do not inherit it", () => {
    const { aim, binds, manager } = fixture();
    const output = aim(1);
    const offscreen = {};
    manager._finalizeFrame(false, output, 0);
    manager._finalizeFrame(false, output, 3);
    manager._finalizeFrame(false, output, undefined, undefined, true);
    manager._finalizeFrame(false, offscreen);
    manager._finalizeFrame(false);
    assert.deepEqual(binds, [
        [output, 0, undefined, undefined, false, 0, undefined], [output, 3, undefined, undefined, false, 0, undefined],
        [output, undefined, undefined, undefined, true, 0, 1], [offscreen, undefined, undefined, undefined, false, 0, undefined], "screen",
    ]);
});

test("the pinned bundle defers a terminal camera clear while its effect is compiling", () => {
    const { aim, manager, camera } = fixture();
    const eye = aim(1);
    const clears = [];
    manager._scene._bindFrameBuffer = (view, clear) => {
        assert.equal(view, camera);
        clears.push(clear);
    };
    for (const ready of [false, true, false]) {
        camera._postProcesses[0].isReady = () => ready;
        manager._finalizeFrame(false, eye, undefined, undefined, false, undefined, true);
    }
    assert.deepEqual(clears, [false, true, false]);
});
