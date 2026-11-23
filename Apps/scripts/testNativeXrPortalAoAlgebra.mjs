import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import vm from "node:vm";
import test from "node:test";

const require = createRequire(import.meta.url);
const B = require("babylonjs/babylon.max.js");
const read = name => readFileSync(new URL(`../Playground/Scripts/${name}.js`, import.meta.url), "utf8");
const source = read("native_xr_portal_ao_algebra_validation");
function context() {
    const c = vm.createContext({ BABYLON: { ShaderStore: { ShadersStoreWGSL: { ssao2PixelShader: B.ShaderStore.ShadersStoreWGSL.ssao2PixelShader } } } });
    vm.runInContext(read("native_xr_portal_ao_projection_validation"), c);
    vm.runInContext(read("native_xr_portal_gtvbao_validation"), c);
    return c;
}
test("orthogonal rotation preserves both slice directions across the full noise interval", () => {
    for (let i = 0; i <= 65536; i++) {
        const noise = i / 65536;
        const phi = noise * Math.PI / 2;
        const rotation = [Math.cos(phi), Math.sin(phi)];
        for (const slice of [0, 1]) {
            const direction = slice ? [-rotation[1], rotation[0]] : rotation;
            const reference = (slice + noise) * Math.PI / 2;
            assert.ok(Math.abs(direction[0] - Math.cos(reference)) < 1e-15);
            assert.ok(Math.abs(direction[1] - Math.sin(reference)) < 1e-15);
        }
    }
});
test("normalization and its near-zero guard agree on both sides of the cutoff", () => {
    for (const length of [0, 1e-9, 0.999e-6, 1e-6, 1.001e-6, 0.1, 1, 10]) {
        const normal = [length, 0, 0];
        const squared = normal.reduce((sum, value) => sum + value * value, 0);
        assert.equal(Math.sqrt(squared) < 1e-6, squared < 1e-12);
        if (squared >= 1e-12) assert.ok(Math.abs(normal[0] / Math.sqrt(squared) - normal[0] * (1 / Math.sqrt(squared))) < 1e-15);
    }
});
test("AO algebra keeps thickness, point counts and the entire blur path unchanged", () => {
    const c = context();
    const before = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    vm.runInContext(source, c);
    const after = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    const blur = "#else\n#ifdef BLUR\nuniform outSize";
    assert.equal(after.slice(after.indexOf(blur)), before.slice(before.indexOf(blur)));
    assert.equal(after.slice(after.indexOf("let nt =")), before.slice(before.indexOf("let nt =")));
    assert.match(after, /PORTAL_STEPS_PER_SIDE: u32 = 2u/);
    const code = after.replace(/\/\/[^\n]*/g, "");
    assert.equal((code.match(/\bcos\(/g) || []).length, 1);
    assert.equal((code.match(/\bsin\(/g) || []).length, 1);
});
test("missing, repeated and changed shader inputs fail without partial installation", () => {
    for (const transform of [() => undefined, s => s.replace("2u;", "3u;"), s => s.replace("normal /= normalLength;", "normal = normalize(normal);")]) {
        const c = context();
        const store = c.BABYLON.ShaderStore.ShadersStoreWGSL;
        store.ssao2PixelShader = transform(store.ssao2PixelShader);
        const before = store.ssao2PixelShader;
        assert.throws(() => vm.runInContext(source, c), /no longer matches/);
        assert.equal(store.ssao2PixelShader, before);
    }
    const c = context();
    vm.runInContext(source, c);
    const before = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    assert.throws(() => vm.runInContext(source, c), /no longer matches/);
    assert.equal(c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, before);
});
