const { test } = require("node:test");
const assert = require("node:assert/strict");
const { method } = require("./parentBuild.cjs");
const skip = Function("Constants", "return function(camera)" + method)({ RIG_MODE_CUSTOM: 22 });
const eye = () => ({ outputRenderTarget: { renderTarget: {} } });
function fixture() {
    const scene = { _engine: { isWebGPU: true, _currentFrameBufferIsDefaultFrameBuffer: () => true } };
    const camera = { cameraRigMode: 22, rigCameras: [eye(), eye()] };
    return { scene, camera, skips: () => skip.call(scene, camera) };
}
test("never elides the default canvas clear without the camera owner's opt-in", () => {
    const { camera, skips } = fixture();
    assert.equal(skips(), false);
    camera.skipUnusedParentFramebufferClear = true;
    assert.equal(skips(), true);
});
test("a spectator entering and leaving in the same frame restores the canvas clear immediately", () => {
    const { camera, scene, skips } = fixture();
    camera.skipUnusedParentFramebufferClear = true;
    assert.equal(skips(), true);
    scene.activeCameras = [camera, {}];
    assert.equal(skips(), false);
    scene.activeCameras.reverse();
    assert.equal(skips(), false);
    scene.activeCameras = [];
    assert.equal(skips(), true);
    camera.skipUnusedParentFramebufferClear = false;
    assert.equal(skips(), false);
});
test("each rig view must be drawable; shared array backing is allowed", () => {
    const { camera, skips } = fixture();
    camera.skipUnusedParentFramebufferClear = true;
    const [left, right] = camera.rigCameras;
    right.outputRenderTarget.renderTarget = left.outputRenderTarget.renderTarget;
    assert.equal(skips(), true);
    for (const eyes of [[], [left], [left, left], [left, {}], [left, { outputRenderTarget: {} }], [left, right, eye()]]) {
        camera.rigCameras = eyes;
        assert.equal(skips(), false);
    }
});
test("flat cameras, prepass, offscreen frames, other backends and eye clears remain untouched", () => {
    const { scene, camera, skips } = fixture();
    camera.skipUnusedParentFramebufferClear = true;
    for (const [target, key, value] of [[camera, "cameraRigMode", 0], [scene, "prePass", true], [scene, "_useCurrentFrameBuffer", true],
        [camera, "outputRenderTarget", {}], [camera, "_multiviewTexture", {}], [scene._engine, "isWebGPU", false],
        [scene._engine, "_currentFrameBufferIsDefaultFrameBuffer", () => false]]) {
        const old = target[key];
        target[key] = value;
        assert.equal(skips(), false, key);
        target[key] = old;
        assert.equal(skips(), true, key);
    }
    assert.equal(skip.call(scene, camera.rigCameras[0]), false);
    assert.equal(skip.call(scene, null), false);
});
