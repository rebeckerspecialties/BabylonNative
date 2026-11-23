// Validation only: do not load unused rows/columns for affine UV transforms.
(function () {
    var store = BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    var source = store.samplerVertexImplementation;
    var pattern = /\(uniforms\._MATRIXNAME_Matrix\s*\*\s*vec4f\((uvUpdated|uv2Updated|vertexInputs\.uv[3-6]),\s*1\.0,\s*0\.0\)\)\.xy/g;
    if (typeof source !== "string" || (source.match(pattern) || []).length !== 6) {
        throw new Error("Affine UV validation no longer matches all six coordinate paths.");
    }
    store.samplerVertexImplementation = source.replace(pattern, function (_, uv) {
        return "(mat3x2f(uniforms._MATRIXNAME_Matrix[0].xy, uniforms._MATRIXNAME_Matrix[1].xy, uniforms._MATRIXNAME_Matrix[2].xy) * vec3f(" + uv + ", 1.0))";
    });
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-affine-uv";
})();
