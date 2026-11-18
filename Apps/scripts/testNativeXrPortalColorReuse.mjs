import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import vm from "node:vm";
import test from "node:test";

const B = createRequire(import.meta.url)("../node_modules/babylonjs/babylon.max.js");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_color_reuse_validation.js", import.meta.url), "utf8");
function fixture() {
    const original = { width: -1, height: -1, effect: {}, onBeforeRenderObservable: new B.Observable() };
    const ao = { _textureType: B.Constants.TEXTURETYPE_UNSIGNED_BYTE, onApplyObservable: new B.Observable() };
    let compiled = 0;
    const combine = { updateEffect(...args) { assert.equal(args[7], "portalSsaoColorReuse"); compiled++; } };
    let color = { getSize: () => ({ width: 1242, height: 2689 }) };
    const prepass = { getRenderTarget: () => ({ textures: [color] }), getIndex: type => { assert.equal(type, B.Constants.PREPASS_COLOR_TEXTURE_TYPE); return 0; } };
    const camera = { viewport: { x: 0, y: 0, width: 1, height: 1 }, _getFirstPostProcess: () => original };
    const pipeline = { name: "native-xr-portal-ssao2", textureSamples: 1, SSAOOriginalSceneColorEffect: "original", _originalColorPostProcess: original,
        _ssaoPostProcess: ao, _ssaoCombinePostProcess: combine, _prePassRenderer: prepass, _thinSSAORenderingPipeline: { _ssaoCombinePostProcess: {} } };
    const disabled = [];
    const scene = { activeCamera: camera, postProcessRenderPipelineManager: { supportedPipelines: [pipeline], disableEffectInPipeline: (...args) => disabled.push(args) } };
    const store = { ...B.ShaderStore.ShadersStoreWGSL };
    const context = vm.createContext({ __nativeArPortalForceNonXrStress: true, BABYLON: { Constants: B.Constants, ShaderStore: { ShadersStoreWGSL: store } } });
    vm.runInContext(source, context);
    return { scene, camera, pipeline, original, ao, combine, store, context, disabled, compiled: () => compiled,
        setColor: value => { color = value; }, start: () => context.__nativeArPortalStartProfile(scene, () => {}) };
}

test("color reuse removes only the original copy and retains eight-bit conversion", () => {
    const f = fixture();
    f.start();
    assert.equal(f.compiled(), 1);
    assert.equal(f.disabled.length, 1);
    assert.equal(f.disabled[0][0], f.pipeline.name);
    assert.equal(f.disabled[0][1], "original");
    assert.equal(f.disabled[0][2][0], f.camera);
    assert.match(f.store.portalSsaoColorReusePixelShader, /floor\(clamp\(sceneColor/);
    assert.doesNotMatch(f.store.ssaoCombinePixelShader, /floor\(clamp\(sceneColor/);
});

test("unsupported layouts fail before disabling or changing the shader", () => {
    for (const change of [f => { f.context.__nativeArPortalForceNonXrStress = false; }, f => { f.pipeline._prePassRenderer = null; }, f => { f.pipeline.textureSamples = 4; },
        f => { f.scene.activeCameras = [f.camera, {}]; }, f => { f.camera.viewport.width = 0.5; },
        f => { f.ao._textureType = B.Constants.TEXTURETYPE_HALF_FLOAT; }, f => { f.camera._getFirstPostProcess = () => ({}); }]) {
        const f = fixture();
        const before = f.store.ssaoCombinePixelShader;
        change(f);
        assert.throws(f.start, /single-camera, full-viewport, single-sample/);
        assert.equal(f.store.ssaoCombinePixelShader, before);
        assert.equal(f.store.portalSsaoColorReusePixelShader, undefined);
        assert.equal(f.disabled.length, 0);
        assert.equal(f.compiled(), 0);
    }
});

test("each apply obtains current color and dimensions, including same-frame replacement", () => {
    const f = fixture();
    let originalSetup = 0;
    f.original.onBeforeRenderObservable.add(() => originalSetup++);
    f.start();
    for (const width of [1242, 640, 2048]) {
        const color = { getSize: () => ({ width, height: width * 2 }) };
        f.setColor(color);
        f.ao.onApplyObservable.notifyObservers({});
        assert.equal(f.original.width, width);
        assert.equal(f.original.height, width * 2);
        f.combine.onApply({ setTexture(name, value) { assert.equal(name, "originalColor"); assert.equal(value, color); } });
        assert.equal(f.pipeline._thinSSAORenderingPipeline._ssaoCombinePostProcess.camera, f.camera);
    }
    assert.equal(originalSetup, 3);
    f.scene.activeCamera = {};
    assert.throws(() => f.ao.onApplyObservable.notifyObservers({}), /changed camera/);
});

test("deferred quantization retains every eight-bit level and clamps HDR and alpha", () => {
    const quantize = x => Math.floor(Math.max(0, Math.min(1, x)) * 255 + 0.5) / 255;
    for (let i = 0; i <= 255; i++) assert.equal(quantize(i / 255), i / 255);
    assert.equal(quantize(-2), 0);
    assert.equal(quantize(16), 1);
});
