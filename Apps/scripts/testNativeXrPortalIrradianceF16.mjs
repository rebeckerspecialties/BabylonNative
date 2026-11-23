import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import vm from "node:vm";
import test from "node:test";

const B = createRequire(import.meta.url)("../node_modules/babylonjs/babylon.max.js");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_irradiance_f16_validation.js", import.meta.url), "utf8");
function fixture() {
    const includes = { ...B.ShaderStore.IncludesShadersStoreWGSL, pbrBRDFFunctions: "portalFp16Pow5" };
    return { includes, context: vm.createContext({ BABYLON: { ShaderLanguage: B.ShaderLanguage, ShaderStore: { IncludesShadersStoreWGSL: includes } } }) };
}
test("half irradiance keeps the original function and guards every coefficient and direction", () => {
    const f = fixture();
    const original = f.includes.harmonicsFunctions;
    vm.runInContext(source, f.context);
    const result = f.includes.harmonicsFunctions;
    assert.match(result, /fn portalIrradianceF32/);
    for (const coefficient of ["L00", "L1_1", "L10", "L11", "L2_2", "L2_1", "L20", "L21", "L22"]) {
        assert.ok(result.includes("any(abs(uniforms.vSpherical" + coefficient + ") > vec3f(8.0))"));
    }
    assert.match(result, /any\(abs\(normal\) > vec3f\(2.0\)\)/);
    assert.ok(result.includes(original.match(/fn computeEnvironmentIrradiance[^}]*vSphericalL22[^}]*\}/)[0].replace("computeEnvironmentIrradiance", "portalIrradianceF32")));
    assert.equal(result.slice(result.indexOf("#else")), original.slice(original.indexOf("#else")));
});
test("half irradiance fails atomically on drift, missing F16 setup, or repeated application", () => {
    for (const mutate of [f => { f.includes.harmonicsFunctions = ""; }, f => { f.includes.pbrBRDFFunctions = ""; }]) {
        const f = fixture();
        mutate(f);
        const before = { ...f.includes };
        assert.throws(() => vm.runInContext(source, f.context), /Half irradiance requires/);
        assert.deepEqual(f.includes, before);
    }
    const f = fixture();
    vm.runInContext(source, f.context);
    const before = f.includes.harmonicsFunctions;
    assert.throws(() => vm.runInContext(source, f.context), /Half irradiance requires/);
    assert.equal(f.includes.harmonicsFunctions, before);
});

test("both shader stages opt into F16 exactly once without changing ordinary shaders", () => {
    const f = fixture();
    vm.runInContext(source, f.context);
    assert.throws(() => f.context.__nativePlaygroundConfigureEngine({ enabledExtensions: [] }), /shader-f16 on the device/);
    const processor = { finalizeShaders: (vertexCode, fragmentCode) => ({ vertexCode, fragmentCode }) };
    f.context.__nativePlaygroundConfigureEngine({ enabledExtensions: ["shader-f16"], _getShaderProcessor: () => processor });
    assert.equal(processor.finalizeShaders("ordinary", "ordinary").vertexCode, "ordinary");
    for (const source of ["portalIrradianceF32", "enable f16;\nportalIrradianceF32"]) {
        const result = processor.finalizeShaders(source, source);
        for (const stage of [result.vertexCode, result.fragmentCode]) {
            assert.ok(stage.startsWith("enable f16;"));
            assert.equal(stage.match(/enable f16;/g).length, 1);
        }
    }
});

function evaluate(n, c, half = false, flush = false) {
    const h = v => {
        const value = half ? Math.f16round(v) : v;
        return half && flush && Math.abs(value) < 2 ** -14 ? 0 : value;
    };
    const [x, y, z] = n.map(h);
    const basis = [1, y, z, x, h(y * x), h(y * z), h(h(h(3 * z) * z) - 1), h(z * x), h(h(x * x) - h(y * y))];
    return basis.reduce((sum, value, i) => h(sum + h(h(c[i]) * value)), 0);
}
function candidate(n, c, flush) {
    return n.some(v => Math.abs(v) > 2) || c.some(v => Math.abs(v) > 8)
        ? evaluate(n, c) : evaluate(n, c, true, flush);
}
test("bounded half accumulation remains finite at guard boundaries and with flushed subnormals", () => {
    for (const flush of [false, true]) for (const scale of [0, 0.00001, 1, 2]) {
        for (let i = 0; i < 4096; i++) {
            const n = [Math.sin(i), Math.cos(i * 0.7), Math.sin(i * 0.3)].map(v => v * scale);
            const c = Array.from({ length: 9 }, (_, j) => 8 * Math.sin(i * 0.13 + j));
            const result = candidate(n, c, flush);
            assert.ok(Number.isFinite(result));
            assert.ok(Math.abs(result - evaluate(n, c)) < 0.25);
        }
    }
    assert.equal(candidate([0, 1, 0], new Array(9).fill(0), true), 0);
});
test("HDR coefficients and non-rigid directions use the unchanged float fallback", () => {
    for (let index = 0; index < 9; index++) for (const value of [-8.01, 8.01, -65536, 65536]) {
        const c = new Array(9).fill(1);
        c[index] = value;
        assert.equal(candidate([0.1, 0.2, 0.3], c, true), evaluate([0.1, 0.2, 0.3], c));
    }
    for (const n of [[2.01, 0, 0], [0, -2.01, 0], [0, 0, 8]]) {
        assert.equal(candidate(n, new Array(9).fill(1), true), evaluate(n, new Array(9).fill(1)));
    }
});
