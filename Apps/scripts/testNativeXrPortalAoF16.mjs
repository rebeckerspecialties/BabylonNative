import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import vm from "node:vm";
import test from "node:test";

const B = createRequire(import.meta.url)("babylonjs/babylon.max.js");
const read = name => readFileSync(new URL(`../Playground/Scripts/${name}.js`, import.meta.url), "utf8");
const source = read("native_xr_portal_ao_f16_validation");
function context() {
    const c = vm.createContext({ BABYLON: { ShaderLanguage: B.ShaderLanguage, ShaderStore: { ShadersStoreWGSL: { ssao2PixelShader: B.ShaderStore.ShadersStoreWGSL.ssao2PixelShader } } } });
    vm.runInContext(read("native_xr_portal_ao_projection_validation"), c);
    vm.runInContext(read("native_xr_portal_gtvbao_validation"), c);
    return c;
}
function cosine(x, y, flush) {
    const scale = Math.max(Math.abs(x), Math.abs(y));
    if (scale < 0.0625 || scale > 128) return x / Math.sqrt(Math.max(x * x + y * y, 1e-12));
    const h = v => {
        const rounded = Math.f16round(v);
        return flush && Math.abs(rounded) < 2 ** -14 ? 0 : rounded;
    };
    const hx = h(x), hy = h(y);
    return h(hx * h(1 / Math.sqrt(h(h(hx * hx) + h(hy * hy)))));
}
test("guarded half normalization is finite and bounded with and without subnormals", () => {
    for (const flush of [false, true]) for (let exponent = -4; exponent <= 7; exponent++) {
        for (let i = 0; i <= 4096; i++) {
            const angle = i / 4096 * 2 * Math.PI;
            const x = Math.cos(angle) * 2 ** exponent;
            const y = Math.sin(angle) * 2 ** exponent;
            const value = cosine(x, y, flush);
            assert.ok(Number.isFinite(value));
            assert.ok(Math.abs(value - Math.cos(angle)) < 0.002);
        }
    }
});
test("near-zero, boundary and large inputs retain the full-precision fallback", () => {
    for (const x of [0, 1e-10, -1e-10, 0.0624, -0.0624, 128.01, -128.01, 1e8]) {
        for (const y of [0, x * 0.5, -x]) {
            const reference = x / Math.sqrt(Math.max(x * x + y * y, 1e-12));
            assert.equal(cosine(x, y, true), reference);
        }
    }
});
test("half-angle variant preserves geometry, thickness, jitter and the blur shader", () => {
    const c = context();
    const before = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    vm.runInContext(source, c);
    const after = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    const remainder = after.slice(after.indexOf("varying vUV:"));
    assert.equal(remainder.replace("portalHalfProjectedCosine(projected)", "projected.x * inverseSqrt(max(dot(projected, projected), 1e-12))"), before.trimStart());
    assert.deepEqual(Array.from(c.__nativePlaygroundRequiredFeatures), ["shader-f16"]);
    assert.throws(() => c.__nativePlaygroundConfigureEngine({ enabledExtensions: [] }), /shader-f16 on the device/);
    const processor = { finalizeShaders: (vertex, fragment) => ({ vertexCode: vertex, fragmentCode: fragment }) };
    c.__nativePlaygroundConfigureEngine({ enabledExtensions: ["shader-f16"], _getShaderProcessor: () => processor });
    assert.equal(processor.finalizeShaders("vertex", "ordinary").fragmentCode, "ordinary");
    const result = processor.finalizeShaders("vertex", after);
    assert.ok(result.fragmentCode.startsWith("enable f16;"));
    assert.equal(result.vertexCode, "vertex");
});
test("missing and repeated shader contracts cannot partially install the variant", () => {
    const c = context();
    c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader = "changed";
    assert.throws(() => vm.runInContext(source, c), /unchanged GT-VBAO/);
    assert.equal(c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, "changed");
    assert.equal(c.__nativePlaygroundConfigureEngine, undefined);
    const valid = context();
    vm.runInContext(source, valid);
    const before = valid.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    assert.throws(() => vm.runInContext(source, valid), /unchanged GT-VBAO/);
    assert.equal(valid.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, before);
});
