// Validation only. Keep the scene moving while disabling only AO's contribution.
(function () {
    var previous = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previous) previous(scene, status);
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (value) {
            return value.name === "native-xr-portal-ssao2";
        });
        if (!pipeline) throw new Error("Shimmer diagnostic requires the AO comparison pipeline.");
        var strength = pipeline.totalStrength;
        var elapsed = 0;
        var phase = -1;
        scene.onBeforeRenderObservable.add(function () {
            if (globalThis.__nativeArPortalBenchmarkCaptureOnly) return;
            elapsed += scene.getEngine().getDeltaTime();
            // Allow initial shader compilation and the fixed-pose captures to finish.
            var next = elapsed < 60000 ? 0 : elapsed < 120000 ? 1 : 2;
            if (next !== phase) {
                phase = next;
                pipeline.totalStrength = phase === 1 ? 0 : strength;
                status("shimmer-diagnostic:ao=" + (phase === 1 ? "off" : "on") + ":elapsedMs=" + elapsed);
            }
        });
        scene.textures.forEach(function (texture) {
            var internal = texture.getInternalTexture && texture.getInternalTexture();
            var resource = internal && internal._hardwareTexture && internal._hardwareTexture.underlyingResource;
            if (!internal || !resource || texture.isRenderTarget) return;
            status("shimmer-diagnostic:texture=" + JSON.stringify({ name: texture.name, width: internal.width, height: internal.height,
                mipLevels: resource.mipLevelCount, generateMipMaps: internal.generateMipMaps, samplingMode: internal.samplingMode,
                anisotropy: texture.anisotropicFilteringLevel, gamma: texture.gammaSpace, srgbBuffer: internal._useSRGBBuffer }));
        });
    };
})();
