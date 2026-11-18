// Validation only: bounded BRDF arithmetic. Positions, depth, HDR sums and
// GGX distribution/visibility remain f32. Unsupported devices fail requestDevice.
(function () {
    var includes = BABYLON.ShaderStore.IncludesShadersStoreWGSL;
    var source = includes.pbrBRDFFunctions;
    if (typeof source !== "string") throw new Error("F16 BRDF validation requires the published WGSL BRDF include.");
    if (source.includes("portalFp16Pow5")) {
        throw new Error("F16 BRDF validation must be installed only once per script context.");
    }
    function replaceFunction(name, replacement) {
        var pattern = new RegExp("fn " + name + "\\([^]*?\\n?\\{[^}]*\\}");
        if ((source.match(new RegExp(pattern.source, "g")) || []).length !== 1) {
            throw new Error("F16 BRDF validation no longer matches function " + name);
        }
        source = source.replace(pattern, replacement);
    }
    function replaceOnce(value, pattern, replacement, label) {
        if (typeof value !== "string" || (value.match(new RegExp(pattern.source, "g")) || []).length !== 1) {
            throw new Error("F16 coherent block no longer matches " + label);
        }
        return value.replace(pattern, replacement);
    }
    replaceFunction("fresnelSchlickGGXVec3", `fn fresnelSchlickGGXVec3(VdotH: f32, reflectance0: vec3f, reflectance90: vec3f) -> vec3f {
        let r0 = vec3h(reflectance0);
        return vec3f(r0 + (vec3h(reflectance90) - r0) * portalFp16Pow5(1.0h - f16(VdotH)));
    }`);
    replaceFunction("fresnelSchlickGGX", `fn fresnelSchlickGGX(VdotH: f32, reflectance0: f32, reflectance90: f32) -> f32 {
        let r0 = f16(reflectance0);
        return f32(r0 + (f16(reflectance90) - r0) * portalFp16Pow5(1.0h - f16(VdotH)));
    }`);
    replaceFunction("diffuseBRDF_Burley", `fn diffuseBRDF_Burley(NdotL: f32, NdotV: f32, VdotH: f32, roughness: f32) -> f32 {
        return f32(portalDiffuseBurleyH(vec3h(vec3f(NdotL, NdotV, VdotH)), f16(roughness)));
    }`);
    replaceFunction("getReflectanceFromBRDFLookup", `fn getReflectanceFromBRDFLookup(specularEnvironmentR0: vec3f, environmentBrdf: vec3f) -> vec3f {
        let r0 = vec3h(specularEnvironmentR0);
        let brdf = vec3h(environmentBrdf);
#ifdef BRDF_V_HEIGHT_CORRELATED
        return vec3f(mix(brdf.xxx, brdf.yyy, r0));
#else
        return vec3f(r0 * brdf.x + brdf.y);
#endif
    }`);
    replaceFunction("getReflectanceFromBRDFWithEnvLookup", `fn getReflectanceFromBRDFWithEnvLookup(specularEnvironmentR0: vec3f, specularEnvironmentR90: vec3f, environmentBrdf: vec3f) -> vec3f {
        let r0 = vec3h(specularEnvironmentR0);
        let r90 = vec3h(specularEnvironmentR90);
        let brdf = vec3h(environmentBrdf);
#ifdef BRDF_V_HEIGHT_CORRELATED
        return vec3f((r90 - r0) * brdf.x + r0 * brdf.y);
#else
        return vec3f(r0 * brdf.x + r90 * brdf.y);
#endif
    }`);
    source = `fn portalFp16Pow5(value: f16) -> f16 { let squared = value * value; return squared * squared * value; }
fn portalDiffuseBurleyH(dots: vec3h, roughness: f16) -> f16 {
    let grazing = clamp(vec2h(1.0h) - dots.xy, vec2h(0.0h), vec2h(1.0h));
    let squared = grazing * grazing;
    let fresnel = squared * squared * grazing;
    let f90 = 0.5h + 2.0h * dots.z * dots.z * roughness;
    let scatter = vec2h(1.0h) + (f90 - 1.0h) * fresnel;
    return scatter.x * scatter.y * 0.31830988618h;
}
` + source;
    var direct = replaceOnce(includes.pbrDirectLightingFunctions,
        /fn computeDiffuseLighting\([^]*?\{[^]*?return diffuseTerm\s*\*\s*info.attenuation\s*\*\s*info.NdotL\s*\*\s*lightColor;\s*\}/,
        `fn computeDiffuseLighting(info: preLightingInfo, lightColor: vec3f) -> vec3f {
#if BASE_DIFFUSE_MODEL == BRDF_DIFFUSE_MODEL_EON
    let clampedAlbedo = clamp(info.surfaceAlbedo, vec3f(0.1), vec3f(1.0));
    let diffuseTerm = diffuseBRDF_EON(clampedAlbedo, info.diffuseRoughness, info.NdotL, info.NdotV, info.LdotV) / clampedAlbedo;
    return diffuseTerm * info.attenuation * info.NdotL * lightColor;
#else
    let dots = vec3h(vec3f(info.NdotL, info.NdotV, info.VdotH));
    var diffuseTerm = 0.31830988618h;
#if BASE_DIFFUSE_MODEL == BRDF_DIFFUSE_MODEL_LEGACY
    diffuseTerm = portalDiffuseBurleyH(dots, f16(info.roughness));
#elif BASE_DIFFUSE_MODEL == BRDF_DIFFUSE_MODEL_BURLEY
    diffuseTerm = portalDiffuseBurleyH(dots, f16(info.diffuseRoughness));
#endif
    // The bounded lobe and cosine stay half; HDR light/attenuation stay float.
    return f32(diffuseTerm * dots.x) * info.attenuation * lightColor;
#endif
}`, "diffuse lighting");
    var reflectivity = replaceOnce(includes.pbrBlockReflectivity, /\)->reflectivityOutParams\s*\{/,
        `)->reflectivityOutParams {
#if defined(METALLICWORKFLOW) && !defined(DETAIL) && !defined(MICROSURFACEMAP) && DEBUGMODE == 0
    var outParams: reflectivityOutParams;
    // Preserve roughness/IOR precision before GGX and refraction calculations.
    var metallicRoughness = reflectivityColor.rg;
    var ior = reflectivityColor.b;
#ifdef REFLECTIVITY
#ifdef METALLNESSSTOREINMETALMAPBLUE
    metallicRoughness.x *= surfaceMetallicOrReflectivityColorMap.b;
#else
    metallicRoughness.x *= surfaceMetallicOrReflectivityColorMap.r;
#endif
#ifdef ROUGHNESSSTOREINMETALMAPALPHA
    metallicRoughness.y *= surfaceMetallicOrReflectivityColorMap.a;
#elif defined(ROUGHNESSSTOREINMETALMAPGREEN)
    metallicRoughness.y *= surfaceMetallicOrReflectivityColorMap.g;
#endif
#endif
    var microSurface = reflectivityColor.a;
    var surfaceReflectivityColor = reflectivityColor.rgb;
#define CUSTOM_FRAGMENT_UPDATE_METALLICROUGHNESS
    microSurface = clamp(1.0 - metallicRoughness.y, 0.0, 1.0);
    surfaceReflectivityColor = metallicReflectanceFactors.rgb;
    let metallic = f16(metallicRoughness.x);
    let weight = f16(metallicReflectanceFactors.a);
    let baseColor = vec3h(surfaceAlbedo);
    let specularColor = vec3h(surfaceReflectivityColor);
    let dielectricF0 = f16(reflectivityColor.a) * weight;
    let dielectricColorF0 = dielectricF0 * specularColor;
    let colorF0 = mix(dielectricColorF0, baseColor, metallic);
#ifdef LEGACY_SPECULAR_ENERGY_CONSERVATION
    let albedo = baseColor * (vec3h(1.0h) - dielectricColorF0) * (1.0h - metallic);
    let reflectanceF0 = max(colorF0.x, max(colorF0.y, colorF0.z));
    let f90Scale = 1.0h;
    let reflectanceF90 = vec3h(weight);
#else
    let albedo = baseColor;
#if DIELECTRIC_SPECULAR_MODEL == DIELECTRIC_SPECULAR_MODEL_GLTF
    let reflectanceF0 = mix(dielectricF0 * max(specularColor.x, max(specularColor.y, specularColor.z)), 1.0h, metallic);
#else
    let reflectanceF0 = mix(dielectricF0, 1.0h, metallic);
#endif
    let f90Scale = f16(clamp(2.0 * (ior - 1.0), 0.0, 1.0));
    let reflectanceF90 = vec3h(mix(weight * f90Scale, 1.0h, metallic));
#endif
#if DIELECTRIC_SPECULAR_MODEL == DIELECTRIC_SPECULAR_MODEL_OPENPBR
    let dielectricColorF90 = specularColor * (weight * f90Scale);
#else
    let dielectricColorF90 = vec3h(weight * f90Scale);
#endif
#if CONDUCTOR_SPECULAR_MODEL == CONDUCTOR_SPECULAR_MODEL_OPENPBR
    let conductorColorF90 = specularColor;
#elif defined(LEGACY_SPECULAR_ENERGY_CONSERVATION)
    let conductorColorF90 = reflectanceF90;
#else
    let conductorColorF90 = vec3h(1.0h);
#endif
    let colorF90 = mix(dielectricColorF90, conductorColorF90, metallic);
    outParams.surfaceAlbedo = vec3f(albedo);
    outParams.colorReflectanceF0 = vec3f(colorF0);
    outParams.colorReflectanceF90 = vec3f(colorF90);
    outParams.dielectricColorF0 = vec3f(dielectricColorF0);
    outParams.reflectanceF0 = f32(reflectanceF0);
    outParams.reflectanceF90 = vec3f(reflectanceF90);
    outParams.metallic = metallicRoughness.x;
    outParams.specularWeight = metallicReflectanceFactors.a;
    outParams.microSurface = microSurface;
    outParams.roughness = 1.0 - microSurface;
    outParams.diffuseRoughness = baseDiffuseRoughness;
#ifdef BASE_DIFFUSE_ROUGHNESS
    outParams.diffuseRoughness *= baseDiffuseRoughnessTexture * baseDiffuseRoughnessInfos.y;
#endif
#if defined(REFLECTIVITY) && defined(AOSTOREINMETALMAPRED)
    outParams.ambientOcclusionColor = vec3f(mix(vec3h(ambientOcclusionColorIn), vec3h(f16(surfaceMetallicOrReflectivityColorMap.r)), f16(reflectivityInfos.z)));
#endif
    return outParams;
#else
`, "reflectivity block signature");
    reflectivity = replaceOnce(reflectivity, /return outParams;\}\s*$/, "return outParams;\n#endif\n}\n", "reflectivity block return");
    // Babylon's finalizer prepends structs, so an enable directive in a shader
    // include is too late. This compile-only shim is scoped to marked shaders.
    var configureEngine = globalThis.__nativePlaygroundConfigureEngine;
    globalThis.__nativePlaygroundConfigureEngine = function (engine) {
        if (configureEngine) configureEngine(engine);
        if (!engine.enabledExtensions.includes("shader-f16")) {
            throw new Error("F16 BRDF validation requires shader-f16 on the requested device, not just the adapter.");
        }
        var processor = engine._getShaderProcessor(BABYLON.ShaderLanguage.WGSL);
        var finalize = processor && processor.finalizeShaders;
        if (typeof finalize !== "function") throw new Error("F16 validation requires the WebGPU WGSL finalizer.");
        processor.finalizeShaders = function (vertex, fragment) {
            var result = finalize.call(this, vertex, fragment);
            if (result.vertexCode.includes("portalFp16Pow5")) {
                result.vertexCode = "enable f16;\n" + result.vertexCode;
            }
            if (result.fragmentCode.includes("portalFp16Pow5")) {
                result.fragmentCode = "enable f16;\n" + result.fragmentCode;
            }
            return result;
        };
    };
    includes.pbrBRDFFunctions = source;
    includes.pbrDirectLightingFunctions = direct;
    includes.pbrBlockReflectivity = reflectivity;
    globalThis.__nativePlaygroundRequiredFeatures = Array.from(new Set((globalThis.__nativePlaygroundRequiredFeatures || []).concat(["shader-f16"])));
    globalThis.__nativeArPortalBenchmarkLabel = (globalThis.__nativeArPortalBenchmarkLabel ? globalThis.__nativeArPortalBenchmarkLabel + "-" : "") + "f16-blocks-v3";
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        status("native-xr-portal:f16-brdf=coherent-metallic-reflectivity-and-diffuse:projection-depth-roughness-ggx-hdr=f32");
    };
})();
