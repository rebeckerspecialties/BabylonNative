// Validation only: store scalar AO without duplicating its red value in RGB.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var source = store.ssaoCombinePixelShader;
    var pattern = /fragmentOutputs\.color\s*=\s*sceneColor\s*\*\s*ssaoColor;/;
    if (typeof source !== "string" || (source.match(new RegExp(pattern.source, "g")) || []).length !== 1) {
        throw new Error("R8 AO validation requires the unchanged SSAO combine contract.");
    }
    store.ssaoCombinePixelShader = source.replace(pattern,
        "fragmentOutputs.color = vec4f(sceneColor.rgb * ssaoColor.r, sceneColor.a);");
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        // A post-process owns its input target. These three targets receive AO,
        // horizontal blur, and vertical blur, respectively. The scene color
        // input of _ssaoPostProcess must remain RGBA.
        var targets = pipeline && [pipeline._blurHPostProcess, pipeline._blurVPostProcess, pipeline._ssaoCombinePostProcess];
        if (!targets || targets.some(function (target) {
            return !target || target._textureFormat !== BABYLON.Constants.TEXTUREFORMAT_RGBA ||
                target._textureType !== BABYLON.Constants.TEXTURETYPE_UNSIGNED_BYTE || target._textures.length !== 0;
        })) {
            throw new Error("R8 AO validation must run before allocation of all three unsigned-byte AO targets.");
        }
        targets.forEach(function (target) { target._textureFormat = BABYLON.Constants.TEXTUREFORMAT_RED; });
        status("native-xr-portal:ao-storage=r8:precision-and-sampling=unchanged");
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-r8-ao";
})();
