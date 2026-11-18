// Validation only: reuse retained pre-pass color instead of copying it for AO.
// The first fixture is single-camera, full viewport, single-sample, non-AR.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var source = store.ssaoCombinePixelShader;
    var colorRead = /var sceneColor:\s*vec4f\s*=\s*textureSample\(originalColor,originalColorSampler,uv\);/;
    if (typeof source !== "string" || (source.match(new RegExp(colorRead.source, "g")) || []).length !== 1) {
        throw new Error("Color reuse requires the unchanged SSAO original-color read.");
    }
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var manager = scene.postProcessRenderPipelineManager;
        var pipeline = manager.supportedPipelines.find(function (candidate) { return candidate.name === "native-xr-portal-ssao2"; });
        var camera = scene.activeCamera;
        var prepass = pipeline && pipeline._prePassRenderer;
        var original = pipeline && pipeline._originalColorPostProcess;
        var ao = pipeline && pipeline._ssaoPostProcess;
        var combine = pipeline && pipeline._ssaoCombinePostProcess;
        if (globalThis.__nativeArPortalForceNonXrStress !== true || !camera || (scene.activeCameras && scene.activeCameras.length > 1) ||
            camera.viewport.x !== 0 || camera.viewport.y !== 0 || camera.viewport.width !== 1 || camera.viewport.height !== 1 ||
            !prepass || !original || !ao || !combine || pipeline.textureSamples !== 1 ||
            ao._textureType !== BABYLON.Constants.TEXTURETYPE_UNSIGNED_BYTE ||
            camera._getFirstPostProcess() !== original) {
            throw new Error("Color reuse requires single-camera, full-viewport, single-sample pre-pass AO with an eight-bit original-color copy.");
        }
        // The removed pass quantizes scene color to eight bits. Preserve that
        // conversion at the final read, including clamping HDR values and alpha.
        store.portalSsaoColorReusePixelShader = source.replace(colorRead,
            "$&\nsceneColor = floor(clamp(sceneColor, vec4f(0.0), vec4f(1.0)) * 255.0 + 0.5) / 255.0;");
        // A distinct key prevents reusing the already compiled original effect.
        combine.updateEffect(null, null, null, undefined, undefined, undefined, undefined, "portalSsaoColorReuse");
        manager.disableEffectInPipeline(pipeline.name, pipeline.SSAOOriginalSceneColorEffect, [camera]);
        ao.onApplyObservable.add(function () {
            if (scene.activeCamera !== camera) throw new Error("Color-reuse fixture changed camera.");
            var target = prepass.getRenderTarget();
            var color = target.textures[prepass.getIndex(BABYLON.Constants.PREPASS_COLOR_TEXTURE_TYPE)];
            var size = color.getSize();
            original.width = size.width;
            original.height = size.height;
            original.onBeforeRenderObservable.notifyObservers(original.effect);
        }, -1, true);
        combine.onApply = function (effect) {
            pipeline._thinSSAORenderingPipeline._ssaoCombinePostProcess.camera = scene.activeCamera;
            var target = prepass.getRenderTarget();
            effect.setTexture("originalColor", target.textures[prepass.getIndex(BABYLON.Constants.PREPASS_COLOR_TEXTURE_TYPE)]);
        };
        status("native-xr-portal:ao-original-color=prepass-reuse:quantization=8-bit");
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-color-reuse";
})();
