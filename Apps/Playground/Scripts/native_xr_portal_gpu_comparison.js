// Validation only: keep SSAO's random kernel and noise identical between comparisons.
// This wraps synchronous setup, never a render-loop or per-draw call.
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        var ssao = pipeline && pipeline._thinSSAORenderingPipeline && pipeline._thinSSAORenderingPipeline._ssaoPostProcess;
        var generate = ssao && ssao._generateHemisphere;
        if (typeof generate !== "function" || !generate.toString().includes("Math.random") || typeof ssao._createRandomTexture !== "function") {
            throw new Error("GPU comparison requires the published randomized SSAO hemisphere generator.");
        }
        var random = Math.random;
        var seed = 0x5eedc0de;
        Math.random = function () {
            seed ^= seed << 13;
            seed ^= seed >>> 17;
            seed ^= seed << 5;
            return (seed >>> 0) / 4294967296;
        };
        try {
            ssao._sampleSphere = generate.call(ssao);
            var oldTexture = ssao._randomTexture;
            ssao._createRandomTexture();
            oldTexture.dispose();
        } finally {
            Math.random = random;
        }
        status("native-xr-portal:comparison-ao-kernel-noise-seed=0x5eedc0de");
    };
    globalThis.__nativeArPortalBenchmarkSuffix = "-seeded-v2";
})();
