import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(import.meta.url);
const B = require("babylonjs/babylon.max.js");
const read = name => readFileSync(new URL(`../Playground/Scripts/native_xr_portal_${name}.js`, import.meta.url), "utf8");
function context() {
    const c = vm.createContext({ BABYLON: { ShaderStore: { ShadersStoreWGSL: { ssao2PixelShader: B.ShaderStore.ShadersStoreWGSL.ssao2PixelShader } } } });
    vm.runInContext(read("ao_projection_validation"), c);
    vm.runInContext(read("gtvbao_validation"), c);
    return c;
}
for (const name of ["ao_ign_validation", "ao_slope_blur_validation"]) {
    test(`${name}: absent, repeated and changed contracts fail without partial installation`, () => {
        for (const change of [() => undefined, s => s.replace(/noiseSeed \^/g, "seed ^").replace("var off3:", "var changed:")]) {
            const c = context(), store = c.BABYLON.ShaderStore.ShadersStoreWGSL;
            store.ssao2PixelShader = change(store.ssao2PixelShader);
            const before = store.ssao2PixelShader;
            assert.throws(() => vm.runInContext(read(name), c), /requires the unchanged/);
            assert.equal(store.ssao2PixelShader, before);
        }
        const c = context();
        vm.runInContext(read(name), c);
        const before = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
        assert.throws(() => vm.runInContext(read(name), c), /requires the unchanged/);
        assert.equal(c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, before);
    });
}
test("IGN changes only the static noise, leaving visibility, thickness and blur untouched", () => {
    const c = context();
    const before = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    vm.runInContext(read("ao_ign_validation"), c);
    const after = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    assert.equal(after.slice(after.indexOf("let helper =")), before.slice(before.indexOf("let helper =")));
    assert.match(after, /fn portalIgn\(pixel: vec2f\) -> f32/);
    assert.doesNotMatch(after, /frameIndex|frameCount|history/);
});
test("slope filtering leaves the entire visibility estimator unchanged", () => {
    const c = context();
    const before = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    vm.runInContext(read("ao_slope_blur_validation"), c);
    const after = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    assert.equal(after.split("#else\n#ifdef BLUR")[0], before.split("#else\n#ifdef BLUR")[0]);
    const blur = after.slice(after.indexOf("fn portalBlurReciprocalDepth"), after.indexOf("return result / weightSum;"));
    assert.doesNotMatch(blur, /\bfor\s*\(/);
    assert.equal((blur.match(/textureSampleLevel\(image/g) || []).length, 7);
    assert.equal((blur.match(/portalBlurReciprocalDepth\(/g) || []).length, 8); // Declaration plus seven depth reads.
    assert.match(after, /sampleUv >= vec2f\(0.0\)/);
    assert.match(after, /sampleUv < vec2f\(1.0\)/);
});
test("a rounded first-pixel neighbor remains inside the view", () => {
    // Comparing against halfTexel rejects a valid neighbor for non-power-of-two widths.
    for (const width of [19, 2689]) {
        const uv = Math.fround(Math.fround(1.5 / width) - Math.fround(1 / width));
        assert.ok(uv < Math.fround(0.5 / width));
        assert.ok(uv >= 0 && uv < 1);
    }
});

// Mathematical reference for the shader's per-axis, view-local seven-tap filter.
function filter(depths, visibility, positions = [-3, -2, -1, 0, 1, 2, 3]) {
    const r = depths.map(z => z > 0 ? 1 / z : 0), center = r[3];
    if (center === 0) return 1;
    const l = (center - r[2]) / Math.max(positions[3] - positions[2], 1);
    const h = (r[4] - center) / Math.max(positions[4] - positions[3], 1);
    const slope = r[2] > 0 && r[4] > 0 && l * h > 0 ? (Math.abs(l) < Math.abs(h) ? l : h) : 0;
    let sum = visibility[3], weights = 1;
    for (const side of [-1, 1]) for (const distance of [1, 2, 3]) {
        const i = 3 + side * distance;
        const residual = Math.abs(r[i] - (center + slope * (positions[i] - positions[3]))) / center;
        const edge = Math.max(0, Math.min(1, 1 - residual / 0.005));
        const w = r[i] > 0 ? [0, 0.8, 0.4, 0.1][distance] * edge * edge : 0;
        sum += visibility[i] * w; weights += w;
    }
    return sum / weights;
}
test("flat and perspective-sloped planes have the same spatial weights", () => {
    const visibility = [0.2, 0.6, 0.3, 0.5, 0.8, 0.9, 0.7];
    for (const slope of [-0.03, -0.001, 0, 0.001, 0.03]) {
        const depths = Array.from({ length: 7 }, (_, i) => 1 / (0.2 + (i - 3) * slope));
        assert.ok(Math.abs(filter(depths, visibility) - filter(Array(7).fill(5), visibility)) < 1e-10);
    }
});
test("mixed AO/depth resolutions fit the sampled depth positions, not nominal tap offsets", () => {
    const visibility = [1, 1, 1, 0, 1, 1, 1];
    for (const positions of [[-7, -4, -2, 0, 2, 4, 7], [-7, -5, -2, 0, 2, 5, 7]]) {
        for (const slope of [-0.003, 0.003]) {
            const depths = positions.map(position => 1 / (0.2 + slope * position));
            const expected = filter(Array(7).fill(5), visibility);
            assert.ok(Math.abs(filter(depths, visibility, positions) - expected) < 1e-10);
            assert.ok(Math.abs(filter(depths, visibility) - expected) > 0.01, "The old nominal-spacing model must fail this regression.");
        }
    }
    const c = context();
    vm.runInContext(read("ao_slope_blur_validation"), c);
    const shader = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    assert.match(shader, /reciprocal\.y - center\.y/);
    assert.match(shader, /max\(center\.y - left\.y, 1\.0\)/);
});
test("sky, isolated geometry and silhouettes do not erase the center occlusion", () => {
    assert.equal(filter(Array(7).fill(0), Array(7).fill(0)), 1);
    assert.equal(filter([0, 0, 0, 5, 0, 0, 0], [1, 1, 1, 0.2, 1, 1, 1]), 0.2);
    assert.ok(Math.abs(filter([5, 5, 5, 5, 20, 20, 20], [0.2, 0.2, 0.2, 0.2, 1, 1, 1]) - 0.2) < 1e-12);
});
test("visibility stays bounded and constants are preserved on every depth pattern", () => {
    for (const depths of [Array(7).fill(5), [2, 3, 4, 5, 6, 7, 8], [0, 100, 1, 5, 0, 20, 1]]) {
        for (const v of [0, 0.2, 1]) assert.ok(Math.abs(filter(depths, Array(7).fill(v)) - v) < 1e-12);
        const value = filter(depths, [0, 1, 0, 1, 0, 1, 0]);
        assert.ok(value >= 0 && value <= 1);
    }
});
test("both spatial controls are packaged but not enabled in the demo", () => {
    const cmake = readFileSync(new URL("../Playground/CMakeLists.txt", import.meta.url), "utf8");
    const demo = readFileSync(new URL("../Playground/Scripts/native_xr_babylon_portal_demo.js", import.meta.url), "utf8");
    const runner = readFileSync(new URL("../Playground/Scripts/playground_runner.js", import.meta.url), "utf8");
    for (const name of ["ao_ign_validation", "ao_slope_blur_validation"]) {
        assert.ok(cmake.includes(`"Scripts/native_xr_portal_${name}.js"`));
        assert.ok(!demo.includes(name));
        assert.ok(!runner.includes(name));
    }
});
