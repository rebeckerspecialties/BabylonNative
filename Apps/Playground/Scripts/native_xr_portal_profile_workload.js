// Fixed AO workload accepted for profiling; not a visual-parity claim.
globalThis.__nativeArPortalLogMeshDiagnostics = false;
globalThis.__nativeArPortalLogAoCoordinates = false;
globalThis.__nativeArPortalSSAOStrength = 1.2;
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        if (pipeline) {
            pipeline.totalStrength = 1.2;
            pipeline.epsilon = 0.01;
        }
        var engine = scene.getEngine();
        var canvas = engine.getRenderingCanvas();
        reportStatus("native-xr-portal:profile-workload=" + JSON.stringify({
            strength: pipeline && pipeline.totalStrength,
            epsilon: pipeline && pipeline.epsilon,
            samples: pipeline && pipeline.samples,
            canvas: canvas && { width: canvas.width, height: canvas.height, clientWidth: canvas.clientWidth, clientHeight: canvas.clientHeight },
            devicePixelRatio: globalThis.devicePixelRatio,
            hardwareScaling: engine.getHardwareScalingLevel()
        }));
    };
})();
