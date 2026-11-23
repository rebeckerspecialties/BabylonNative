import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import vm from "node:vm";
import test from "node:test";

const require = createRequire(import.meta.url);
const B = require("../node_modules/babylonjs/babylon.max.js");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_prepass_varying_validation.js", import.meta.url), "utf8");
function context() {
    return vm.createContext({ BABYLON: { ShaderStore: {
        IncludesShadersStoreWGSL: { ...B.ShaderStore.IncludesShadersStoreWGSL },
        ShadersStoreWGSL: { ...B.ShaderStore.ShadersStoreWGSL },
    } } });
}
test("scalar depth preserves the pre-pass expression and all other material code", () => {
    const c = context();
    vm.runInContext(source, c);
    const store = c.BABYLON.ShaderStore;
    for (const key of ["prePassVertexDeclaration", "prePassDeclaration"]) {
        assert.match(store.IncludesShadersStoreWGSL[key], /varying vViewDepth: f32;/);
        assert.doesNotMatch(store.IncludesShadersStoreWGSL[key], /vViewPos/);
    }
    assert.match(store.IncludesShadersStoreWGSL.prePassVertex, /vViewDepth = \(scene.view \* worldPos\).z;/);
    for (const [target, key] of [["IncludesShadersStoreWGSL", "pbrBlockPrePass"], ["IncludesShadersStoreWGSL", "openpbrBlockPrePass"], ["ShadersStoreWGSL", "defaultPixelShader"]]) {
        assert.equal(store[target][key].replace("fragmentInputs.vViewDepth", "fragmentInputs.vViewPos.z"), B.ShaderStore[target][key]);
    }
    assert.throws(() => vm.runInContext(source, c), /no longer matches/);
});
test("missing and duplicate shader contracts fail closed", () => {
    for (const value of [undefined, "varying vViewPos: vec3f; varying vViewPos: vec3f;"]) {
        const c = context();
        c.BABYLON.ShaderStore.IncludesShadersStoreWGSL.prePassVertexDeclaration = value;
        assert.throws(() => vm.runInContext(source, c), /prePassVertexDeclaration/);
    }
});
test("late contract failure never leaves a partially installed shader variant", () => {
    const c = context();
    const store = c.BABYLON.ShaderStore;
    store.ShadersStoreWGSL.defaultPixelShader = "fragmentInputs_vViewPos_z";
    const before = JSON.stringify(store);
    assert.throws(() => vm.runInContext(source, c), /defaultPixelShader/);
    assert.equal(JSON.stringify(store), before);
});
test("scalar interpolation equals the original depth component for perspective interpolation", () => {
    const vertices = [[2, -5, 0.25, 0.4], [-7, 3, -8, 2], [0, 12, 9000, 300]];
    for (const weights of [[1, 0, 0], [0, 1, 0], [0, 0, 1], [0.2, 0.3, 0.5]]) {
        const denominator = vertices.reduce((sum, v, i) => sum + weights[i] / v[3], 0);
        const vector = [0, 1, 2].map(component => vertices.reduce((sum, v, i) => sum + weights[i] * v[component] / v[3], 0) / denominator);
        const scalar = vertices.reduce((sum, v, i) => sum + weights[i] * v[2] / v[3], 0) / denominator;
        assert.equal(scalar, vector[2]);
    }
});
