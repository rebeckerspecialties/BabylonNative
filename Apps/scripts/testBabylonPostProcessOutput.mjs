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
        camera.outputRenderTarget = { renderTarget, _bindFrameBuffer: () => engine.bindFramebuffer(renderTarget, undefined, undefined, undefined, false, 0, layer) };
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

test("explicit faces, fullscreen, independent targets and the canvas keep their original binding", () => {
    const { aim, binds, manager } = fixture();
    const output = aim(1);
    const offscreen = {};
    manager._finalizeFrame(false, output, 0);
    manager._finalizeFrame(false, output, 3);
    manager._finalizeFrame(false, output, undefined, undefined, true);
    manager._finalizeFrame(false, offscreen);
    manager._finalizeFrame(false);
    assert.deepEqual(binds, [
        [output, 0, undefined, undefined, false], [output, 3, undefined, undefined, false],
        [output, undefined, undefined, undefined, true], [offscreen, undefined, undefined, undefined, false], "screen",
    ]);
});
