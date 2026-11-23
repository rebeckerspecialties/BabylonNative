// Optional prelude. These are CPU-side wall times and overlap; they are not GPU timings.
globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
    if (typeof BABYLON.SceneInstrumentation !== "function") {
        reportStatus("native-xr-portal:cpu-phases=unavailable");
        return;
    }

    var instrumentation = new BABYLON.SceneInstrumentation(scene);
    var phases = [
        ["frame", "Frame", "frame"],
        ["activeMeshes", "ActiveMeshesEvaluation", "activeMeshesEvaluation"],
        ["renderTargets", "RenderTargetsRender", "renderTargetsRender"],
        ["draw", "Render", "render"],
        ["particles", "ParticlesRender", "particlesRender"],
        ["animations", "Animations", "animations"]
    ];
    var totals = phases.map(function () { return 0; });
    for (var i = 0; i < phases.length; i++) {
        instrumentation["capture" + phases[i][1] + "Time"] = true;
    }

    var frames = 0;
    var lastReport = performance.now();
    var observer = scene.onAfterRenderObservable.add(function () {
        frames++;
        for (var i = 0; i < phases.length; i++) {
            totals[i] += instrumentation[phases[i][2] + "TimeCounter"].current;
        }
        var now = performance.now();
        if (now - lastReport < 2000) {
            return;
        }
        var message = "native-xr-portal:cpu-phases:frames=" + frames;
        for (var i = 0; i < phases.length; i++) {
            message += ":" + phases[i][0] + "Ms=" + (totals[i] / frames).toFixed(3);
            totals[i] = 0;
        }
        reportStatus(message);
        frames = 0;
        lastReport = now;
    });
    scene.onDisposeObservable.add(function () {
        scene.onAfterRenderObservable.remove(observer);
        instrumentation.dispose();
    });
};
