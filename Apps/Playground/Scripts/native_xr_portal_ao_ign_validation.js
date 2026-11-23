// Validation only: static, f32 interleaved gradient noise. No temporal history.
// Rejected portal control: visible shadow stripes/crawling with the slope blur.
// Retained for reproduction, not enabled by the demo or the accepted workload.
// Jimenez 2014; also used in Unity's ShaderLibrary/Random.hlsl.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var source = store.ssao2PixelShader;
    var needle = "let noise = vec2f(portalRandom(noiseSeed), portalRandom(noiseSeed ^ 0x9e3779b9u));";
    var entry = "@fragment\nfn main(input: FragmentInputs)";
    if (typeof source !== "string" || source.split(needle).length !== 2 || source.split("#else\n#ifdef BLUR")[0].split(entry).length !== 2) {
        throw new Error("Static IGN validation requires the unchanged GT-VBAO noise contract.");
    }
    source = source.replace(entry, `fn portalIgn(pixel: vec2f) -> f32 {
    return fract(52.9829189 * fract(dot(pixel, vec2f(0.06711056, 0.00583715))));
}

` + entry).replace(needle,
        "let noise = vec2f(portalIgn(vec2f(noisePixel)), portalIgn(vec2f(noisePixel.yx) + vec2f(19.0, 47.0)));");
    store.ssao2PixelShader = source;
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-static-ign";
})();
