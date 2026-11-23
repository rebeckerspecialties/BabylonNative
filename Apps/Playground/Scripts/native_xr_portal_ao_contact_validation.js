// Validation only: compare contact shading at unchanged resolution and sample count.
// Load after the projection validation prelude, before the demo.
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        if (!pipeline) {
            reportStatus("native-xr-portal:ao-contact=unavailable");
            return;
        }
        var originalStrength = pipeline.totalStrength;
        var originalEpsilon = pipeline.epsilon;
        var start = performance.now();
        var lastPhase = -1;
        var phases = ["current", "off", "contact", "off", "contact"];
        var observer = scene.onBeforeRenderObservable.add(function () {
            var phase = Math.min(phases.length - 1, Math.floor((performance.now() - start) / 15000));
            if (phase === lastPhase) {
                return;
            }
            lastPhase = phase;
            var mode = phases[phase];
            pipeline.totalStrength = mode === "current" ? originalStrength : mode === "off" ? 0 : 1.2;
            pipeline.epsilon = mode === "current" ? originalEpsilon : 0.01;
            reportStatus("native-xr-portal:ao-contact=" + mode + ":strength=" + pipeline.totalStrength + ":epsilon=" + pipeline.epsilon);
            if (phase === phases.length - 1) {
                scene.onBeforeRenderObservable.remove(observer);
            }
        });
        scene.onDisposeObservable.add(function () {
            scene.onBeforeRenderObservable.remove(observer);
        });
    };
})();
