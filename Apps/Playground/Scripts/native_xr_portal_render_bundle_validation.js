// Validation only: Babylon's existing dynamic render-bundle cache, not snapshots.
globalThis.__nativeArPortalUseWebGPUFastPath = true;
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var engine = scene.getEngine();
        if (!engine.isWebGPU || engine.compatibilityMode !== false || engine.snapshotRendering ||
            globalThis.__nativeArPortalFreezeMaterials === true || globalThis.__nativeArPortalUseSceneStaticFastPath === true) {
            throw new Error("Render-bundle comparison requires dynamic WebGPU rendering without snapshots or freezing.");
        }
        status("native-xr-portal:render-bundle-comparison=dynamic");
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-render-bundles";
})();
