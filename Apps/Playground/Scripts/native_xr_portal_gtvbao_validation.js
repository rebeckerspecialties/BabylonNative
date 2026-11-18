// Validation only: uniform-hemisphere GT-VBAO, not cosine-weighted GTAO.
// References and derivation: Documentation/NativeXrPortalProfiling.md.
// Load after the projection validation prelude, before creating the scene.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var original = store.ssao2PixelShader;
    var boundary = "#else\n#ifdef BLUR\nuniform outSize";
    if (original.split(boundary).length !== 2 || !original.includes("% SAMPLES]")) {
        throw new Error("GT-VBAO validation requires the corrected SSAO2 shader and an unchanged blur boundary.");
    }
    store.ssao2PixelShader = `
varying vUV: vec2f;
var textureSamplerSampler: sampler;
var textureSampler: texture_2d<f32>;
#ifdef SSAO
var depthSamplerSampler: sampler;
var depthSampler: texture_2d<f32>;
var normalSamplerSampler: sampler;
var normalSampler: texture_2d<f32>;
var randomSamplerSampler: sampler;
var randomSampler: texture_2d<f32>;
uniform randTextureTiles: f32;
uniform radius: f32;
uniform totalStrength: f32;
uniform base: f32;
uniform maxZ: f32;
uniform projection: mat4x4f;
uniform xViewport: f32;
uniform yViewport: f32;
#ifdef NORMAL_WORLDSPACE
uniform normalWorldToView: mat3x3f;
#endif
const PORTAL_SLICE_COUNT: u32 = 2u;
const PORTAL_STEPS_PER_SIDE: u32 = 2u;

fn portalViewPosition(uv: vec2f, depth: f32, depthSign: f32) -> vec3f {
    let xy = (uv * 2.0 - 1.0 - uniforms.projection[2].xy * depthSign) *
        vec2f(uniforms.xViewport, uniforms.yViewport) * depth;
    return vec3f(xy, depth * depthSign);
}

// Integral of |sin(theta)| over a hemisphere slice. No inverse trig is needed.
fn portalSliceCdf(delta: vec3f, view: vec3f, tangent: vec3f, normalTangent: f32) -> f32 {
    let projected = vec2f(dot(delta, view), dot(delta, tangent));
    let cosine = clamp(projected.x * inverseSqrt(max(dot(projected, projected), 1e-12)), -1.0, 1.0);
    return clamp(select((cosine - normalTangent) * 0.5,
        1.0 - (cosine + normalTangent) * 0.5, projected.y >= 0.0), 0.0, 1.0);
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

fn portalClipSlab(delta: vec3f, ray: vec3f, thickness: f32, radius: f32) -> vec2f {
    let b = dot(delta, ray);
    let discriminant = b * b - dot(delta, delta) + radius * radius;
    if (discriminant <= 0.0) { return vec2f(0.0); }
    let extent = sqrt(discriminant);
    return vec2f(max(0.0, -b - extent), min(thickness, -b + extent));
}

// SSAO's normalized RG vectors are not uniform random scalars. Use a stable
// pixel hash for slice rotation and point jitter instead of treating them as such.
fn portalRandom(seed: u32) -> f32 {
    let state = seed * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return f32(((word >> 22u) ^ word) >> 8u) * (1.0 / 16777216.0);
}

@fragment
fn main(input: FragmentInputs) -> FragmentOutputs {
    let depthDimensions = textureDimensions(depthSampler);
    let depthSize = vec2f(depthDimensions);
    let centerPixel = clamp(vec2i(input.vUV * depthSize), vec2i(0), vec2i(depthDimensions) - 1);
    let centerUv = (vec2f(centerPixel) + 0.5) / depthSize;
    // Depth, normal, and ray must describe the same pre-pass texel. Mixing a
    // filtered depth or an unsnapped ray with texel-centered neighbors makes
    // sloped planes occlude themselves when AO and pre-pass sizes differ.
    let signedDepth = textureLoad(depthSampler, centerPixel, 0).r;
    let depth = abs(signedDepth);
    if (depth == 0.0 || depth >= uniforms.maxZ) {
        fragmentOutputs.color = vec4f(1.0);
        return fragmentOutputs;
    }
    let depthSign = sign(signedDepth);
    let position = portalViewPosition(centerUv, depth, depthSign);
    let view = normalize(-position);
    var normal = textureLoad(normalSampler, centerPixel, 0).xyz;
#ifdef NORMAL_WORLDSPACE
    normal = uniforms.normalWorldToView * normal;
#endif
    let normalLength = length(normal);
    if (normalLength < 1e-6) {
        fragmentOutputs.color = vec4f(1.0);
        return fragmentOutputs;
    }
    normal /= normalLength;
    let normalView = dot(normal, view);
    if (normalView <= 1e-5) {
        fragmentOutputs.color = vec4f(1.0);
        return fragmentOutputs;
    }
    let noisePixel = vec2u(input.position.xy);
    let noiseSeed = noisePixel.x + noisePixel.y * 65536u;
    let noise = vec2f(portalRandom(noiseSeed), portalRandom(noiseSeed ^ 0x9e3779b9u));
    let helper = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(view.y) > 0.9);
    let basisX = normalize(cross(helper, view));
    let basisY = cross(view, basisX);
    let centerClip = uniforms.projection * vec4f(position, 1.0);
    let safeRadius = max(uniforms.radius, 1e-4);
    var occluded = 0u;
    for (var slice = 0u; slice < PORTAL_SLICE_COUNT; slice++) {
        let phi = (f32(slice) + noise.x) * (3.14159265359 / f32(PORTAL_SLICE_COUNT));
        let tangent = basisX * cos(phi) + basisY * sin(phi);
        let nt = dot(normal, tangent);
        let normalTangent = nt * inverseSqrt(nt * nt + normalView * normalView);
        let clipDirection = uniforms.projection * vec4f(tangent * safeRadius, 0.0);
        let jitter = fract(noise.y + f32(slice) * 0.61803398875);
        var mask = 0u;
        for (var stepIndex = 0u; stepIndex < PORTAL_STEPS_PER_SIDE; stepIndex++) {
            let sampleNoise = fract(noise.y + f32(slice + stepIndex * PORTAL_STEPS_PER_SIDE) * 0.61803398875);
            let sampleDistance = (f32(stepIndex) + 0.5 + sampleNoise * 0.5) / f32(PORTAL_STEPS_PER_SIDE);
            for (var side = -1.0; side <= 1.0; side += 2.0) {
                let clip = centerClip + clipDirection * (side * sampleDistance * sampleDistance);
                if (clip.w <= 1e-6) { continue; }
                let uv = clip.xy / clip.w * 0.5 + 0.5;
                if (any(uv < vec2f(0.0)) || any(uv >= vec2f(1.0))) { continue; }
                // Reconstruct at the actual nearest texel center, not the unsnapped ray.
                let pixel = vec2i(uv * depthSize);
                let sampleDepth = abs(textureLoad(depthSampler, pixel, 0).r);
                if (sampleDepth == 0.0 || sampleDepth >= uniforms.maxZ) { continue; }
                let sampleUv = (vec2f(pixel) + 0.5) / depthSize;
                let samplePosition = portalViewPosition(sampleUv, sampleDepth, depthSign);
                let delta = samplePosition - position;
                let footprint = sampleDepth * 2.0 * max(abs(uniforms.xViewport) / depthSize.x, abs(uniforms.yViewport) / depthSize.y);
                let height = dot(delta, normal);
                if (height <= max(footprint * 0.25, 1e-4)) { continue; }
                // Thickness follows this sample's camera ray, not the center pixel's ray.
                let ray = normalize(samplePosition);
                let thickness = portalThickness(height, dot(ray, normal), footprint, safeRadius);
                let interval = portalClipSlab(delta, ray, thickness, safeRadius);
                if (interval.y <= interval.x) { continue; }
                let front = delta + ray * interval.x;
                let back = delta + ray * interval.y;
                mask |= portalIntervalMask(portalSliceCdf(front, view, tangent, normalTangent),
                    portalSliceCdf(back, view, tangent, normalTangent), jitter);
            }
        }
        occluded += countOneBits(mask);
    }
    let fade = 1.0 - smoothstep(uniforms.maxZ * 0.75, uniforms.maxZ, depth);
    let ao = clamp(1.0 - uniforms.totalStrength * fade * f32(occluded) / (32.0 * f32(PORTAL_SLICE_COUNT)) + uniforms.base, 0.0, 1.0);
    fragmentOutputs.color = vec4f(vec3f(ao), 1.0);
    return fragmentOutputs;
}
` + original.slice(original.indexOf(boundary));
    globalThis.__nativeArPortalBenchmarkLabel = "gtvbao-slab-v4";
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (scene.activeCamera.mode !== BABYLON.Camera.PERSPECTIVE_CAMERA) {
            throw new Error("GT-VBAO validation currently requires a perspective camera.");
        }
        if (previousStart) previousStart(scene, status);
        status("native-xr-portal:gtvbao=uniform-hemisphere:slices=2:stepsPerSide=2:bits=32:thickness=receiver-plane-clipped-slab:center=texel-aligned");
    };
})();
