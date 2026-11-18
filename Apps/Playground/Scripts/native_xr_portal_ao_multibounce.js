// Validation only: lets occluded light return by interreflection. Plain AO
// treats every occluder as black, which shades a bright wall from the bright
// pavement beside it. The fit is Jimenez et al., "Practical Real-Time Strategies
// for Accurate Indirect Occlusion" (2016), with the lit scene color standing in
// for the albedo: dark or shadowed surfaces keep their occlusion.
// Load after the color-reuse prelude and before a capture or benchmark prelude.
(function () {
    var combined = "vec4f(sceneColor.rgb * ssaoColor.r, sceneColor.a)";
    var fragmentEntry = /@fragment/g;
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var store = BABYLON.ShaderStore.ShadersStoreWGSL;
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        var source = store.portalSsaoColorReusePixelShader;
        if (!pipeline || typeof source !== "string" || source.split(combined).length !== 2 || (source.match(fragmentEntry) || []).length !== 1) {
            throw new Error("Multi-bounce AO requires the R8 and color-reuse combine contract.");
        }
        store.portalSsaoMultiBouncePixelShader = source.replace(fragmentEntry, `
fn portalMultiBounce(visibility: f32, reflectance: vec3f) -> vec3f {
    let a = 2.0404 * reflectance - 0.3324;
    let b = -4.7951 * reflectance + 0.6417;
    let c = 2.7552 * reflectance + 0.6903;
    return max(vec3f(visibility), ((visibility * a + b) * visibility + c) * visibility);
}
@fragment`).replace(combined, "vec4f(sceneColor.rgb * portalMultiBounce(ssaoColor.r, clamp(sceneColor.rgb, vec3f(0.0), vec3f(1.0))), sceneColor.a)");
        pipeline._ssaoCombinePostProcess.updateEffect(null, null, null, undefined, undefined, undefined, undefined, "portalSsaoMultiBounce");
        status("native-xr-portal:ao-combine=multi-bounce:reflectance=scene-color");
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-multi-bounce";
})();
