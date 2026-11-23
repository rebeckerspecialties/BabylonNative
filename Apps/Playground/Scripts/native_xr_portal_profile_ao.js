// Diagnostic only: compare the complete AO chain, bypass it, then restore it.
// Load after native_xr_portal_profile.js. This never changes the demo defaults.
(function () {
    var startProfile = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (typeof startProfile === "function") {
            startProfile(scene, reportStatus);
        }
        var manager = scene.postProcessRenderPipelineManager;
        var pipeline = manager && manager.supportedPipelines.filter(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        })[0];
        var renderer = scene.geometryBufferRenderer;
        var camera = scene.activeCamera;
        if (!pipeline || !renderer || !camera) {
            reportStatus("native-xr-portal:ao-comparison=unavailable");
            return;
        }

        var buffer = renderer.getGBuffer();
        var refreshRate = buffer.refreshRate;
        var phase = 0;
        var lastTransition = performance.now();
        var minimumFactor = Math.max(0, Math.min(1, 1 - pipeline.totalStrength + pipeline.base));
        reportStatus("native-xr-portal:ao-comparison:phase=baseline:strength=" + pipeline.totalStrength +
            ":base=" + pipeline.base + ":minimumFactor=" + minimumFactor.toFixed(3));
        var observer = scene.onBeforeRenderObservable.add(function () {
            var now = performance.now();
            if (now - lastTransition < 30000) {
                return;
            }
            lastTransition = now;
            phase++;
            if (phase === 1) {
                manager.detachCamerasFromRenderPipeline(pipeline.name, [camera]);
                // Refresh-once avoids recurring geometry work without destroying the
                // renderer retained by the pipeline. Exclude the transition frame.
                buffer.refreshRate = 0;
                reportStatus("native-xr-portal:ao-comparison:phase=bypass");
            } else if (phase === 2) {
                buffer.refreshRate = refreshRate;
                manager.attachCamerasToRenderPipeline(pipeline.name, [camera]);
                reportStatus("native-xr-portal:ao-comparison:phase=restored");
            } else {
                scene.onBeforeRenderObservable.remove(observer);
                reportStatus("native-xr-portal:ao-comparison:phase=complete");
            }
        });
        scene.onDisposeObservable.add(function () {
            scene.onBeforeRenderObservable.remove(observer);
        });
    };
})();
