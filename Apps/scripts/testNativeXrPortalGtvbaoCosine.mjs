import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(import.meta.url);
const B = require("babylonjs/babylon.max.js");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_gtvbao_cosine_validation.js", import.meta.url), "utf8");

function context(transform = (store) => store, transformIncludes = (includes) => includes) {
    const published = B.ShaderStore.ShadersStoreWGSL;
    const store = transform({ ssao2PixelShader: published.ssao2PixelShader, ssaoCombinePixelShader: published.ssaoCombinePixelShader });
    const includes = transformIncludes({ pbrBlockPrePass: B.ShaderStore.IncludesShadersStoreWGSL.pbrBlockPrePass });
    // The arithmetic tests evaluate the 32-bit shader; half precision, the default, has its own test.
    return vm.createContext({ __nativeArPortalAoPrecision: "full", BABYLON: { ShaderStore: { ShadersStoreWGSL: store, IncludesShadersStoreWGSL: includes }, Camera: B.Camera, Constants: B.Constants, ShaderLanguage: B.ShaderLanguage } });
}

// The body of a WGSL function in a shader string.
function functionBody(shader, name) {
    const start = shader.indexOf("fn " + name + "(");
    assert.ok(start >= 0, name);
    const open = shader.indexOf("{", start);
    let depth = 0;
    for (let i = open; i < shader.length; i++) {
        depth += shader[i] === "{" ? 1 : shader[i] === "}" ? -1 : 0;
        if (depth === 0) return shader.slice(open + 1, i);
    }
    throw new Error("unterminated " + name);
}

function install() {
    const c = context();
    vm.runInContext(source, c);
    return c;
}

// Evaluates the shader's own cumulative-distribution code, not a copy of it.
function sliceCdf(shader) {
    const body = (name) => functionBody(shader, name);
    const toJs = (text) => text.replace(/\blet\b/g, "const").replace(/(\d+\.\d+(?:e-?\d+)?)/g, "$1");
    const helpers = "const abs = Math.abs, sqrt = Math.sqrt, max = Math.max; const inverseSqrt = (x) => 1 / Math.sqrt(x);" +
        "const clamp = (x, a, b) => Math.min(Math.max(x, a), b); const select = (f, t, c) => (c ? t : f);" +
        "const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];";
    const acos = new Function("x", helpers + toJs(body("portalAcos")));
    const cdf = new Function("portalAcos", "x", "y", "sinGamma", "cosGamma", "lower", "inverseTotal", helpers + toJs(body("portalSliceCdf")));
    // Takes a direction with the view vector and the slice tangent it is measured against.
    const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return { acos, cdf: (delta, view, tangent, ...slice) => cdf(acos, dot(delta, view), dot(delta, tangent), ...slice) };
}

test("installs the geometry, AO, filter and upsample shaders together", () => {
    const c = install();
    const store = c.BABYLON.ShaderStore.ShadersStoreWGSL;
    assert.equal(c.__nativeArPortalBenchmarkLabel, "gtvbao-cosine-v5");
    assert.equal(c.__nativeArPortalSSAORadius, 0.5);
    assert.match(store.portalAoGeometryPixelShader, /uniform normalToView: mat3x3f;/);
    assert.match(store.ssao2PixelShader, /const PORTAL_SLICE_COUNT: u32 = 3u;\s*const PORTAL_STEPS_PER_SIDE: u32 = 2u;/);
    // Sixteen ordered phases need a filter that weights every residue modulo four equally.
    assert.match(store.ssao2PixelShader, /for \(var i = -2; i <= 2; i\+\+\)/);
    assert.match(store.ssao2PixelShader, /select\(2\.0, 1\.0, abs\(i\) == 2\)/);
    assert.match(store.ssaoCombinePixelShader, /var ssaoColor: vec4f = vec4f\(portalUpsampleAo\(uv\)\);/);
    assert.match(store.ssaoCombinePixelShader, /return mix\(portalSurfaceAo\(uv\), 0\.8500, coverage\);/);
    // Texels whose depth belongs to a cutout neither receive nor occlude, and no filter or upsample tap reads them.
    assert.equal(store.ssao2PixelShader.match(/dot\((sampleGeometry|tapGeometry)\.xyz, \1\.xyz\) < 0\.5/g).length, 2);
    assert.match(store.ssaoCombinePixelShader, /tapDepth == 0\.0 \|\| dot\(geometry\.xyz, geometry\.xyz\) < 0\.5/);
    assert.match(store.portalAoGeometryPixelShader, /if \(farLength < 0\.6\)/);
    assert.equal(store.ssaoCombinePixelShader.match(/@fragment/g).length, 1);
    // The AO pass reads only the AO-resolution geometry it is handed.
    const ao = store.ssao2PixelShader.slice(0, store.ssao2PixelShader.indexOf("#else"));
    assert.doesNotMatch(ao, /depthSampler|normalSampler|randomSampler/);
});

test("a radius chosen before loading is kept", () => {
    const c = context();
    c.__nativeArPortalSSAORadius = 0.85;
    vm.runInContext(source, c);
    assert.equal(c.__nativeArPortalSSAORadius, 0.85);
});

test("sample layouts set the slice and step counts and how the two sides are read", () => {
    const build = (layout) => {
        const c = context();
        if (layout) vm.runInContext(readFileSync(new URL(`../Playground/Scripts/native_xr_portal_ao_layout_${layout}.js`, import.meta.url), "utf8"), c);
        vm.runInContext(source, c);
        return c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    };
    const paired = /for \(var side = -1\.0; side <= 1\.0; side \+= 2\.0\) \{/;
    const alternating = /let side = select\(-1\.0, 1\.0, \(\(stepIndex \+ slice \+ cell\.x \+ cell\.y\) & 1u\) == 0u\);/;
    for (const [layout, slices, steps, pattern, other] of [
        [undefined, 3, 2, paired, alternating], ["2x3", 2, 3, paired, alternating], ["2x2", 2, 2, paired, alternating],
        ["8x4", 8, 4, paired, alternating], ["2x5", 2, 5, alternating, paired],
    ]) {
        const shader = build(layout);
        assert.ok(shader.includes(`const PORTAL_SLICE_COUNT: u32 = ${slices}u;\nconst PORTAL_STEPS_PER_SIDE: u32 = ${steps}u;`), String(layout));
        assert.match(shader, pattern);
        assert.doesNotMatch(shader, other);
    }
    const c = context();
    c.__nativeArPortalAoSampleLayout = "4x4";
    const before = { ...c.BABYLON.ShaderStore.ShadersStoreWGSL };
    assert.throws(() => vm.runInContext(source, c), /Unknown portal AO sample layout: 4x4/);
    assert.deepEqual({ ...c.BABYLON.ShaderStore.ShadersStoreWGSL }, before);
});

test("slices are rotated by constants and samples are reconstructed without dividing", () => {
    const build = (layout) => {
        const c = context();
        if (layout) c.__nativeArPortalAoSampleLayout = layout;
        vm.runInContext(source, c);
        const shader = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
        return shader.slice(0, shader.indexOf("#else")).replace(/\/\/.*/g, "");
    };
    for (const [layout, slices] of [[undefined, 3], ["2x2-paired", 2], ["8x4-paired", 8]]) {
        const ao = build(layout);
        const step = /let nextTangent = tangent \* (\d\.\d+) \+ crossTangent \* (\d\.\d+);\s*crossTangent = crossTangent \* (\d\.\d+) - tangent \* (\d\.\d+);\s*tangent = nextTangent;/.exec(ao);
        assert.ok(step, String(layout));
        assert.ok(Math.abs(Number(step[1]) - Math.cos(Math.PI / slices)) < 1e-8 && step[1] === step[3]);
        assert.ok(Math.abs(Number(step[2]) - Math.sin(Math.PI / slices)) < 1e-8 && step[2] === step[4]);
        // One sine and cosine per texel, for the ordered phase; none per slice.
        assert.equal(ao.match(/\bcos\(/g).length, 2);
        assert.equal(ao.match(/\bsin\(/g).length, 2);
        // Per sample, the only divisions written out are the step spacing by a constant and the perspective divide.
        const loop = ao.slice(ao.indexOf("for (var stepIndex"), ao.indexOf("occlusion +="));
        assert.deepEqual(loop.match(/\/ [^;]*/g).map((division) => division.slice(0, 12)), ["/ f32(PORTAL", "/ clip.w) + "]);
        assert.match(loop, /let sampleRay = vec3f\(vec2f\(pixel\) \* rayScale \+ rayOffset, depthSign\);/);
    }
    // Depth times the ray through a texel is the view position the other passes reconstruct.
    const ao = build();
    assert.match(ao, /let rayScale = 2\.0 \* inverseSize \* vec2f\(uniforms\.xViewport, uniforms\.yViewport\);/);
    assert.match(ao, /let rayOffset = \(inverseSize - 1\.0 - uniforms\.projection\[2\]\.xy \* depthSign\) \* vec2f\(uniforms\.xViewport, uniforms\.yViewport\);/);
    const size = [806, 372], viewport = [0.61, 0.28], center = [0.013, -0.02];
    for (const [pixel, depth, sign] of [[[0, 0], 3.5, 1], [[805, 371], 40, -1], [[400, 100], 0.7, 1]]) {
        for (const axis of [0, 1]) {
            const uv = (pixel[axis] + 0.5) / size[axis];
            const shared = (uv * 2 - 1 - center[axis] * sign) * viewport[axis] * depth;
            const ray = pixel[axis] * (2 / size[axis]) * viewport[axis] + (1 / size[axis] - 1 - center[axis] * sign) * viewport[axis];
            assert.ok(Math.abs(ray * depth - shared) < 1e-12);
        }
    }
});

test("half precision, the default, evaluates the same slab and slice distribution on 16-bit floats", () => {
    const c = context();
    delete c.__nativeArPortalAoPrecision;
    vm.runInContext(source, c);
    const shader = c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    // Reconstruction, the plane test and the thickness stay f32; only the slab and the slice angles narrow.
    assert.match(shader, /let delta = sampleRay \* sampleDepth - position;/);
    assert.match(shader, /let offset = vec3h\(delta \* inverseRadius\);/);
    assert.match(shader, /let far = min\(f16\(thickness \* inverseRadius\), extent - b\);/);
    assert.doesNotMatch(shader, /portalClipSlab\(delta, ray|enable f16/);
    const full = sliceCdf(install().BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader).cdf;
    const body = functionBody(shader, "portalSliceCdfHalf").replace(/(\d)h\b/g, "$1").replace(/\blet\b/g, "const")
        .replace("point * select(", "scale(point, select(").replace("0.015625);", "0.015625));");
    const half = new Function("point", "slice", "const abs = Math.abs, sqrt = Math.sqrt, max = Math.max; const inverseSqrt = (x) => 1 / Math.sqrt(x);" +
        "const clamp = (x, a, b) => Math.min(Math.max(x, a), b); const select = (f, t, c) => (c ? t : f);" +
        "const dot = (a, b) => a.x * b.x + a.y * b.y; const scale = (a, k) => ({ x: a.x * k, y: a.y * k });" + body);
    for (const gamma of [-1.2, 0, 0.4, 1.1]) {
        const sinGamma = Math.sin(gamma);
        const cosGamma = Math.cos(gamma);
        const total = cosGamma + gamma * sinGamma;
        const lower = 0.25 * cosGamma + 0.5 * (gamma - Math.PI / 2) * sinGamma;
        // From contact distance to the full radius, in units of the radius.
        for (const length of [0.0004, 0.01, 0.3, 1]) {
            for (let theta = -3; theta <= 3; theta += 0.05) {
                const x = length * Math.cos(theta);
                const y = length * Math.sin(theta);
                const expected = full([y, 0, x], [0, 0, 1], [1, 0, 0], sinGamma, cosGamma, lower, 1 / total);
                assert.ok(Math.abs(half({ x, y }, { x: sinGamma, y: cosGamma, z: lower, w: 1 / total }) - expected) < 1e-9, `${gamma} ${length} ${theta}`);
            }
        }
    }
    // The enable directive goes on the finished AO shader only, once.
    const processor = { finalizeShaders: (vertexCode, fragmentCode) => ({ vertexCode, fragmentCode }) };
    c.__nativePlaygroundConfigureEngine({ enabledExtensions: ["shader-f16"], _getShaderProcessor: () => processor });
    assert.deepEqual(Array.from(c.__nativePlaygroundRequiredFeatures), ["shader-f16"]);
    assert.equal(processor.finalizeShaders("v", shader).fragmentCode, "enable f16;\n" + shader);
    assert.equal(processor.finalizeShaders("v", "enable f16;\n" + shader).fragmentCode, "enable f16;\n" + shader);
    assert.deepEqual(processor.finalizeShaders("v", "fn main() {}"), { vertexCode: "v", fragmentCode: "fn main() {}" });
    const explicit = context();
    explicit.__nativeArPortalAoPrecision = "half";
    vm.runInContext(source, explicit);
    assert.throws(() => explicit.__nativePlaygroundConfigureEngine({ enabledExtensions: [] }), /requires shader-f16/);
    const unknown = context();
    unknown.__nativeArPortalAoPrecision = "quarter";
    assert.throws(() => vm.runInContext(source, unknown), /Unknown portal AO precision: quarter/);
    // Full precision needs no feature.
    const plain = context();
    delete plain.__nativeArPortalAoPrecision;
    vm.runInContext(readFileSync(new URL("../Playground/Scripts/native_xr_portal_ao_full_precision.js", import.meta.url), "utf8"), plain);
    vm.runInContext(source, plain);
    assert.doesNotMatch(plain.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, /vec3h|f16|portalSliceCdfHalf/);
    assert.equal(plain.__nativePlaygroundRequiredFeatures, undefined);
    c.__nativePlaygroundConfigureEngine({ enabledExtensions: [] });
    assert.equal(c.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader, plain.BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader);
});

test("the bilinear switch keeps the published AO read and adds only the cutout visibility", () => {
    const c = context();
    vm.runInContext(readFileSync(new URL("../Playground/Scripts/native_xr_portal_ao_bilinear_upsample.js", import.meta.url), "utf8"), c);
    vm.runInContext(source, c);
    const store = c.BABYLON.ShaderStore.ShadersStoreWGSL;
    const published = B.ShaderStore.ShadersStoreWGSL.ssaoCombinePixelShader;
    const read = /var ssaoColor:\s*vec4f\s*=\s*textureSample\(textureSampler,\s*textureSamplerSampler,\s*uv\);/.exec(published)[0];
    assert.ok(store.ssaoCombinePixelShader.includes(read + "\nssaoColor = vec4f(mix(ssaoColor.r, 0.8500, portalCutoutCoverage(uv)));"));
    assert.doesNotMatch(store.ssaoCombinePixelShader, /portalUpsampleAo|geometrySampler|depthSampler/);
    assert.equal(store.ssaoCombinePixelShader.match(/@fragment/g).length, 1);
    assert.match(store.ssao2PixelShader, /fn portalSliceCdf/);
    // The filter is centered on the texel, so the plain read needs no offset.
    assert.match(store.ssao2PixelShader, /for \(var i = -2; i <= 2; i\+\+\)/);
});

test("fails closed on a drifted SSAO layout, combine contract or pre-pass normal output", () => {
    const same = (value) => value;
    for (const [drift, driftIncludes] of [
        [(store) => ({ ...store, ssao2PixelShader: store.ssao2PixelShader.replace("#ifdef SSAO", "#ifdef OTHER") }), same],
        [(store) => ({ ...store, ssaoCombinePixelShader: store.ssaoCombinePixelShader.replace("textureSample(textureSampler", "textureSampleLevel(textureSampler") }), same],
        [(store) => ({ ...store, ssaoCombinePixelShader: store.ssaoCombinePixelShader + "\n@fragment\n" }), same],
        [(store) => ({ ...store, ssaoCombinePixelShader: undefined }), same],
        [same, (includes) => ({ pbrBlockPrePass: includes.pbrBlockPrePass.replace("vec4f(normalW,", "vec4f(normalV,") })],
        [same, (includes) => ({ pbrBlockPrePass: includes.pbrBlockPrePass + includes.pbrBlockPrePass })],
        [same, () => ({})],
    ]) {
        const c = context(drift, driftIncludes);
        const before = { ...c.BABYLON.ShaderStore.ShadersStoreWGSL };
        const includesBefore = { ...c.BABYLON.ShaderStore.IncludesShadersStoreWGSL };
        assert.throws(() => vm.runInContext(source, c), /Portal AO requires/);
        assert.deepEqual({ ...c.BABYLON.ShaderStore.ShadersStoreWGSL }, before);
        assert.deepEqual({ ...c.BABYLON.ShaderStore.IncludesShadersStoreWGSL }, includesBefore);
    }
});

test("cutout materials write a zero normal blended by their alpha, other materials their normal", () => {
    const published = B.ShaderStore.IncludesShadersStoreWGSL.pbrBlockPrePass;
    const block = install().BABYLON.ShaderStore.IncludesShadersStoreWGSL.pbrBlockPrePass;
    const marker = "\n#if defined(ALPHABLEND) || defined(ALPHATEST)\nfragData[PREPASS_NORMAL_INDEX] = vec4f(vec3f(0.0), finalColor.a);\n#else\n";
    // Only the world-space and the view-space normal outputs are wrapped.
    let expected = published;
    for (const write of [
        /fragData\[PREPASS_NORMAL_INDEX\]\s*=\s*vec4f\(normalW,\s*writeGeometryInfo\);/,
        /fragData\[PREPASS_NORMAL_INDEX\]\s*=\s*vec4f\(normalize\(\(scene\.view\s*\*\s*vec4f\(normalW,\s*0\.0\)\)\.rgb\),\s*writeGeometryInfo\);/,
    ]) {
        const original = write.exec(published)[0];
        expected = expected.replace(original, marker + original + "\n#endif\n");
    }
    assert.notEqual(expected, published);
    assert.equal(block, expected);
});

test("cutout coverage is the share of the normal that the cutouts removed", () => {
    const combine = install().BABYLON.ShaderStore.ShadersStoreWGSL.ssaoCombinePixelShader;
    const expression = /return (clamp\(.*\));/.exec(functionBody(combine, "portalCutoutCoverage"))[1];
    const coverage = new Function("normal", "const clamp = (x, a, b) => Math.min(Math.max(x, a), b), min = Math.min; const length = (v) => Math.hypot(...v); return " + expression + ";");
    const texel = (normalLength, alpha) => ({ xyz: [0, normalLength, 0], a: alpha });
    // An opaque surface, including half-float rounding of its unit normal.
    assert.equal(coverage(texel(1, 1)), 0);
    assert.equal(coverage(texel(0.995, 1)), 0);
    // Nothing drawn.
    assert.equal(coverage(texel(0, 0)), 0);
    // One cutout of alpha 0.3 over an opaque surface (alpha accumulates past one), and over nothing.
    assert.ok(Math.abs(coverage(texel(0.7, 1.3)) - 0.3) < 0.03);
    assert.ok(Math.abs(coverage(texel(0, 0.3)) - 0.3) < 0.03);
    // Two cutouts of alpha 0.5 over an opaque surface, and a fully covering one.
    assert.ok(Math.abs(coverage(texel(0.25, 2)) - 0.75) < 0.03);
    assert.equal(coverage(texel(0, 1)), 1);
    let previous = 0;
    for (let alpha = 0; alpha <= 1.0001; alpha += 0.05) {
        const value = coverage(texel(1 - alpha, 1 + alpha));
        assert.ok(value >= previous && value <= 1);
        previous = value;
    }
});

test("the cutout visibility is configurable and validated", () => {
    const c = context();
    c.__nativeArPortalCutoutAoVisibility = 0.9;
    vm.runInContext(source, c);
    assert.match(c.BABYLON.ShaderStore.ShadersStoreWGSL.ssaoCombinePixelShader, /mix\(portalSurfaceAo\(uv\), 0\.9000, coverage\)/);
    for (const invalid of [0, -0.1, 1.5, "none"]) {
        const rejected = context();
        rejected.__nativeArPortalCutoutAoVisibility = invalid;
        const before = { ...rejected.BABYLON.ShaderStore.ShadersStoreWGSL };
        assert.throws(() => vm.runInContext(source, rejected), /cutout visibility/);
        assert.deepEqual({ ...rejected.BABYLON.ShaderStore.ShadersStoreWGSL }, before);
    }
});

test("the ordered sequence gives every 4x4 block all sixteen phases", () => {
    const shader = install().BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    const expression = /fn portalOrdered\(cell: vec2u\) -> f32 \{\s*let q = cell\.x \^ cell\.y;\s*return \(f32\((.*)\) \+ 0\.5\) \/ 16\.0;/.exec(shader);
    assert.ok(expression);
    const index = new Function("cell", "q", "return " + expression[1].replace(/(\d)u/g, "$1") + ";");
    const direction = new Set();
    const offset = new Set();
    for (let y = 0; y < 4; y++) {
        for (let x = 0; x < 4; x++) {
            direction.add(index({ x, y }, x ^ y));
            // The radial offset reads the same sequence at the rotated cell.
            offset.add(index({ x: 3 - y, y: x }, (3 - y) ^ x));
        }
    }
    assert.equal(direction.size, 16);
    assert.equal(offset.size, 16);
});

test("the slice distribution is the cosine-weighted cumulative integral", () => {
    const { cdf } = sliceCdf(install().BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader);
    const view = [0, 0, 1];
    const tangent = [1, 0, 0];
    for (const gamma of [-1.2, -0.6, 0, 0.4, 1.1]) {
        const sinGamma = Math.sin(gamma);
        const cosGamma = Math.cos(gamma);
        const total = cosGamma + gamma * sinGamma;
        const lower = 0.25 * cosGamma + 0.5 * (gamma - Math.PI / 2) * sinGamma;
        const weight = (t) => Math.cos(t - gamma) * Math.abs(Math.sin(t));
        let integral = 0;
        let previous = -1;
        const steps = 2000;
        const start = gamma - Math.PI / 2;
        for (let i = 0; i <= steps; i++) {
            const theta = start + (Math.PI * i) / steps;
            if (i > 0) integral += weight(theta - Math.PI / (2 * steps)) * (Math.PI / steps);
            const value = cdf([Math.sin(theta), 0, Math.cos(theta)], view, tangent, sinGamma, cosGamma, lower, 1 / total);
            // The shader uses a two-term arccosine; half a bit of the 32-bit mask is ample.
            assert.ok(Math.abs(value - integral / total) < 0.012, `gamma ${gamma} theta ${theta}: ${value} vs ${integral / total}`);
            assert.ok(value >= previous - 0.004, "monotonic");
            previous = value;
        }
        // Directions behind the surface saturate at the nearer end of the slice.
        assert.equal(cdf([Math.sin(start - 0.3), 0, Math.cos(start - 0.3)], view, tangent, sinGamma, cosGamma, lower, 1 / total), 0);
        assert.equal(cdf([Math.sin(start + Math.PI + 0.3), 0, Math.cos(start + Math.PI + 0.3)], view, tangent, sinGamma, cosGamma, lower, 1 / total), 1);
    }
});

test("setup rejects cameras and pipelines it cannot reshape", () => {
    const c = install();
    const observers = [];
    const scene = (overrides) => ({
        activeCamera: { mode: B.Camera.PERSPECTIVE_CAMERA },
        onBeforeRenderObservable: { addOnce: (callback) => observers.push(callback) },
        postProcessRenderPipelineManager: { supportedPipelines: [] },
        getEngine: () => ({}),
        ...overrides,
    });
    c.__nativeArPortalStartProfile(scene({ activeCamera: { mode: B.Camera.ORTHOGRAPHIC_CAMERA } }), () => {});
    assert.throws(() => observers.pop()(), /perspective camera/);
    c.__nativeArPortalStartProfile(scene({}), () => {});
    assert.throws(() => observers.pop()(), /unallocated SSAO2 pipeline/);
    const pass = (allocated) => ({ _textures: { length: allocated ? 1 : 0 }, _options: 0.3 });
    c.__nativeArPortalStartProfile(scene({ postProcessRenderPipelineManager: { supportedPipelines: [{
        name: "native-xr-portal-ssao2", _prePassRenderer: {}, _ssaoPostProcess: pass(false),
        _blurHPostProcess: pass(false), _blurVPostProcess: pass(true), _ssaoCombinePostProcess: pass(false) }] } }), () => {});
    assert.throws(() => observers.pop()(), /unallocated SSAO2 pipeline/);
});

for (const separateGeometry of [true, false]) {
    test("each eye binds current geometry and projection; separate geometry=" + separateGeometry, () => {
        const c = install();
        Object.assign(c.BABYLON, { GeometryBufferRenderer: B.GeometryBufferRenderer, Matrix: B.Matrix, PostProcess: class {} });
        const pass = () => ({ _textures: [], _options: 0.5, onApplyObservable: new B.Observable() });
        const ao = pass(), h = pass(), v = pass(), combine = pass();
        const left = B.Matrix.Identity(), right = B.Matrix.Identity();
        left.m[8] = -0.05;
        right.m[8] = 0.05;
        let eye = 0, installed;
        const camera = { mode: B.Camera.PERSPECTIVE_CAMERA, _postProcesses: [ao, h, v, combine],
            attachPostProcess: p => { installed = p; } };
        const target = () => ({ textures: eye ? ["R-normal", "R-depth"] : ["L-depth", "L-normal"] });
        const index = (isDepth) => isDepth ? eye : 1 - eye;
        const renderer = separateGeometry ?
            { getGBuffer: target, getTextureIndex: type => index(type === B.GeometryBufferRenderer.DEPTH_TEXTURE_TYPE) } :
            { getRenderTarget: target, getIndex: type => index(type === B.Constants.PREPASS_DEPTH_TEXTURE_TYPE) };
        const pipeline = { name: "native-xr-portal-ssao2", _ssaoPostProcess: ao, _blurHPostProcess: h, _blurVPostProcess: v,
            _ssaoCombinePostProcess: combine, [separateGeometry ? "_geometryBufferRenderer" : "_prePassRenderer"]: renderer };
        const scene = { activeCamera: camera, materials: [], getEngine: () => ({}),
            postProcessRenderPipelineManager: { supportedPipelines: [pipeline] }, onBeforeRenderObservable: { addOnce: cb => cb() }, onDisposeObservable: new B.Observable() };
        c.__nativeArPortalStartProfile(scene, () => {});
        for (eye of [0, 1, 0]) {
            scene.activeCamera = { getProjectionMatrix: () => eye ? right : left };
            const bound = {};
            installed.onApply({ setTexture: (k, value) => { bound[k] = value; }, setMatrix: (k, value) => { bound[k] = value; }, setFloat() {}, setMatrix3x3() {} });
            assert.deepEqual(bound, { depthSampler: eye ? "R-depth" : "L-depth", normalSampler: eye ? "R-normal" : "L-normal", projection: eye ? right : left });
        }
        if (separateGeometry) {
            scene.activeCamera = camera;
            renderer.normalsAreUnsigned = true;
            assert.throws(() => c.__nativeArPortalStartProfile(scene, () => {}), /signed geometry-buffer normals/);
        }
    });
}

for (const retainColorCopy of [true, false]) {
    test("geometry feeds AO without replacing scene color; color copy=" + retainColorCopy, () => {
        const c = install();
        const pass = () => ({ _textures: [], _options: 0.5, onApplyObservable: new B.Observable() });
        const color = pass(), ssao = pass(), blurH = pass(), blurV = pass(), combine = pass();
        const chain = [...(retainColorCopy ? [color] : []), ssao, blurH, blurV, combine];
        const camera = { mode: B.Camera.PERSPECTIVE_CAMERA, _postProcesses: chain, attachPostProcess(p, i) { chain.splice(i, 0, p); } };
        c.BABYLON.Matrix = B.Matrix;
        c.BABYLON.PostProcess = class { constructor(name) { this.name = name; } };
        const pipeline = { name: "native-xr-portal-ssao2", _prePassRenderer: {}, _originalColorPostProcess: color,
            _ssaoPostProcess: ssao, _blurHPostProcess: blurH, _blurVPostProcess: blurV, _ssaoCombinePostProcess: combine };
        c.__nativeArPortalStartProfile({ activeCamera: camera, materials: [], getEngine: () => ({}),
            postProcessRenderPipelineManager: { supportedPipelines: [pipeline] },
            onBeforeRenderObservable: { addOnce: cb => cb() }, onDisposeObservable: new B.Observable() }, () => {});
        const geometry = chain.find(p => p.name === "portalAoGeometry");
        assert.ok(geometry);
        assert.deepEqual(chain, [...(retainColorCopy ? [color] : []), geometry, ssao, blurH, blurV, combine]);
    });
}
