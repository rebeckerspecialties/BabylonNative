// Validation only: higher-sample control to distinguish AO sampling noise from
// material/depth artifacts. This is not the default performance workload.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var source = store.ssao2PixelShader;
    ["PORTAL_SLICE_COUNT", "PORTAL_STEPS_PER_SIDE"].forEach(function (name) {
        var pattern = new RegExp("const " + name + ": u32 = 2u;", "g");
        if (typeof source !== "string" || (source.match(pattern) || []).length !== 1) {
            throw new Error("AO quality comparison requires the unmodified two-slice/two-step GT-VBAO shader: " + name);
        }
        source = source.replace(pattern, "const " + name + ": u32 = 4u;");
    });
    store.ssao2PixelShader = source;
    var previous = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previous) previous(scene, status);
        status("shimmer-diagnostic:ao-slices=4:stepsPerSide=4:strength-radius-resolution-unchanged");
    };
})();
