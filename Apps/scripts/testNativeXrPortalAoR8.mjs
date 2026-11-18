import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import vm from "node:vm";
import test from "node:test";

const require = createRequire(import.meta.url);
const B = require("../node_modules/babylonjs/babylon.max.js");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_ao_r8_validation.js", import.meta.url), "utf8");
function fixture() {
    const target = () => ({ _textureFormat: B.Constants.TEXTUREFORMAT_RGBA, _textureType: B.Constants.TEXTURETYPE_UNSIGNED_BYTE, _textures: [] });
    const pipeline = { name: "native-xr-portal-ssao2", _ssaoPostProcess: target(), _blurHPostProcess: target(), _blurVPostProcess: target(), _ssaoCombinePostProcess: target() };
    const context = vm.createContext({ BABYLON: { Constants: B.Constants, ShaderStore: { ShadersStoreWGSL: { ...B.ShaderStore.ShadersStoreWGSL } } } });
    vm.runInContext(source, context);
    return { context, pipeline, start: () => context.__nativeArPortalStartProfile({ postProcessRenderPipelineManager: { supportedPipelines: [pipeline] } }, () => {}) };
}

test("R8 only changes the three scalar producer destinations, never scene color", () => {
    const f = fixture();
    f.start();
    for (const key of ["_blurHPostProcess", "_blurVPostProcess", "_ssaoCombinePostProcess"]) {
        assert.equal(f.pipeline[key]._textureFormat, B.Constants.TEXTUREFORMAT_RED);
    }
    assert.equal(f.pipeline._ssaoPostProcess._textureFormat, B.Constants.TEXTUREFORMAT_RGBA);
    assert.match(f.context.BABYLON.ShaderStore.ShadersStoreWGSL.ssaoCombinePixelShader, /vec4f\(sceneColor.rgb \* ssaoColor.r, sceneColor.a\)/);
});

test("partially allocated, missing, non-RGBA and non-byte targets fail without partial format changes", () => {
    for (const change of [p => { p._blurVPostProcess._textures.push({}); }, p => { p._blurVPostProcess = null; },
        p => { p._blurVPostProcess._textureType = B.Constants.TEXTURETYPE_FLOAT; }, p => { p._blurVPostProcess._textureFormat = B.Constants.TEXTUREFORMAT_RED; }]) {
        const f = fixture();
        change(f.pipeline);
        const before = JSON.stringify(f.pipeline);
        assert.throws(f.start, /before allocation/);
        assert.equal(JSON.stringify(f.pipeline), before);
    }
});

test("scalar combine preserves all quantized AO levels, HDR colors and original alpha", () => {
    for (let byte = 0; byte <= 255; byte++) {
        const ao = byte / 255;
        for (const scene of [[0, 0, 0, 0], [1, 0.3, 0.25, 0.4], [15, 4, 2, 1]]) {
            const old = scene.map((v, i) => v * [ao, ao, ao, 1][i]);
            const next = [...scene.slice(0, 3).map(v => v * ao), scene[3]];
            assert.deepEqual(next, old);
        }
    }
});

test("combine contract drift and duplicate installation are rejected", () => {
    const f = fixture();
    assert.throws(() => vm.runInContext(source, f.context), /unchanged SSAO combine contract/);
});
