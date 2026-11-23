// Validation only: retain linear depth without interpolating unused view X/Y.
(function () {
    var includes = BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    var shaders = BABYLON.ShaderStore.ShadersStoreWGSL;
    var updates = [];
    function replace(store, key, pattern, replacement) {
        var source = store[key];
        if (typeof source !== "string" || (source.match(new RegExp(pattern.source, "g")) || []).length !== 1) {
            throw new Error("Scalar pre-pass depth no longer matches " + key);
        }
        updates.push([store, key, source.replace(pattern, replacement)]);
    }
    replace(includes, "prePassVertexDeclaration", /varying vViewPos\s*:\s*vec3f;/, "varying vViewDepth: f32;");
    replace(includes, "prePassDeclaration", /varying vViewPos\s*:\s*vec3f;/, "varying vViewDepth: f32;");
    replace(includes, "prePassVertex", /vertexOutputs.vViewPos\s*=\s*\(scene.view\s*\*\s*worldPos\).rgb;/,
        "vertexOutputs.vViewDepth = (scene.view * worldPos).z;");
    replace(includes, "pbrBlockPrePass", /fragmentInputs\.vViewPos\.z/, "fragmentInputs.vViewDepth");
    replace(includes, "openpbrBlockPrePass", /fragmentInputs\.vViewPos\.z/, "fragmentInputs.vViewDepth");
    replace(shaders, "defaultPixelShader", /fragmentInputs\.vViewPos\.z/, "fragmentInputs.vViewDepth");
    updates.forEach(function (update) { update[0][update[1]] = update[2]; });
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-scalar-depth";
})();
