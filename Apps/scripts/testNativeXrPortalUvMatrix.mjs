import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import vm from "node:vm";
import test from "node:test";

const require = createRequire(import.meta.url);
const B = require("../node_modules/babylonjs/babylon.max.js");
const original = B.ShaderStore.IncludesShadersStoreWGSL.samplerVertexImplementation;
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_uv_matrix_validation.js", import.meta.url), "utf8");
function context(shader = original) {
    return vm.createContext({ BABYLON: { ShaderStore: { IncludesShadersStoreWGSL: { samplerVertexImplementation: shader } } } });
}

test("affine UV lowering preserves all six selectors and only changes matrix arithmetic", () => {
    const c = context();
    vm.runInContext(source, c);
    const changed = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL.samplerVertexImplementation;
    assert.equal((changed.match(/mat3x2f/g) || []).length, 6);
    assert.equal((changed.match(/#ifdef UV[2-6]/g) || []).length, 5);
    assert.equal((changed.match(/uniforms.v_INFONAME_/g) || []).length, 6);
    const restored = changed.replace(/\(mat3x2f\(uniforms\._MATRIXNAME_Matrix\[0\]\.xy, uniforms\._MATRIXNAME_Matrix\[1\]\.xy, uniforms\._MATRIXNAME_Matrix\[2\]\.xy\) \* vec3f\(([^,]+), 1\.0\)\)/g,
        "(uniforms._MATRIXNAME_Matrix * vec4f($1, 1.0, 0.0)).xy");
    assert.equal(restored.replace(/\s/g, ""), original.replace(/\s/g, ""));
});

test("missing, partial, and repeated installation fail before mutation", () => {
    for (const value of [undefined, original.replaceAll("uvUpdated", "otherUv"), original + original]) {
        const c = context(value);
        if (value === undefined) delete c.BABYLON.ShaderStore.IncludesShadersStoreWGSL.samplerVertexImplementation;
        const before = JSON.stringify(c.BABYLON);
        assert.throws(() => vm.runInContext(source, c), /six coordinate paths/);
        assert.equal(JSON.stringify(c.BABYLON), before);
    }
    const c = context();
    vm.runInContext(source, c);
    const before = JSON.stringify(c.BABYLON);
    assert.throws(() => vm.runInContext(source, c), /six coordinate paths/);
    assert.equal(JSON.stringify(c.BABYLON), before);
});

test("reduced transform retains translation, rotation, scaling and arbitrary unused rows", () => {
    for (const uv of [[0, 0], [1, 1], [-3, 2.5]]) {
        for (let angle = -3; angle <= 3; angle += 0.25) {
            const m = [[2 * Math.cos(angle), 2 * Math.sin(angle), 99, -4], [-Math.sin(angle), Math.cos(angle), 17, 45], [0.25, -0.75, 1, 0], [88, 77, 66, 55]];
            const full = [0, 1].map(row => m.reduce((sum, col, i) => sum + col[row] * [uv[0], uv[1], 1, 0][i], 0));
            const reduced = [0, 1].map(row => m[0][row] * uv[0] + m[1][row] * uv[1] + m[2][row]);
            assert.deepEqual(reduced, full);
        }
    }
});
