import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(import.meta.url);
const B = require("babylonjs/babylon.max.js");
const read = name => readFileSync(new URL(`../Playground/Scripts/${name}.js`, import.meta.url), "utf8");
const aoSource = read("native_xr_portal_gtvbao_validation");
const projectionSource = read("native_xr_portal_ao_projection_validation");
const f16Source = read("native_xr_portal_f16_material_validation");
const comparisonSource = read("native_xr_portal_gpu_comparison");
function context() {
    return vm.createContext({ BABYLON: {
        ShaderStore: { ShadersStoreWGSL: { ssao2PixelShader: B.ShaderStore.ShadersStoreWGSL.ssao2PixelShader },
            IncludesShadersStoreWGSL: Object.fromEntries(["pbrBRDFFunctions", "pbrDirectLightingFunctions", "pbrBlockReflectivity"].map(name => [name, B.ShaderStore.IncludesShadersStoreWGSL[name]])) },
        Camera: B.Camera, ShaderLanguage: B.ShaderLanguage,
    } });
}

test("comparison kernel is repeatable and restores random even after setup failure", () => {
    const c = vm.createContext({});
    vm.runInContext(`globalThis.ssao = { samples: 8, _generateHemisphere: function () {
            const values = Array.from({ length: this.samples }, () => Math.random());
            if (this.fail) throw new Error("kernel failure");
            return values;
        }, _randomTexture: { dispose() {} }, _createRandomTexture() {
            this._randomTexture = { values: [Math.random(), Math.random()], dispose() {} };
        } };
        globalThis.originalRandom = Math.random;`, c);
    vm.runInContext(comparisonSource, c);
    const scene = { postProcessRenderPipelineManager: { supportedPipelines: [{ name: "native-xr-portal-ssao2", _thinSSAORenderingPipeline: { _ssaoPostProcess: c.ssao } }] } };
    for (const samples of [0, 1, 8, 32]) {
        c.ssao.samples = samples;
        c.__nativeArPortalStartProfile(scene, () => {});
        const first = c.ssao._sampleSphere;
        const noise = c.ssao._randomTexture.values;
        c.__nativeArPortalStartProfile(scene, () => {});
        assert.deepEqual(first, c.ssao._sampleSphere);
        assert.deepEqual(noise, c.ssao._randomTexture.values);
        assert.equal(first.length, samples);
        assert.ok(first.every(value => value >= 0 && value < 1));
    }
    c.ssao.fail = true;
    assert.throws(() => c.__nativeArPortalStartProfile(scene, () => {}), /kernel failure/);
    assert.equal(vm.runInContext("Math.random", c), c.originalRandom);
    assert.equal(c.__nativeArPortalBenchmarkSuffix, "-seeded-v2");
});

test("AO opt-in preserves the corrected blur path and has no inverse trigonometry", () => {
    const c = context();
    vm.runInContext(projectionSource, c);
    const prior = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    vm.runInContext(aoSource, c);
    const shader = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    const boundary = "#else\n#ifdef BLUR\nuniform outSize";
    assert.equal(shader.slice(shader.indexOf(boundary)), prior.slice(prior.indexOf(boundary)));
    assert.doesNotMatch(shader, /\b(?:acos|asin|atan|atan2)\s*\(/);
    assert.match(shader, /PORTAL_SLICE_COUNT: u32 = 2u/);
    assert.match(shader, /PORTAL_STEPS_PER_SIDE: u32 = 2u/);
    assert.match(shader, /insertBits\(0u, 0xffffffffu, lo, hi - lo\)/);
    assert.match(shader, /normalWorldToView \* normal/);
    assert.match(shader, /sampleUv, sampleDepth, depthSign/);
    assert.match(shader, /portalClipSlab\(delta, ray, thickness, safeRadius\)/);
    assert.match(shader, /delta \+ ray \* interval.x/);
    assert.match(shader, /delta \+ ray \* interval.y/);
    assert.doesNotMatch(shader, /distance > safeRadius/);
    assert.match(shader, /height <= max\(footprint \* 0.25, 1e-4\)/);
    assert.match(shader, /sampleDepth == 0.0 \|\| sampleDepth >= uniforms.maxZ/);
});

test("AO fails closed on missing prerequisite or shader drift", () => {
    const c = context();
    const original = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    assert.throws(() => vm.runInContext(aoSource, c), /requires the corrected SSAO2/);
    assert.equal(c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, original);
    vm.runInContext(projectionSource, c);
    c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader += "#else\n#ifdef BLUR\nuniform outSize";
    assert.throws(() => vm.runInContext(aoSource, c), /unchanged blur boundary/);
});

test("AO rejects orthographic cameras before invoking the earlier setup hook", () => {
    const c = context();
    vm.runInContext(projectionSource, c);
    let calls = 0;
    c.__nativeArPortalStartProfile = () => calls++;
    vm.runInContext(aoSource, c);
    assert.throws(() => c.__nativeArPortalStartProfile({ activeCamera: { mode: B.Camera.ORTHOGRAPHIC_CAMERA } }, () => {}), /perspective camera/);
    assert.equal(calls, 0);
    c.__nativeArPortalStartProfile({ activeCamera: { mode: B.Camera.PERSPECTIVE_CAMERA } }, () => {});
    assert.equal(calls, 1);
});

test("AO jitter does not interpret normalized SSAO vectors as uniform samples", () => {
    assert.match(aoSource, /noisePixel = vec2u\(input.position.xy\)/);
    assert.match(aoSource, /portalRandom\(noiseSeed \^ 0x9e3779b9u\)/);
    assert.doesNotMatch(aoSource, /textureSampleLevel\(randomSampler/);
});

test("AO center depth, normal and ray all refer to the same pre-pass texel", () => {
    assert.match(aoSource, /centerPixel = clamp\(vec2i\(input.vUV \* depthSize\), vec2i\(0\), vec2i\(depthDimensions\) - 1\)/);
    assert.match(aoSource, /centerUv = \(vec2f\(centerPixel\) \+ 0.5\) \/ depthSize/);
    assert.match(aoSource, /textureLoad\(depthSampler, centerPixel, 0\)/);
    assert.match(aoSource, /textureLoad\(normalSampler, centerPixel, 0\)/);
    assert.match(aoSource, /portalViewPosition\(centerUv, depth, depthSign\)/);
    assert.doesNotMatch(aoSource, /textureSampleLevel\((depthSampler|normalSampler),/);
});

// Independent CPU reference, not a claim that the WGSL has executed on a GPU.
const clamp = x => Math.max(0, Math.min(1, x));
const cdf = (angle, normalAngle) => clamp(angle < 0
    ? (Math.cos(angle) - Math.sin(normalAngle)) / 2
    : 1 - (Math.cos(angle) + Math.sin(normalAngle)) / 2);
test("angle-free slice CDF matches numerical hemisphere solid-angle integration", () => {
    for (const n of [-1.55, -0.8, 0, 0.8, 1.55]) {
        const low = n - Math.PI / 2;
        const high = n + Math.PI / 2;
        assert.ok(Math.abs(cdf(low, n)) < 1e-12);
        assert.ok(Math.abs(cdf(high, n) - 1) < 1e-12);
        for (let sample = 0; sample <= 32; sample++) {
            const angle = low + (high - low) * sample / 32;
            let integral = 0;
            const step = (angle - low) / 4096;
            for (let i = 0; i < 4096; i++) integral += Math.abs(Math.sin(low + (i + 0.5) * step)) * step / 2;
            assert.ok(Math.abs(cdf(angle, n) - integral) < 1e-6);
            assert.ok(Math.abs(cdf(angle, n) + cdf(-angle, -n) - 1) < 1e-12);
        }
    }
});

function mask(a, b, jitter) {
    const low = Math.max(0, Math.min(32, Math.ceil(Math.min(a, b) * 32 - jitter)));
    const high = Math.max(0, Math.min(32, Math.ceil(Math.max(a, b) * 32 - jitter)));
    return ((1n << BigInt(high - low)) - 1n) << BigInt(low);
}
test("0/1/32-bit masks agree with explicit jittered points including reversed horizons", () => {
    for (const jitter of [0, 0.5, 0.999]) {
        for (const [a, b] of [[0, 0], [1, 1], [0, 1], [1, 0], [0, 1 / 32], [31 / 32, 1], [0.37, 0.39], [-1, 2]]) {
            let reference = 0n;
            for (let i = 0; i < 32; i++) {
                const point = (i + jitter) / 32;
                if (point >= Math.min(a, b) && point < Math.max(a, b)) reference |= 1n << BigInt(i);
            }
            assert.equal(mask(a, b, jitter), reference);
            assert.equal(mask(a, b, jitter) | mask(a, b, jitter), reference);
        }
    }
});

test("perspective reconstruction handles mirrored projection and both depth signs", () => {
    for (const sign of [-1, 1]) for (const sx of [-2, 2]) for (const sy of [-4, 4]) {
        for (const offset of [[0, 0], [0.2, -0.3]]) {
            const uv = [0.1, 0.8];
            const depth = 3;
            const position = [(uv[0] * 2 - 1 - offset[0] * sign) * depth / sx,
                (uv[1] * 2 - 1 - offset[1] * sign) * depth / sy, depth * sign];
            const clip = [sx * position[0] + offset[0] * position[2], sy * position[1] + offset[1] * position[2]];
            assert.ok(Math.abs((clip[0] / depth + 1) / 2 - uv[0]) < 1e-12);
            assert.ok(Math.abs((clip[1] / depth + 1) / 2 - uv[1]) < 1e-12);
        }
    }
});

test("receiver-plane thickness is finite, monotonic and bounded by the AO diameter", () => {
    for (const radius of [0.001, 0.05, 0.85, 10]) for (const height of [0.0001, 0.01, 1, 100]) for (const rayNormal of [-1, -0.5, 0, 0.5, 1]) {
        let previous = 0;
        for (const footprint of [0, 0.0001, 0.1, 10]) {
            const thickness = Math.min(radius * 2, Math.max(2 * footprint, height / Math.max(-rayNormal, 0.125)));
            assert.ok(Number.isFinite(thickness) && thickness > 0 && thickness <= radius * 2);
            assert.ok(thickness >= previous);
            previous = thickness;
        }
    }
});

const dot = (a, b) => a.reduce((sum, value, i) => sum + value * b[i], 0);
function clipSlab(delta, ray, thickness, radius) {
    const b = dot(delta, ray);
    const discriminant = b * b - dot(delta, delta) + radius * radius;
    if (discriminant <= 0) return [0, 0];
    const extent = Math.sqrt(discriminant);
    return [Math.max(0, -b - extent), Math.min(thickness, -b + extent)];
}
test("slab clipping retains an outside front surface whose interior crosses the AO sphere", () => {
    assert.deepEqual(clipSlab([0, 0, -1.5], [0, 0, 1], 2, 1), [0.5, 2]);
    assert.deepEqual(clipSlab([0, 0, 0], [0, 0, 1], 2, 1), [0, 1]);
    for (const [delta, ray, thickness] of [
        [[0, 0, -1.5], [0, 0, 1], 0.2], // Slab stops before sphere.
        [[0, 0, 1.5], [0, 0, 1], 2], // Slab moves away.
        [[2, 0, -1.5], [0, 0, 1], 2], // Ray misses sphere.
        [[1, 0, -1.5], [0, 0, 1], 2], // Tangent has zero solid angle.
        [[0, 0, 0], [0, 0, 1], 0],
    ]) {
        const [lo, hi] = clipSlab(delta, ray, thickness, 1);
        assert.ok(hi <= lo);
    }
    // Compare segment membership to a separate brute-force sphere test.
    for (const sign of [-1, 1]) for (const x of [0, 0.3, 0.9, 1.1]) for (const z of [-1.5, -0.5, 0.5, 1.5]) {
        const delta = [x, 0, z * sign], ray = [0, 0, sign];
        const [lo, hi] = clipSlab(delta, ray, 2, 1);
        for (let i = 0; i < 100; i++) {
            const t = (i + 0.5) / 50;
            const point = delta.map((value, j) => value + ray[j] * t);
            assert.equal(t > lo && t < hi, dot(point, point) < 1);
        }
    }
});

test("F16 only requests an explicit device feature and marks selected BRDF functions", () => {
    const c = context();
    vm.runInContext(f16Source, c);
    assert.deepEqual(Array.from(c.__nativePlaygroundRequiredFeatures), ["shader-f16"]);
    const shader = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL.pbrBRDFFunctions;
    assert.match(shader, /fn portalFp16Pow5\(value: f16\)/);
    assert.match(shader, /fn portalDiffuseBurleyH\(dots: vec3h, roughness: f16\)/);
    const includes = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    assert.match(includes.pbrDirectLightingFunctions, /f32\(diffuseTerm \* dots.x\) \* info.attenuation \* lightColor/);
    assert.match(includes.pbrBlockReflectivity, /let colorF0 = mix\(dielectricColorF0, baseColor, metallic\)/);
    assert.match(includes.pbrBlockReflectivity, /outParams.roughness = 1.0 - microSurface/);
    for (const name of ["normalDistributionFunction_TrowbridgeReitzGGX", "smithVisibility_GGXCorrelated", "diffuseBRDF_EON"]) {
        const pattern = new RegExp("fn " + name + "\\([^]*?\\{[^}]*\\}");
        assert.equal(shader.match(pattern)[0], B.ShaderStore.IncludesShadersStoreWGSL.pbrBRDFFunctions.match(pattern)[0]);
    }
    assert.throws(() => c.__nativePlaygroundConfigureEngine({ enabledExtensions: [] }), /requested device/);
});

test("F16 compile directive precedes generated structs and leaves unrelated stages unchanged", () => {
    const c = context();
    vm.runInContext(f16Source, c);
    const processor = { finalizeShaders(vertex, fragment) { return { vertexCode: vertex, fragmentCode: "struct X {}\n" + fragment }; } };
    c.__nativePlaygroundConfigureEngine({ enabledExtensions: ["shader-f16"], _getShaderProcessor: () => processor });
    const result = processor.finalizeShaders("vertex", "fn portalFp16Pow5() {}");
    assert.equal(result.vertexCode, "vertex");
    assert.ok(result.fragmentCode.startsWith("enable f16;\nstruct X"));
    assert.ok(processor.finalizeShaders("fn portalFp16Pow5() {}", "unrelated").vertexCode.startsWith("enable f16;\n"));
    assert.equal(processor.finalizeShaders("vertex", "unrelated").fragmentCode, "struct X {}\nunrelated");
});

test("bounded F16 Fresnel and Burley arithmetic stays finite and close to F32", () => {
    const half = Math.f16round;
    const pow5 = value => {
        const x = half(value);
        const squared = half(x * x);
        return half(half(squared * squared) * x);
    };
    for (let a = 0; a <= 16; a++) for (let b = 0; b <= 16; b++) for (let c = 0; c <= 16; c++) {
        const x = a / 16, y = b / 16, z = c / 16;
        const fresnel = half(half(y) + half(half(half(z) - half(y)) * pow5(half(1 - half(x)))));
        assert.ok(Math.abs(fresnel - (y + (z - y) * (1 - x) ** 5)) < 0.001);
        for (let d = 0; d <= 16; d++) {
            const roughness = d / 16;
            const f90 = half(0.5 + half(half(half(2 * z) * z) * half(roughness)));
            const burley = half(half(half(1 + half(half(f90 - 1) * pow5(1 - x))) *
                half(1 + half(half(f90 - 1) * pow5(1 - y)))) * half(1 / Math.PI));
            const reference = (1 + (0.5 + 2 * z * z * roughness - 1) * (1 - x) ** 5) *
                (1 + (0.5 + 2 * z * z * roughness - 1) * (1 - y) ** 5) / Math.PI;
            assert.ok(Math.abs(burley - reference) < 0.002);
        }
    }
});

test("coherent metallic reflectivity preserves dielectric/conductor endpoints and finite energy", () => {
    const h = Math.f16round;
    const mix = (a, b, t) => h(h(a * h(1 - t)) + h(b * t));
    for (const metallic of [0, 0.01, 0.5, 0.99, 1]) for (const weight of [0, 0.3, 1]) {
        for (const base of [0, 0.1, 0.7, 1]) for (const specular of [0, 0.2, 1]) for (const f0 of [0, 0.04, 1]) {
            const dielectric = h(h(h(f0) * h(weight)) * h(specular));
            const reflectance = mix(dielectric, h(base), h(metallic));
            const reference = f0 * weight * specular * (1 - metallic) + base * metallic;
            assert.ok(Number.isFinite(reflectance) && Math.abs(reflectance - reference) < 0.002);
            if (metallic === 0) assert.equal(reflectance, dielectric);
            if (metallic === 1) assert.equal(reflectance, h(base));
            const albedo = h(h(h(base) * h(1 - dielectric)) * h(1 - h(metallic)));
            assert.ok(albedo >= 0 && albedo <= 1);
            if (metallic === 1) assert.equal(albedo, 0);
            if (weight === 0 && metallic === 0) assert.equal(albedo, h(base));
        }
    }
});

test("coherent blocks keep sensitive full-precision paths and original fallback source", () => {
    const c = context();
    const original = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL.pbrBlockReflectivity;
    vm.runInContext(f16Source, c);
    const includes = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    const originalBody = original.slice(original.indexOf("{var outParams:" ) + 1).replace(/return outParams;\}\s*$/, "return outParams;");
    assert.ok(includes.pbrBlockReflectivity.includes(originalBody));
    assert.match(includes.pbrBlockReflectivity, /!defined\(DETAIL\) && !defined\(MICROSURFACEMAP\) && DEBUGMODE == 0/);
    assert.match(includes.pbrBlockReflectivity, /var ior = reflectivityColor.b/);
    assert.doesNotMatch(includes.pbrBlockReflectivity, /f16\(metallicRoughness.y\)/);
    assert.match(includes.pbrDirectLightingFunctions, /diffuseBRDF_EON\(clampedAlbedo, info.diffuseRoughness/);
    assert.doesNotMatch(includes.pbrDirectLightingFunctions, /f16\(info.attenuation\)|vec3h\(lightColor\)/);
});

test("GT-VBAO and F16 compose without changing AO precision or losing variant attribution", () => {
    const c = context();
    vm.runInContext(projectionSource, c);
    vm.runInContext(aoSource, c);
    const ao = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    vm.runInContext(f16Source, c);
    assert.equal(c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, ao);
    assert.equal(c.__nativeArPortalBenchmarkLabel, "gtvbao-slab-v4-f16-blocks-v3");
});

test("F16 rejects repeated installation without mutating the existing shader", () => {
    const c = context();
    vm.runInContext(f16Source, c);
    const shader = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL.pbrBRDFFunctions;
    assert.throws(() => vm.runInContext(f16Source, c), /only once/);
    assert.equal(c.BABYLON.ShaderStore.IncludesShadersStoreWGSL.pbrBRDFFunctions, shader);
});

test("F16 source mismatch fails before changing shader or engine setup", () => {
    const c = context();
    const includes = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    includes.pbrBRDFFunctions = includes.pbrBRDFFunctions.replace("fn diffuseBRDF_Burley", "fn renamed");
    const original = includes.pbrBRDFFunctions;
    assert.throws(() => vm.runInContext(f16Source, c), /no longer matches function diffuseBRDF_Burley/);
    assert.equal(includes.pbrBRDFFunctions, original);
    assert.equal(c.__nativePlaygroundConfigureEngine, undefined);
});

for (const name of ["pbrBRDFFunctions", "pbrDirectLightingFunctions", "pbrBlockReflectivity"]) {
    test(`F16 reports a missing ${name} without partial installation`, () => {
        const c = context();
        const includes = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL;
        delete includes[name];
        const before = { ...includes };
        assert.throws(() => vm.runInContext(f16Source, c), /requires the published WGSL BRDF include|no longer matches/);
        assert.deepEqual(includes, before);
        assert.equal(c.__nativePlaygroundConfigureEngine, undefined);
        assert.equal(c.__nativePlaygroundRequiredFeatures, undefined);
    });
}

for (const [include, token] of [["pbrDirectLightingFunctions", "fn computeDiffuseLighting"], ["pbrBlockReflectivity", ")->reflectivityOutParams"]]) {
    test(`F16 drift in ${include} leaves all three includes unchanged`, () => {
        const c = context();
        const includes = c.BABYLON.ShaderStore.IncludesShadersStoreWGSL;
        includes[include] = includes[include].replace(token, "renamed");
        const before = { ...includes };
        assert.throws(() => vm.runInContext(f16Source, c), /no longer matches/);
        assert.deepEqual(includes, before);
        assert.equal(c.__nativePlaygroundConfigureEngine, undefined);
    });
}
