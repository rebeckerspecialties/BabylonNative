// Opt-in A/B/A comparison using Babylon's public material API. No mesh freezing.
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var materials = scene.materials.filter(function (material) {
            return material && !material.isFrozen && typeof material.freeze === "function" && typeof material.unfreeze === "function";
        });
        var start = performance.now();
        var previousPhase = -1;
        var frozen = false;
        function restore() {
            if (frozen) {
                materials.forEach(function (material) { material.unfreeze(); });
                frozen = false;
            }
        }
        var observer = scene.onBeforeRenderObservable.add(function () {
            var phase = Math.min(2, Math.floor((performance.now() - start) / 30000));
            if (phase === previousPhase) {
                return;
            }
            previousPhase = phase;
            if (phase === 1) {
                materials.forEach(function (material) { material.freeze(); });
                frozen = true;
            } else {
                restore();
            }
            reportStatus("native-xr-portal:material-comparison:phase=" + phase + ":frozen=" + frozen + ":materials=" + materials.length);
        });
        scene.onDisposeObservable.add(function () {
            scene.onBeforeRenderObservable.remove(observer);
            restore();
        });
    };
})();
