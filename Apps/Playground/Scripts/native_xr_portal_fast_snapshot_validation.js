// Validation only: fast WebGPU snapshot rendering for the static portal workload.
// The engine records the draws of one frame and replays them, so the scene is
// no longer traversed: no active-mesh evaluation, no material binding and no
// draw encoding per frame. The camera and the scene uniforms still update.
// What the recording fixes until engine.snapshotRenderingReset() is called:
// the list of meshes, their materials and transforms, and the back-to-front
// order of blended meshes as seen from the recording frame.
// Load in place of the render-bundle and frozen-materials preludes, before the demo.
globalThis.__nativeArPortalUseWebGPUFastPath = true;
(function () {
    // The AO preludes reshape the pipeline during the first frames.
    var settledFrames = 60;
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var engine = scene.getEngine();
        if (!engine.isWebGPU || engine.compatibilityMode !== false || engine.snapshotRendering) {
            throw new Error("Fast snapshot rendering is measured on the WebGPU render-bundle path, enabled by this prelude alone.");
        }
        var ready = 0;
        var observer = scene.onAfterRenderObservable.add(function () {
            ready = scene.isReady() ? ready + 1 : 0;
            if (ready < settledFrames) return;
            scene.onAfterRenderObservable.remove(observer);
            // A mesh culled in the recorded frame would never be drawn again.
            scene.meshes.forEach(function (mesh) { mesh.alwaysSelectAsActiveMesh = true; });
            // Record a whole frame: one frame to select every mesh, then enable
            // at a frame boundary. Enabling inside a frame records its remainder.
            var framesToWait = 2;
            var frameObserver = engine.onBeginFrameObservable.add(function () {
                if (--framesToWait > 0) return;
                engine.onBeginFrameObservable.remove(frameObserver);
                engine.snapshotRenderingMode = BABYLON.Constants.SNAPSHOTRENDERING_FAST;
                engine.snapshotRendering = true;
                status("native-xr-portal:snapshot-rendering=fast:meshes=" + scene.meshes.length);
            });
        });
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-fast-snapshot";
})();
