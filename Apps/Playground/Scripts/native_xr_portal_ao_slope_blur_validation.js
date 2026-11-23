// Validation only. Replaces the two existing blur passes, retaining scalar R8.
// V4 fixes mixed-resolution sampling, but has not passed moving-scene visual acceptance.
// Textures/UVs must be local to one view; packed stereo needs explicit eye bounds.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var source = store.ssao2PixelShader;
    var pattern = /fn blur13Bilateral\(image:\s*texture_2d<f32>,\s*imageSampler:\s*sampler,\s*uv:\s*vec2f,\s*step:\s*vec2f\)\s*->\s*f32\s*\{[\s\S]*?return result\s*\/\s*weightSum;\s*\}/g;
    if (typeof source !== "string" || (source.match(pattern) || []).length !== 1 || !source.includes("var off3:")) {
        throw new Error("Slope blur validation requires the unchanged legacy bilateral blur contract.");
    }
    // Generate constant taps once at shader setup, not nested per-fragment loops.
    var taps = [-1, -2, -3, 1, 2, 3].map(function (offset) {
        var reciprocal = offset === -1 ? "left" : offset === 1 ? "right" : "portalBlurReciprocalDepth(sampleUv, step)";
        var spatial = [0, 0.8, 0.4, 0.1][Math.abs(offset)];
        return `    {
        let sampleUv = uv + step * ${offset.toFixed(1)};
        let reciprocal = ${reciprocal};
        let residual = abs(reciprocal.x - (center.x + slope * (reciprocal.y - center.y)));
        let edge = clamp(1.0 - residual * edgeScale, 0.0, 1.0);
        let valid = reciprocal.x > 0.0 && all(sampleUv >= vec2f(0.0)) && all(sampleUv < vec2f(1.0));
        let weight = select(0.0, ${spatial} * edge * edge, valid);
        result += textureSampleLevel(image, imageSampler, clamp(sampleUv, halfTexel, 1.0 - halfTexel), 0.0).r * weight;
        weightSum += weight;
    }`;
    }).join("\n");
    store.ssao2PixelShader = source.replace(pattern, `fn portalBlurReciprocalDepth(uv: vec2f, step: vec2f) -> vec2f {
    let size = textureDimensions(depthSampler);
    let pixel = clamp(vec2i(uv * vec2f(size)), vec2i(0), vec2i(size) - 1);
    let depth = abs(textureLoad(depthSampler, pixel, 0).r);
    let coordinate = select(f32(pixel.y), f32(pixel.x), step.x != 0.0);
    return vec2f(select(0.0, 1.0 / max(depth, 1e-6), depth > 0.0), coordinate);
}

fn blur13Bilateral(image: texture_2d<f32>, imageSampler: sampler, uv: vec2f, step: vec2f) -> f32 {
    let center = portalBlurReciprocalDepth(uv, step);
    if (center.x == 0.0) { return 1.0; }
    let left = portalBlurReciprocalDepth(uv - step, step);
    let right = portalBlurReciprocalDepth(uv + step, step);
    let leftSlope = (center.x - left.x) / max(center.y - left.y, 1.0);
    let rightSlope = (right.x - center.x) / max(right.y - center.y, 1.0);
    // Inverse view depth is affine across a perspective-projected plane.
    // Fit at the actual depth texels, whose spacing differs from AO texels.
    // Use the smaller same-sign derivative to avoid fitting a silhouette jump.
    var slope = 0.0;
    if (left.x > 0.0 && right.x > 0.0 && leftSlope * rightSlope > 0.0) {
        slope = select(rightSlope, leftSlope, abs(leftSlope) < abs(rightSlope));
    }
    let halfTexel = 0.5 / vec2f(textureDimensions(image));
    let edgeScale = 1.0 / (center.x * 0.005);
    var result = textureSampleLevel(image, imageSampler, clamp(uv, halfTexel, 1.0 - halfTexel), 0.0).r;
    var weightSum = 1.0;
${taps}
    return result / weightSum;
}`);
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (item) {
            return item.name === "native-xr-portal-ssao2";
        });
        if (!pipeline || pipeline.expensiveBlur || pipeline._blurHPostProcess._textureFormat !== BABYLON.Constants.TEXTUREFORMAT_RED ||
            pipeline._blurVPostProcess._textureFormat !== BABYLON.Constants.TEXTUREFORMAT_RED) {
            throw new Error("Slope blur validation requires legacy blur selection and R8 AO targets.");
        }
        status("native-xr-portal:ao-blur=slope-reciprocal-v4:r8:passes=2:aoTaps=7:depthTaps=7:viewLocal=true:depthTexelSpacing=true");
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-slope-blur-v4";
})();
