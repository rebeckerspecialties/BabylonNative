// Validation only: cosine-weighted GT-VBAO evaluated on AO-resolution geometry,
// with an ordered sampling pattern, a filter matched to that pattern, and a
// surface-aware upsample. Measurements: Documentation/NativeXrPortalAoStability.md.
// Load after the projection validation prelude, before the R8 and color-reuse
// preludes and before the scene is created.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var original = store.ssao2PixelShader;
    if (typeof original !== "string" || !original.includes("#ifdef SSAO") || !original.includes("#ifdef BLUR")) {
        throw new Error("Portal AO requires the published SSAO2 shader layout.");
    }
    // Alpha cutouts and blended alpha textures (foliage, grilles, wires) are
    // lacework far finer than an AO texel. Estimated like other surfaces they
    // alias into a speckle that changes with every sub-texel of motion. They
    // stay in the pre-pass with a zero normal instead, blended by their own
    // alpha: the normal that remains is shortened by exactly their coverage.
    // The combine pass gives the covered share of a pixel one fixed visibility,
    // and the AO passes ignore texels whose depth belongs to a cutout.
    var includes = BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    var prePassBlock = includes && includes.pbrBlockPrePass;
    // The world-space and view-space normal outputs; the store may hold them with or without spaces.
    var normalWrites = [
        /fragData\[PREPASS_NORMAL_INDEX\]\s*=\s*vec4f\(normalW,\s*writeGeometryInfo\);/,
        /fragData\[PREPASS_NORMAL_INDEX\]\s*=\s*vec4f\(normalize\(\(scene\.view\s*\*\s*vec4f\(normalW,\s*0\.0\)\)\.rgb\),\s*writeGeometryInfo\);/
    ];
    if (typeof prePassBlock !== "string" || normalWrites.some(function (write) {
        return (prePassBlock.match(new RegExp(write.source, "g")) || []).length !== 1;
    })) {
        throw new Error("Portal AO requires the published PBR pre-pass normal output.");
    }
    var cutoutVisibility = Number(globalThis.__nativeArPortalCutoutAoVisibility ?? 0.85);
    if (!(cutoutVisibility > 0 && cutoutVisibility <= 1)) {
        throw new Error("Portal AO cutout visibility must be within (0, 1].");
    }
    // Slices x distance steps. "paired" reads both sides of the receiver at every
    // step; "alternating" reads one side per step and swaps sides between
    // steps, slices and neighbouring texels.
    var sampleLayouts = {
        "2x2-paired": { slices: 2, steps: 2, paired: true },
        "2x3-paired": { slices: 2, steps: 3, paired: true },
        "3x2-paired": { slices: 3, steps: 2, paired: true },
        "2x5-alternating": { slices: 2, steps: 5, paired: false },
        "8x4-paired": { slices: 8, steps: 4, paired: true }
    };
    var sampleLayoutName = String(globalThis.__nativeArPortalAoSampleLayout ?? "3x2-paired");
    var sampleLayout = sampleLayouts[sampleLayoutName];
    if (!sampleLayout) {
        throw new Error("Unknown portal AO sample layout: " + sampleLayoutName);
    }
    var sideLoop = sampleLayout.paired
        ? "for (var side = -1.0; side <= 1.0; side += 2.0) {"
        : "for (var sideIndex = 0u; sideIndex < 1u; sideIndex++) {\n" +
          "                let side = select(-1.0, 1.0, ((stepIndex + slice + cell.x + cell.y) & 1u) == 0u);";
    // "half" evaluates each sample's slab and slice angles in 16-bit floats, in
    // units of the AO radius. Reconstruction, depth and the plane tests stay f32.
    var automaticPrecision = globalThis.__nativeArPortalAoPrecision === undefined;
    var precision = globalThis.__nativeArPortalAoPrecision ?? "half";
    if (precision !== "full" && precision !== "half") {
        throw new Error("Unknown portal AO precision: " + precision);
    }
    var halfPrecision = precision === "half";
    function slabSample(half) {
        return half ? `let offset = vec3h(delta * inverseRadius);
                let rayH = vec3h(ray);
                let b = dot(offset, rayH);
                let discriminant = b * b - dot(offset, offset) + 1.0h;
                if (discriminant <= 0.0h) { continue; }
                let extent = sqrt(discriminant);
                let near = max(0.0h, -b - extent);
                let far = min(f16(thickness * inverseRadius), extent - b);
                if (far <= near) { continue; }
                let start = vec2h(dot(offset, viewH), dot(offset, tangentH));
                let along = vec2h(dot(rayH, viewH), dot(rayH, tangentH));
                mask |= portalIntervalMask(
                    f32(portalSliceCdfHalf(start + along * near, sliceH)),
                    f32(portalSliceCdfHalf(start + along * far, sliceH)), jitter);` : `let interval = portalClipSlab(delta, ray, thickness, radiusSquared);
                if (interval.y <= interval.x) { continue; }
                // The slab's two ends, as components along the view vector and the tangent.
                let start = vec2f(dot(delta, view), dot(delta, tangent));
                let along = vec2f(dot(ray, view), dot(ray, tangent));
                let front = start + along * interval.x;
                let back = start + along * interval.y;
                mask |= portalIntervalMask(
                    portalSliceCdf(front.x, front.y, sinGamma, cosGamma, lower, inverseTotal),
                    portalSliceCdf(back.x, back.y, sinGamma, cosGamma, lower, inverseTotal), jitter);`;
    }
    var halfFunctions = `
// portalSliceCdf for a point given by its view and tangent components, in
// 16-bit floats. slice is (sinGamma, cosGamma, lower, inverseTotal).
fn portalSliceCdfHalf(point: vec2h, slice: vec4h) -> f16 {
    // Only the direction matters. Contact samples are a few thousandths of the
    // radius away, and their squares would fall below the smallest normal half.
    let p = point * select(1.0h, 64.0h, max(abs(point.x), abs(point.y)) < 0.015625h);
    if (p.x * slice.y + p.y * slice.x <= 0.0h) {
        return select(0.0h, 1.0h, p.y >= 0.0h);
    }
    let inverseLength = inverseSqrt(max(dot(p, p), 0.0001h));
    let c = clamp(p.x * inverseLength, -1.0h, 1.0h);
    let s = abs(p.y) * inverseLength;
    let a = abs(c);
    let r = (1.5707963h - 0.156583h * a) * sqrt(1.0h - a);
    let angle = select(3.14159265h - r, r, c >= 0.0h);
    let cos2 = 2.0h * c * c - 1.0h;
    let sin2 = 2.0h * c * s;
    let towards = slice.z + 0.5h * slice.y - 0.25h * (cos2 * slice.y + sin2 * slice.x) + 0.5h * angle * slice.x;
    let away = slice.z + 0.25h * (cos2 * slice.y - sin2 * slice.x) + 0.5h * angle * slice.x;
    return clamp(select(away, towards, p.y >= 0.0h) * slice.w, 0.0h, 1.0h);
}
`;
    var halfSlice = `
        let viewH = vec3h(view);
        let tangentH = vec3h(tangent);
        let sliceH = vec4h(vec4f(sinGamma, cosGamma, lower, inverseTotal));`;
    // Slices are a fixed angle apart, so each is the previous one rotated by constants.
    var sliceAngle = Math.PI / sampleLayout.slices;
    var sliceCos = Math.cos(sliceAngle).toFixed(9);
    var sliceSin = Math.sin(sliceAngle).toFixed(9);
    var combine = store.ssaoCombinePixelShader;
    var aoRead = /var ssaoColor:\s*vec4f\s*=\s*textureSample\(textureSampler,\s*textureSamplerSampler,\s*uv\);/;
    if (typeof combine !== "string" || (combine.match(new RegExp(aoRead.source, "g")) || []).length !== 1) {
        throw new Error("Portal AO requires the unchanged SSAO combine AO read.");
    }

    var planeTolerance = "0.04";
    var reconstruction = `
fn portalViewPosition(uv: vec2f, depth: f32, depthSign: f32) -> vec3f {
    let xy = (uv * 2.0 - 1.0 - uniforms.projection[2].xy * depthSign) *
        vec2f(uniforms.xViewport, uniforms.yViewport) * depth;
    return vec3f(xy, depth * depthSign);
}
`;

    var fragmentEntry = /@fragment/;
    if ((combine.match(new RegExp(fragmentEntry.source, "g")) || []).length !== 1) {
        throw new Error("Portal AO requires a single SSAO combine entry point.");
    }
    // Opaque surfaces leave a unit normal with alpha one. Cutouts blend a zero
    // normal over it, and add their alpha where nothing lies behind them.
    var cutoutTest = `
var normalSamplerSampler: sampler;
var normalSampler: texture_2d<f32>;
fn portalCutoutCoverage(uv: vec2f) -> f32 {
    let dimensions = vec2i(textureDimensions(normalSampler));
    let normal = textureLoad(normalSampler, clamp(vec2i(uv * vec2f(dimensions)), vec2i(0), dimensions - 1), 0);
    return clamp((min(normal.a, 1.0) - length(normal.xyz) - 0.02) / 0.98, 0.0, 1.0);
}
`;
    var cutoutLiteral = cutoutVisibility.toFixed(4);
    // Plain bilinear magnification spreads AO across silhouettes. Each pixel
    // instead blends only the AO texels whose surface contains it. That costs
    // ten texture loads per screen pixel; "bilinear" keeps the published read.
    var surfaceAwareUpsample = globalThis.__nativeArPortalAoUpsample !== "bilinear";
    if (!surfaceAwareUpsample) {
        store.ssaoCombinePixelShader = combine.replace(fragmentEntry, cutoutTest + "@fragment")
            .replace(aoRead, "$&\nssaoColor = vec4f(mix(ssaoColor.r, " + cutoutLiteral + ", portalCutoutCoverage(uv)));");
    } else store.ssaoCombinePixelShader = combine.replace(fragmentEntry, `
var geometrySamplerSampler: sampler;
var geometrySampler: texture_2d<f32>;
var depthSamplerSampler: sampler;
var depthSampler: texture_2d<f32>;
uniform maxZ: f32;
uniform projection: mat4x4f;
uniform xViewport: f32;
uniform yViewport: f32;
${reconstruction}
${cutoutTest}
fn portalSurfaceAo(uv: vec2f) -> f32 {
    let depthDimensions = vec2i(textureDimensions(depthSampler));
    let depthPixel = clamp(vec2i(uv * vec2f(depthDimensions)), vec2i(0), depthDimensions - 1);
    let signedDepth = textureLoad(depthSampler, depthPixel, 0).r;
    let depth = abs(signedDepth);
    if (depth == 0.0 || depth >= uniforms.maxZ) { return 1.0; }
    let depthSign = sign(signedDepth);
    let position = portalViewPosition((vec2f(depthPixel) + 0.5) / vec2f(depthDimensions), depth, depthSign);
    let dimensions = vec2i(textureDimensions(textureSampler));
    let size = vec2f(dimensions);
    let coordinate = uv * size - 0.5;
    let base = vec2i(floor(coordinate));
    let fraction = coordinate - floor(coordinate);
    let tolerance = ${planeTolerance} + 0.004 * depth;
    var sum = 0.0;
    var weight = 0.0;
    for (var i = 0; i < 4; i++) {
        let corner = vec2i(i & 1, i >> 1);
        let pixel = clamp(base + corner, vec2i(0), dimensions - 1);
        let geometry = textureLoad(geometrySampler, pixel, 0);
        let tapDepth = abs(geometry.a);
        if (tapDepth == 0.0 || dot(geometry.xyz, geometry.xyz) < 0.5) { continue; }
        let tapPosition = portalViewPosition((vec2f(pixel) + 0.5) / size, tapDepth, depthSign);
        let bilinear = mix(vec2f(1.0) - fraction, fraction, vec2f(corner));
        if (abs(dot(geometry.xyz, position - tapPosition)) > tolerance) { continue; }
        let tapWeight = bilinear.x * bilinear.y + 1e-3;
        sum += tapWeight * textureLoad(textureSampler, pixel, 0).r;
        weight += tapWeight;
    }
    // A surface too small to exist at AO resolution receives no occlusion.
    return select(1.0, sum / max(weight, 1e-6), weight > 0.0);
}
fn portalUpsampleAo(uv: vec2f) -> f32 {
    let coverage = portalCutoutCoverage(uv);
    if (coverage > 0.999) { return ${cutoutLiteral}; }
    return mix(portalSurfaceAo(uv), ${cutoutLiteral}, coverage);
}
@fragment`).replace(aoRead, "var ssaoColor: vec4f = vec4f(portalUpsampleAo(uv));");

    // One AO texel covers several pre-pass texels. Point sampling one of them
    // aliases fine detail into the AO grid, so each AO texel stores the surface
    // shared by the farthest tap and every tap on its tangent plane.
    store.portalAoGeometryPixelShader = `
varying vUV: vec2f;
var textureSamplerSampler: sampler;
var textureSampler: texture_2d<f32>;
var depthSamplerSampler: sampler;
var depthSampler: texture_2d<f32>;
var normalSamplerSampler: sampler;
var normalSampler: texture_2d<f32>;
uniform maxZ: f32;
uniform projection: mat4x4f;
uniform xViewport: f32;
uniform yViewport: f32;
uniform normalToView: mat3x3f;
${reconstruction}
@fragment
fn main(input: FragmentInputs) -> FragmentOutputs {
    let dimensions = vec2i(textureDimensions(depthSampler));
    let size = vec2f(dimensions);
    let center = input.vUV * size;
    let quarter = 0.25 * size * vec2f(abs(dpdx(input.vUV.x)), abs(dpdy(input.vUV.y)));
    var positions: array<vec3f, 4>;
    var normals: array<vec3f, 4>;
    var depths: array<f32, 4>;
    var farIndex = -1;
    var farDepth = 0.0;
    var depthSign = 1.0;
    for (var i = 0; i < 4; i++) {
        let offset = vec2f(select(-1.0, 1.0, (i & 1) == 1), select(-1.0, 1.0, i >= 2)) * quarter;
        let pixel = clamp(vec2i(center + offset), vec2i(0), dimensions - 1);
        let signedDepth = textureLoad(depthSampler, pixel, 0).r;
        var depth = abs(signedDepth);
        if (depth >= uniforms.maxZ) { depth = 0.0; }
        let tapSign = select(1.0, -1.0, signedDepth < 0.0);
        depths[i] = depth;
        positions[i] = portalViewPosition((vec2f(pixel) + 0.5) / size, depth, tapSign);
        normals[i] = uniforms.normalToView * textureLoad(normalSampler, pixel, 0).xyz;
        if (depth > farDepth) {
            farDepth = depth;
            farIndex = i;
            depthSign = tapSign;
        }
    }
    if (farIndex < 0) {
        fragmentOutputs.color = vec4f(0.0);
        return fragmentOutputs;
    }
    let farNormal = normals[farIndex];
    let farLength = length(farNormal);
    // Depth is written where a cutout's alpha exceeds the pre-pass threshold of
    // 0.4, which leaves less than 0.6 of the normal behind it.
    if (farLength < 0.6) {
        fragmentOutputs.color = vec4f(0.0, 0.0, 0.0, depthSign * farDepth);
        return fragmentOutputs;
    }
    let plane = farNormal / farLength;
    let tolerance = ${planeTolerance} + 0.002 * farDepth;
    var reciprocal = 0.0;
    var normal = vec3f(0.0);
    var point = vec3f(0.0);
    var count = 0.0;
    for (var i = 0; i < 4; i++) {
        if (depths[i] > 0.0 && abs(dot(plane, positions[i] - positions[farIndex])) <= tolerance) {
            reciprocal += 1.0 / depths[i];
            normal += normals[i];
            point += positions[i];
            count += 1.0;
        }
    }
    normal = normalize(normal);
    // Place the texel on the accepted taps' plane along its own center ray, so a
    // partly covered texel does not take the depth of the taps' centroid.
    let ray = portalViewPosition(input.vUV, 1.0, depthSign);
    let slope = dot(normal, ray);
    var depth = count / reciprocal;
    if (abs(slope) > 0.05) {
        depth = clamp(dot(normal, point) / (count * slope), 0.5 * depth, 2.0 * depth);
    }
    fragmentOutputs.color = vec4f(normal, depthSign * depth);
    return fragmentOutputs;
}
`;

    if (halfPrecision) {
        // Babylon's finalizer prepends structs, so an enable directive inside the
        // shader would come too late; it is added to the finished AO shader.
        var configureEngine = globalThis.__nativePlaygroundConfigureEngine;
        globalThis.__nativePlaygroundConfigureEngine = function (engine) {
            if (configureEngine) configureEngine(engine);
            if (!engine.enabledExtensions.includes("shader-f16")) {
                if (automaticPrecision) {
                    precision = "full";
                    store.ssao2PixelShader = occlusionShader(false);
                    return;
                }
                throw new Error("Half-precision portal AO requires shader-f16 on the device.");
            }
            var processor = engine._getShaderProcessor(BABYLON.ShaderLanguage.WGSL);
            var finalize = processor && processor.finalizeShaders;
            if (typeof finalize !== "function") throw new Error("Half-precision portal AO requires the WebGPU WGSL finalizer.");
            processor.finalizeShaders = function (vertex, fragment) {
                var result = finalize.call(this, vertex, fragment);
                if (result.fragmentCode.includes("portalSliceCdfHalf(") && !result.fragmentCode.startsWith("enable f16;")) {
                    result.fragmentCode = "enable f16;\n" + result.fragmentCode;
                }
                return result;
            };
        };
        globalThis.__nativePlaygroundRequiredFeatures = Array.from(new Set((globalThis.__nativePlaygroundRequiredFeatures || []).concat(["shader-f16"])));
    }
    function occlusionShader(half) {
        return `
varying vUV: vec2f;
var textureSamplerSampler: sampler;
var textureSampler: texture_2d<f32>;
#ifdef SSAO
uniform radius: f32;
uniform totalStrength: f32;
uniform base: f32;
uniform maxZ: f32;
uniform projection: mat4x4f;
uniform xViewport: f32;
uniform yViewport: f32;
const PORTAL_SLICE_COUNT: u32 = ${sampleLayout.slices}u;
const PORTAL_STEPS_PER_SIDE: u32 = ${sampleLayout.steps}u;
${reconstruction}
fn portalAcos(x: f32) -> f32 {
    let a = abs(x);
    let r = (1.5707963 - 0.156583 * a) * sqrt(1.0 - a);
    return select(3.14159265 - r, r, x >= 0.0);
}

// Cumulative share of cos(theta - gamma) * |sin(theta)| over a hemisphere slice,
// with theta measured from the view vector and gamma the projected normal's angle.
// Equal steps of this value carry equal cosine-weighted solid angle.
// x and y are the direction's components along the view vector and the slice tangent.
fn portalSliceCdf(x: f32, y: f32, sinGamma: f32, cosGamma: f32, lower: f32, inverseTotal: f32) -> f32 {
    if (x * cosGamma + y * sinGamma <= 0.0) {
        return select(0.0, 1.0, y >= 0.0);
    }
    let inverseLength = inverseSqrt(max(x * x + y * y, 1e-12));
    let c = clamp(x * inverseLength, -1.0, 1.0);
    let s = abs(y) * inverseLength;
    let angle = portalAcos(c);
    let cos2 = 2.0 * c * c - 1.0;
    let sin2 = 2.0 * c * s;
    let towards = lower + 0.5 * cosGamma - 0.25 * (cos2 * cosGamma + sin2 * sinGamma) + 0.5 * angle * sinGamma;
    let away = lower + 0.25 * (cos2 * cosGamma - sin2 * sinGamma) + 0.5 * angle * sinGamma;
    return clamp(select(away, towards, y >= 0.0) * inverseTotal, 0.0, 1.0);
}

// Bits represent jittered point samples, not touched angular sectors.
fn portalIntervalMask(a: f32, b: f32, jitter: f32) -> u32 {
    let lo = u32(clamp(ceil(min(a, b) * 32.0 - jitter), 0.0, 32.0));
    let hi = u32(clamp(ceil(max(a, b) * 32.0 - jitter), 0.0, 32.0));
    return insertBits(0u, 0xffffffffu, lo, hi - lo);
}

fn portalThickness(height: f32, rayNormal: f32, footprint: f32, radius: f32) -> f32 {
    // Infer an occluder slab towards the receiving tangent plane, bounded to
    // the AO diameter. A minimum ray slope avoids unbounded grazing slabs.
    return min(radius * 2.0, max(2.0 * footprint, height / max(-rayNormal, 0.125)));
}

fn portalClipSlab(delta: vec3f, ray: vec3f, thickness: f32, radiusSquared: f32) -> vec2f {
    let b = dot(delta, ray);
    let discriminant = b * b - dot(delta, delta) + radiusSquared;
    if (discriminant <= 0.0) { return vec2f(0.0); }
    let extent = sqrt(discriminant);
    return vec2f(max(0.0, -b - extent), min(thickness, -b + extent));
}

${half ? halfFunctions : ""}
// Position of a cell in the 4x4 ordered-dither sequence. Every 4x4 block of
// AO texels holds each of the 16 phases once. A static random field would
// instead show through moving shadows as a pattern fixed to the screen.
fn portalOrdered(cell: vec2u) -> f32 {
    let q = cell.x ^ cell.y;
    return (f32(((q & 1u) << 3u) | ((cell.y & 1u) << 2u) | (q & 2u) | ((cell.y & 2u) >> 1u)) + 0.5) / 16.0;
}

@fragment
fn main(input: FragmentInputs) -> FragmentOutputs {
    let dimensions = vec2i(textureDimensions(textureSampler));
    let size = vec2f(dimensions);
    let inverseSize = 1.0 / size;
    let centerPixel = clamp(vec2i(input.vUV * size), vec2i(0), dimensions - 1);
    let geometry = textureLoad(textureSampler, centerPixel, 0);
    let depth = abs(geometry.a);
    let normal = geometry.xyz;
    if (depth == 0.0 || dot(normal, normal) < 0.5) {
        fragmentOutputs.color = vec4f(1.0);
        return fragmentOutputs;
    }
    let depthSign = sign(geometry.a);
    let position = portalViewPosition((vec2f(centerPixel) + 0.5) * inverseSize, depth, depthSign);
    let view = normalize(-position);
    let normalView = dot(normal, view);
    if (normalView <= 1e-5) {
        fragmentOutputs.color = vec4f(1.0);
        return fragmentOutputs;
    }
    let cell = vec2u(centerPixel) & vec2u(3u);
    let noise = vec2f(portalOrdered(cell), portalOrdered(vec2u(3u - cell.y, cell.x)));
    let helper = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(view.y) > 0.9);
    let basisX = normalize(cross(helper, view));
    let basisY = cross(view, basisX);
    let centerClip = uniforms.projection * vec4f(position, 1.0);
    let safeRadius = max(uniforms.radius, 1e-4);
    let radiusSquared = safeRadius * safeRadius;
    let inverseRadius = 1.0 / safeRadius;
    // Every sample is reconstructed as depth times this ray through its texel.
    let rayScale = 2.0 * inverseSize * vec2f(uniforms.xViewport, uniforms.yViewport);
    let rayOffset = (inverseSize - 1.0 - uniforms.projection[2].xy * depthSign) * vec2f(uniforms.xViewport, uniforms.yViewport);
    let phase = noise.x * (3.14159265359 / f32(PORTAL_SLICE_COUNT));
    var tangent = basisX * cos(phase) + basisY * sin(phase);
    var crossTangent = basisY * cos(phase) - basisX * sin(phase);
    let texelFootprint = 2.0 * max(abs(uniforms.xViewport) / size.x, abs(uniforms.yViewport) / size.y);
    var occlusion = 0.0;
    for (var slice = 0u; slice < PORTAL_SLICE_COUNT; slice++) {
        let normalTangent = dot(normal, tangent);
        let projectedSquared = normalTangent * normalTangent + normalView * normalView;
        let inverseProjected = inverseSqrt(projectedSquared);
        let sinGamma = normalTangent * inverseProjected;
        let cosGamma = normalView * inverseProjected;
        let gamma = sign(sinGamma) * (1.5707963 - portalAcos(abs(sinGamma)));
        let total = cosGamma + gamma * sinGamma;
        let lower = 0.25 * cosGamma + 0.5 * (gamma - 1.5707963) * sinGamma;
        let inverseTotal = 1.0 / total;
        let clipDirection = uniforms.projection * vec4f(tangent * safeRadius, 0.0);
        let jitter = fract(noise.y + f32(slice) * 0.61803398875);${half ? halfSlice : ""}
        var mask = 0u;
        for (var stepIndex = 0u; stepIndex < PORTAL_STEPS_PER_SIDE; stepIndex++) {
            let sampleNoise = fract(noise.y + f32(slice + stepIndex * PORTAL_STEPS_PER_SIDE) * 0.61803398875);
            let sampleDistance = (f32(stepIndex) + 0.5 + sampleNoise * 0.5) / f32(PORTAL_STEPS_PER_SIDE);
            ${sideLoop}
                let clip = centerClip + clipDirection * (side * sampleDistance * sampleDistance);
                if (clip.w <= 1e-6) { continue; }
                let uv = clip.xy * (0.5 / clip.w) + 0.5;
                if (any(uv < vec2f(0.0)) || any(uv >= vec2f(1.0))) { continue; }
                // Reconstruct at the actual nearest texel center, not the unsnapped ray.
                let pixel = vec2i(uv * size);
                let sampleGeometry = textureLoad(textureSampler, pixel, 0);
                let sampleDepth = abs(sampleGeometry.a);
                // Cutouts do not occlude: their coverage flips with sub-texel motion.
                if (sampleDepth == 0.0 || dot(sampleGeometry.xyz, sampleGeometry.xyz) < 0.5) { continue; }
                let sampleRay = vec3f(vec2f(pixel) * rayScale + rayOffset, depthSign);
                let delta = sampleRay * sampleDepth - position;
                let footprint = sampleDepth * texelFootprint;
                let height = dot(delta, normal);
                if (height <= max(footprint * 0.25, 1e-4)) { continue; }
                // Thickness follows this sample's camera ray, not the center pixel's ray.
                let ray = normalize(sampleRay);
                let thickness = portalThickness(height, dot(ray, normal), footprint, safeRadius);
${slabSample(half)}
            }
        }
        // Each slice carries the projected normal's length times its unoccluded integral.
        occlusion += projectedSquared * inverseProjected * total * f32(countOneBits(mask));
        let nextTangent = tangent * ${sliceCos} + crossTangent * ${sliceSin};
        crossTangent = crossTangent * ${sliceCos} - tangent * ${sliceSin};
        tangent = nextTangent;
    }
    let fade = 1.0 - smoothstep(uniforms.maxZ * 0.75, uniforms.maxZ, depth);
    let ao = clamp(1.0 - uniforms.totalStrength * fade * occlusion / (32.0 * f32(PORTAL_SLICE_COUNT)) + uniforms.base, 0.0, 1.0);
    fragmentOutputs.color = vec4f(vec3f(ao), 1.0);
    return fragmentOutputs;
}
#else
#ifdef BLUR
var depthSamplerSampler: sampler;
var depthSampler: texture_2d<f32>;
uniform projection: mat4x4f;
uniform xViewport: f32;
uniform yViewport: f32;
${reconstruction}
// One axis of the filter that averages the ordered phases: a 4-texel box
// convolved with a 2-texel box, so every phase keeps an equal share and the
// result stays centered on the texel. depthSampler is the AO-resolution
// geometry; taps off the center's tangent plane are skipped.
@fragment
fn main(input: FragmentInputs) -> FragmentOutputs {
    let dimensions = vec2i(textureDimensions(textureSampler));
    let size = vec2f(dimensions);
    let centerPixel = clamp(vec2i(input.vUV * size), vec2i(0), dimensions - 1);
    var result = textureLoad(textureSampler, centerPixel, 0).r;
#ifndef BLUR_BYPASS
    let geometry = textureLoad(depthSampler, centerPixel, 0);
    let depth = abs(geometry.a);
    let normal = geometry.xyz;
    if (depth > 0.0 && dot(normal, normal) > 0.5) {
        let depthSign = sign(geometry.a);
        let position = portalViewPosition((vec2f(centerPixel) + 0.5) / size, depth, depthSign);
        let tolerance = ${planeTolerance} + 0.004 * depth;
        var sum = 0.0;
        var weight = 0.0;
        for (var i = -2; i <= 2; i++) {
#ifdef BLUR_H
            let pixel = centerPixel + vec2i(i, 0);
#else
            let pixel = centerPixel + vec2i(0, i);
#endif
            if (any(pixel < vec2i(0)) || any(pixel >= dimensions)) { continue; }
            let tapGeometry = textureLoad(depthSampler, pixel, 0);
            let tapDepth = abs(tapGeometry.a);
            if (tapDepth == 0.0 || dot(tapGeometry.xyz, tapGeometry.xyz) < 0.5) { continue; }
            let tapPosition = portalViewPosition((vec2f(pixel) + 0.5) / size, tapDepth, depthSign);
            if (abs(dot(normal, tapPosition - position)) > tolerance) { continue; }
            let tapWeight = select(2.0, 1.0, abs(i) == 2);
            sum += tapWeight * textureLoad(textureSampler, pixel, 0).r;
            weight += tapWeight;
        }
        result = sum / weight;
    }
#endif
    fragmentOutputs.color = vec4f(result, result, result, 1.0);
    return fragmentOutputs;
}
#endif
#endif
`;
    }
    store.ssao2PixelShader = occlusionShader(halfPrecision);
    includes.pbrBlockPrePass = normalWrites.reduce(function (block, write) {
        return block.replace(write, "\n#if defined(ALPHABLEND) || defined(ALPHATEST)\nfragData[PREPASS_NORMAL_INDEX] = vec4f(vec3f(0.0), finalColor.a);\n#else\n$&\n#endif\n");
    }, prePassBlock);

    globalThis.__nativeArPortalBenchmarkLabel = "gtvbao-cosine-v5";
    // The occluders within the radius count as unlit. Past contact range that
    // shades a wall from the pavement it stands on, so the radius stays short.
    if (globalThis.__nativeArPortalSSAORadius === undefined) {
        globalThis.__nativeArPortalSSAORadius = 0.5;
    }
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        // Later preludes inspect the published pipeline, so reshape it once they have run.
        scene.onBeforeRenderObservable.addOnce(function () { install(scene, status); });
    };
    function install(scene, status) {
        var camera = scene.activeCamera;
        if (camera.mode !== BABYLON.Camera.PERSPECTIVE_CAMERA) {
            throw new Error("Portal AO currently requires a perspective camera.");
        }
        var engine = scene.getEngine();
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        var prepass = pipeline && pipeline._prePassRenderer;
        var gbuffer = pipeline && pipeline._geometryBufferRenderer;
        var ssao = pipeline && pipeline._ssaoPostProcess;
        var blurs = pipeline && [pipeline._blurHPostProcess, pipeline._blurVPostProcess];
        var combinePass = pipeline && pipeline._ssaoCombinePostProcess;
        if ((!prepass && !gbuffer) || !ssao || [ssao, blurs[0], blurs[1], combinePass].some(function (pass) { return pass._textures.length !== 0; })) {
            throw new Error("Portal AO requires an unallocated SSAO2 pipeline with depth and normal textures.");
        }
        if (gbuffer && gbuffer.normalsAreUnsigned) {
            throw new Error("Portal AO requires signed geometry-buffer normals.");
        }
        // Pre-pass layouts can change between cameras. Resolve both attachments
        // from the current layout when the pass is applied, not at installation.
        function bindGeometryInputs(effect, depth) {
            var textures = prepass ? prepass.getRenderTarget().textures : gbuffer.getGBuffer().textures;
            effect.setTexture("normalSampler", textures[prepass ? prepass.getIndex(BABYLON.Constants.PREPASS_NORMAL_TEXTURE_TYPE) :
                gbuffer.getTextureIndex(BABYLON.GeometryBufferRenderer.NORMAL_TEXTURE_TYPE)]);
            if (depth) {
                effect.setTexture("depthSampler", textures[prepass ? prepass.getIndex(BABYLON.Constants.PREPASS_DEPTH_TEXTURE_TYPE) :
                    gbuffer.getTextureIndex(BABYLON.GeometryBufferRenderer.DEPTH_TEXTURE_TYPE)]);
            }
        }
        // A pass owns its input target. The AO pass now receives the AO-resolution
        // geometry, and the filter is defined on the AO texel grid, so the geometry,
        // AO and both filter targets share the AO size.
        var aoScale = blurs[0]._options;
        ssao._options = blurs[1]._options = combinePass._options = aoScale;
        ssao._textureType = BABYLON.Constants.TEXTURETYPE_FLOAT;
        ssao._textureFormat = BABYLON.Constants.TEXTUREFORMAT_RGBA;
        ssao._samplingMode = BABYLON.Constants.TEXTURE_NEAREST_SAMPLINGMODE;
        var normalToView = new Float32Array(9);
        var normalMatrix = new BABYLON.Matrix();
        function bindReconstruction(effect) {
            var projection = scene.activeCamera.getProjectionMatrix();
            effect.setMatrix("projection", projection);
            effect.setFloat("xViewport", 1 / projection.m[0]);
            effect.setFloat("yViewport", 1 / projection.m[5]);
        }
        var geometry = new BABYLON.PostProcess("portalAoGeometry", "portalAoGeometry", {
            uniforms: ["maxZ", "projection", "xViewport", "yViewport", "normalToView"],
            samplers: ["depthSampler", "normalSampler"],
            size: 1,
            samplingMode: BABYLON.Constants.TEXTURE_NEAREST_SAMPLINGMODE,
            engine: engine,
            reusable: false,
            shaderLanguage: BABYLON.ShaderLanguage.WGSL
        });
        geometry.autoClear = false;
        geometry.onApply = function (effect) {
            bindGeometryInputs(effect, true);
            bindReconstruction(effect);
            effect.setFloat("maxZ", pipeline.maxZ);
            if ((prepass || gbuffer).generateNormalsInWorldSpace) {
                scene.activeCamera.getViewMatrix().toNormalMatrix(normalMatrix);
                var m = normalMatrix.m;
                normalToView.set([m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]]);
            } else {
                normalToView.set([1, 0, 0, 0, 1, 0, 0, 0, 1]);
            }
            effect.setMatrix3x3("normalToView", normalToView);
        };
        ssao.onApply = function () {};
        combinePass.onApplyObservable.add(function (effect) {
            bindGeometryInputs(effect, surfaceAwareUpsample);
            if (!surfaceAwareUpsample) return;
            // The published combine effect declares no such sampler, and the engine
            // only binds post-process inputs to declared channels.
            engine._bindTexture(0, ssao.inputTexture.texture, "geometrySampler");
            effect.setFloat("maxZ", pipeline.maxZ);
            bindReconstruction(effect);
        });
        blurs.forEach(function (blur) {
            blur.onApply = function (effect) {
                effect.setTextureFromPostProcess("depthSampler", ssao);
                bindReconstruction(effect);
            };
        });
        // Panes blended without an alpha texture draw color alone. They are drawn
        // back to front without depth writes, so one that overwrote the geometry
        // would leave whatever is drawn after it standing in a pit behind it.
        var cutouts = 0;
        var panes = scene.materials.filter(function (material) {
            var texture = material.albedoTexture || material.diffuseTexture;
            var alphaTextured = !!(texture && texture.hasAlpha);
            if (alphaTextured && (material.needAlphaBlending() || material.needAlphaTesting())) cutouts++;
            return prepass && material.needAlphaBlending() && !alphaTextured && prepass.excludedMaterials.indexOf(material) === -1;
        });
        panes.forEach(function (material) { prepass.excludedMaterials.push(material); });
        // Geometry produces normals/depth for AO, never the scene color used by
        // the combine. The optional non-XR color-reuse path has no color copy.
        var aoIndex = camera._postProcesses.indexOf(ssao);
        if (aoIndex < 0) throw new Error("Portal AO camera has no attached AO pass.");
        camera.attachPostProcess(geometry, aoIndex);
        scene.onDisposeObservable.add(function () { geometry.dispose(camera); });
        status("native-xr-portal:gtvbao=cosine-weighted:slices=" + sampleLayout.slices + ":stepsPerSide=" + sampleLayout.steps +
            ":samples=" + sampleLayout.slices * sampleLayout.steps * (sampleLayout.paired ? 2 : 1) + ":bits=32:precision=" + precision + ":noise=ordered-4x4:filter=box4*box2" +
            ":source=" + (prepass ? "pre-pass" : "geometry-buffer") + ":geometry=ao-resolution-plane-filtered:upsample=" + (surfaceAwareUpsample ? "surface-aware" : "bilinear") +
            ":color-only-panes=" + panes.length + ":cutouts=" + cutouts + ":cutout-visibility=" + cutoutLiteral);
    }
})();
