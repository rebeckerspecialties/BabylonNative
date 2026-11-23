import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import test from "node:test";

const require = createRequire(import.meta.url);
const bundle = process.env.BABYLONJS_BUNDLE ? resolve(process.env.BABYLONJS_BUNDLE) : require.resolve("babylonjs/babylon.max.js");
const BABYLON = require(bundle);

function fixture(t) {
    const engine = new BABYLON.NullEngine();
    engine.getCaps().drawBuffersExtension = true;
    engine._features.supportSSAO2 = true;
    const scene = new BABYLON.Scene(engine);
    const camera = new BABYLON.FreeCamera("camera", BABYLON.Vector3.Zero(), scene);
    const geometry = Object.create(BABYLON.GeometryBufferRenderer.prototype);
    geometry.generateNormalsInWorldSpace = false;
    const pipeline = new BABYLON.SSAO2RenderingPipeline("ssao-test", scene, 1, undefined, geometry);
    const postProcess = pipeline._thinSSAORenderingPipeline._ssaoPostProcess;
    const values = {};
    const effect = Object.fromEntries(["setArray3", "setFloat", "setFloat2", "setFloat4", "setMatrix", "setMatrix3x3", "setTexture"].map(method =>
        [method, (name, ...args) => { values[name] = { method, args }; }]));
    effect.dispose = () => {};
    postProcess._drawWrapper.effect = effect;
    postProcess.camera = camera;
    t.after(() => { pipeline.dispose(); scene.dispose(); engine.dispose(); });
    return { pipeline, postProcess, camera, geometry, values, effect };
}

test("published SSAO2 retains upstream #18539 normal-space selection", t => {
    const f = fixture(t);
    assert.equal(f.postProcess.normalsInWorldSpace, false);
    f.geometry.generateNormalsInWorldSpace = true;
    f.pipeline.isReady();
    assert.equal(f.postProcess.normalsInWorldSpace, true);
    f.geometry.generateNormalsInWorldSpace = false;
    f.pipeline.isReady();
    assert.equal(f.postProcess.normalsInWorldSpace, false);
});

test("published SSAO2 binds an inverse-transpose mat3 only for world-space normals", t => {
    const f = fixture(t);
    f.camera.getViewMatrix = () => BABYLON.Matrix.Scaling(2, 4, 8);
    f.postProcess.bind(true);
    assert.equal(f.values.normalWorldToView, undefined);
    f.postProcess.normalsInWorldSpace = true;
    f.postProcess._drawWrapper.effect = f.effect;
    f.postProcess.bind(true);
    assert.equal(f.values.normalWorldToView.method, "setMatrix3x3");
    assert.deepEqual(Array.from(f.values.normalWorldToView.args[0], value => value === 0 ? 0 : value), [0.5, 0, 0, 0, 0.25, 0, 0, 0, 0.125]);
});

for (const [language, store] of [["GLSL", BABYLON.ShaderStore.ShadersStore], ["WGSL", BABYLON.ShaderStore.ShadersStoreWGSL]]) {
    test(`published ${language} SSAO2 normalizes sampled normals and converts world-space normals`, () => {
        const shader = store.ssao2PixelShader;
        assert.match(shader, /normal[^=\n]*=\s*normalize\(texture/);
        assert.match(shader, /#ifdef NORMAL_WORLDSPACE\s+normal\s*=\s*normalize\((?:uniforms\.)?normalWorldToView\s*\*\s*normal\)/);
    });
}
