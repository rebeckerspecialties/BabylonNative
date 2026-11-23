// Exercise the published Scene.render and rig/spectator ordering with a small recording framebuffer.
const assert = require("node:assert/strict");
const { execFileSync } = require("node:child_process");
const Module = require("node:module");
const { withParentClear } = require("./parentBuild.cjs");
const source = execFileSync("tar", ["-xzOf", process.argv[2], "package/babylon.max.js"], { encoding: "utf8", maxBuffer: 64 << 20 });
const compiled = new Module(__filename);
compiled._compile(withParentClear(source), __filename);
const B = compiled.exports;
const engine = new B.NullEngine();
engine._isWebGPU = true;
const scene = new B.Scene(engine);
const camera = n => new B.FreeCamera(n, B.Vector3.Zero(), scene);
const parent = camera("parent"), left = camera("left"), right = camera("right"), spectator = camera("spectator");
parent.cameraRigMode = B.Constants.RIG_MODE_CUSTOM;
parent._rigCameras = [left, right];
const canvas = [0, 0];
let bound = canvas;
engine._currentFrameBufferIsDefaultFrameBuffer = () => bound === canvas;
engine.restoreDefaultFramebuffer = () => { bound = canvas; };
engine.clear = (_color, clear) => { if (clear) bound.fill(1); };
function output() {
    const pixels = [0, 0];
    return { renderTarget: {}, pixels, _bindFrameBuffer() { bound = pixels; }, onClearObservable: new B.Observable() };
}
left.outputRenderTarget = output();
right.outputRenderTarget = output();
// Only the final draw is mocked; the parent clear, eye binds and spectator bind/clear use the real Scene methods.
scene._renderForCamera = function (c) {
    this._bindFrameBuffer(c);
    bound[1] = c === spectator ? 4 : c === left ? 2 : 3;
};
function frame(cameras, skip) {
    canvas.fill(9);
    bound = canvas;
    parent.skipUnusedParentFramebufferClear = skip;
    scene.activeCamera = parent;
    scene.activeCameras = cameras;
    scene.render(false);
    assert.deepEqual(left.outputRenderTarget.pixels, [1, 2]);
    assert.deepEqual(right.outputRenderTarget.pixels, [1, 3]);
    return canvas.slice();
}
assert.deepEqual(frame(null, false), [1, 1]);
assert.deepEqual(frame(null, true), [9, 9]);
assert.deepEqual(frame([parent, spectator], true), [1, 4]);
assert.deepEqual(frame([parent, spectator], false), [1, 4]);
assert.deepEqual(frame(null, true), [9, 9]);
console.log("PASS: both eye outputs preserved; spectator background and foreground match the unoptimized frame");
