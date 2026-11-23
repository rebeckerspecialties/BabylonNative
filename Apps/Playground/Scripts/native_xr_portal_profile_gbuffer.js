// Opt-in A/B/A/B comparison. Keep shading and AO output resolution unchanged.
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var geometry = scene.geometryBufferRenderer;
        var gbuffer = geometry && geometry.getGBuffer();
        if (!gbuffer) {
            reportStatus("native-xr-portal:gbuffer-comparison=unavailable");
            return;
        }
        var original = Object.assign({}, gbuffer.getSize());
        var start = performance.now();
        var previousPhase = -1;
        var observer = scene.onBeforeCameraRenderObservable.add(function (camera) {
            var target = camera.outputRenderTarget;
            if (!target) {
                return;
            }
            var phase = Math.min(3, Math.floor((performance.now() - start) / 30000));
            var match = phase % 2 === 1;
            var size = match ? target.getSize() : original;
            if (!Number.isInteger(size.width) || !Number.isInteger(size.height) || size.width <= 0 || size.height <= 0) {
                throw new Error("Geometry-buffer comparison requires a positive integer XR render-target size.");
            }
            var current = gbuffer.getSize();
            if (current.width !== size.width || current.height !== size.height) {
                gbuffer.resize({ width: size.width, height: size.height });
            }
            if (phase !== previousPhase) {
                previousPhase = phase;
                reportStatus("native-xr-portal:gbuffer-comparison=" + (match ? "xr-size" : "original") +
                    ":phase=" + phase + ":size=" + size.width + "x" + size.height);
            }
        });
        scene.onDisposeObservable.add(function () {
            scene.onBeforeCameraRenderObservable.remove(observer);
        });
    };
})();
