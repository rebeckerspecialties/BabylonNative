// Validation only: half arithmetic for well-scaled AO angle normalization.
// Near-zero and large vectors keep the full-precision path.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var source = store.ssao2PixelShader;
    var pattern = /projected\.x \* inverseSqrt\(max\(dot\(projected, projected\), 1e-12\)\)/;
    if (typeof source !== "string" || source.includes("portalHalfProjectedCosine") || (source.match(new RegExp(pattern.source, "g")) || []).length !== 1) {
        throw new Error("Half AO normalization requires the unchanged GT-VBAO slice cosine.");
    }
    source = `fn portalHalfProjectedCosine(projected: vec2f) -> f32 {
    let scale = max(abs(projected.x), abs(projected.y));
    // At least one squared component is normal in f16; neither the sum nor
    // its reciprocal square root can overflow, including flush-to-zero GPUs.
    if (scale >= 0.0625 && scale <= 128.0) {
        let h = vec2h(projected);
        return f32(h.x * inverseSqrt(dot(h, h)));
    }
    return projected.x * inverseSqrt(max(dot(projected, projected), 1e-12));
}
` + source.replace(pattern, "portalHalfProjectedCosine(projected)");
    var configure = globalThis.__nativePlaygroundConfigureEngine;
    globalThis.__nativePlaygroundConfigureEngine = function (engine) {
        if (configure) configure(engine);
        if (!engine.enabledExtensions.includes("shader-f16")) throw new Error("Half AO normalization requires shader-f16 on the device.");
        var processor = engine._getShaderProcessor(BABYLON.ShaderLanguage.WGSL);
        if (!processor || typeof processor.finalizeShaders !== "function") throw new Error("Half AO normalization requires the WGSL finalizer.");
        var finalize = processor.finalizeShaders;
        processor.finalizeShaders = function (vertex, fragment) {
            var result = finalize.call(this, vertex, fragment);
            if (result.fragmentCode.includes("portalHalfProjectedCosine") && !result.fragmentCode.startsWith("enable f16;")) {
                result.fragmentCode = "enable f16;\n" + result.fragmentCode;
            }
            return result;
        };
    };
    store.ssao2PixelShader = source;
    globalThis.__nativePlaygroundRequiredFeatures = Array.from(new Set((globalThis.__nativePlaygroundRequiredFeatures || []).concat(["shader-f16"])));
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-ao-half-angle";
})();
