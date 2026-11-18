// Validation only: frozen materials on the dynamic render-bundle path.
// Babylon.js re-derives the defines and rewrites the uniform block of every
// material for every draw. Hill Valley's materials do not change after they
// load, so freezing them removes that work from each frame. A frozen material
// ignores later changes to lights, clip planes and its own properties.
// Load in place of the render-bundle comparison prelude, before the demo.
globalThis.__nativeArPortalUseWebGPUFastPath = true;
(function () {
    // The AO preludes reshape the pipeline during the first frames, which changes
    // material defines. Freeze only once the scene has been ready this long.
    var settledFrames = 60;
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var engine = scene.getEngine();
        if (!engine.isWebGPU || engine.compatibilityMode !== false || engine.snapshotRendering) {
            throw new Error("Frozen materials are measured on dynamic WebGPU rendering without snapshots.");
        }
        var ready = 0;
        var observer = scene.onAfterRenderObservable.add(function () {
            ready = scene.isReady() ? ready + 1 : 0;
            if (ready < settledFrames) return;
            scene.onAfterRenderObservable.remove(observer);
            scene.materials.forEach(function (material) { material.freeze(); });
            status("native-xr-portal:materials-frozen=" + scene.materials.length);
        });
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-frozen-materials";
})();
