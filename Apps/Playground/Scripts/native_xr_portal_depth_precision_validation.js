// Validation only: isolate depth precision without changing the far plane,
// material arithmetic, sample count, or render resolution. Never use for XR.
(function () {
    var previous = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previous) previous(scene, status);
        var camera = scene.activeCamera;
        if (globalThis.__nativeArPortalForceNonXrStress !== true || !camera ||
            camera.mode !== BABYLON.Camera.PERSPECTIVE_CAMERA || camera.minZ !== 0.05) {
            throw new Error("Depth precision comparison requires the unmodified non-XR perspective camera.");
        }
        camera.minZ = 0.5;
        status("shimmer-diagnostic:near=0.5:far=" + camera.maxZ);
    };
})();
