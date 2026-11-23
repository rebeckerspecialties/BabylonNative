// Validation only: bounded environment-light arithmetic, with an HDR fallback.
// Load after the existing F16 material prelude.
(function () {
    var includes = BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    var source = includes.harmonicsFunctions;
    var pattern = /fn computeEnvironmentIrradiance\(normal:\s*vec3f\)\s*->\s*vec3f\s*\{[^}]*uniforms\.vSphericalL22[^}]*\}/;
    if (typeof source !== "string" || source.includes("portalIrradianceF32") ||
        (source.match(new RegExp(pattern.source, "g")) || []).length !== 1 ||
        !includes.pbrBRDFFunctions.includes("portalFp16Pow5")) {
        throw new Error("Half irradiance requires unchanged spherical harmonics and the F16 material prelude.");
    }
    var coefficients = ["L00", "L1_1", "L10", "L11", "L2_2", "L2_1", "L20", "L21", "L22"];
    var checks = coefficients.map(function (name) { return "any(abs(uniforms.vSpherical" + name + ") > vec3f(8.0))"; });
    includes.harmonicsFunctions = source.replace(pattern, function (original) {
        return original.replace("computeEnvironmentIrradiance", "portalIrradianceF32") + `
fn computeEnvironmentIrradiance(normal: vec3f) -> vec3f {
    // Uniform coefficient checks preserve HDR inputs; the direction guard also
    // covers non-rigid reflection transforms. No positions or HDR sums narrow.
    if (any(abs(normal) > vec3f(2.0)) || ${checks.join(" || ")}) {
        return portalIrradianceF32(normal);
    }
    let n = vec3h(normal);
    let irradiance = vec3h(uniforms.vSphericalL00)
        + vec3h(uniforms.vSphericalL1_1) * n.y
        + vec3h(uniforms.vSphericalL10) * n.z
        + vec3h(uniforms.vSphericalL11) * n.x
        + vec3h(uniforms.vSphericalL2_2) * (n.y * n.x)
        + vec3h(uniforms.vSphericalL2_1) * (n.y * n.z)
        + vec3h(uniforms.vSphericalL20) * (3.0h * n.z * n.z - 1.0h)
        + vec3h(uniforms.vSphericalL21) * (n.z * n.x)
        + vec3h(uniforms.vSphericalL22) * (n.x * n.x - n.y * n.y);
    return vec3f(irradiance);
}`;
    });
    var configure = globalThis.__nativePlaygroundConfigureEngine;
    globalThis.__nativePlaygroundConfigureEngine = function (engine) {
        if (configure) configure(engine);
        if (!engine.enabledExtensions.includes("shader-f16")) {
            throw new Error("Half irradiance requires shader-f16 on the device.");
        }
        var processor = engine._getShaderProcessor(BABYLON.ShaderLanguage.WGSL);
        var finalize = processor.finalizeShaders;
        processor.finalizeShaders = function (vertex, fragment) {
            var result = finalize.call(this, vertex, fragment);
            ["vertexCode", "fragmentCode"].forEach(function (key) {
                if (result[key].includes("portalIrradianceF32") && !/\benable\s+f16\s*;/.test(result[key])) {
                    result[key] = "enable f16;\n" + result[key];
                }
            });
            return result;
        };
    };
    var previous = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previous) previous(scene, status);
        status("native-xr-portal:irradiance=f16-bounded:hdr-and-transform-fallback=f32");
    };
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-half-irradiance";
})();
