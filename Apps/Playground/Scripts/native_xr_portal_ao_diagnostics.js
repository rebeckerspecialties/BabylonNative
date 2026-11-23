// Validation only: inspect the actual AO inputs without changing XR poses or resolution.
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        var geometry = scene.geometryBufferRenderer;
        if (!pipeline || !geometry || !pipeline._ssaoCombinePostProcess) {
            reportStatus("native-xr-portal:ao-diagnostic=unavailable");
            return;
        }
        BABYLON.ShaderStore.ShadersStoreWGSL.nativePortalAoDiagnosticPixelShader = `
varying vUV: vec2f;
var textureSamplerSampler: sampler;
var textureSampler: texture_2d<f32>;
var originalColorSampler: sampler;
var originalColor: texture_2d<f32>;
var depthSamplerSampler: sampler;
var depthSampler: texture_2d<f32>;
var normalSamplerSampler: sampler;
var normalSampler: texture_2d<f32>;
var rawAoSamplerSampler: sampler;
var rawAoSampler: texture_2d<f32>;
uniform viewport: vec4f;
uniform diagnosticMode: f32;
@fragment
fn main(input: FragmentInputs) -> FragmentOutputs {
    let uv = uniforms.viewport.xy + input.vUV * uniforms.viewport.zw;
    let ao = textureSampleLevel(textureSampler, textureSamplerSampler, uv, 0.0);
    let color = textureSampleLevel(originalColor, originalColorSampler, uv, 0.0);
    var result = color * ao;
    if (uniforms.diagnosticMode == 1.0) {
        result = vec4f(ao.rgb, 1.0);
    } else if (uniforms.diagnosticMode == 2.0) {
        let depth = abs(textureSampleLevel(depthSampler, depthSamplerSampler, uv, 0.0).r);
        result = vec4f(vec3f(clamp(depth / 10.0, 0.0, 1.0)), 1.0);
    } else if (uniforms.diagnosticMode == 3.0) {
        let normal = textureSampleLevel(normalSampler, normalSamplerSampler, uv, 0.0).rgb;
        result = vec4f(normal * 0.5 + 0.5, 1.0);
    } else if (uniforms.diagnosticMode == 4.0) {
        result = color;
    } else if (uniforms.diagnosticMode == 5.0) {
        let rawAo = textureSampleLevel(rawAoSampler, rawAoSamplerSampler, uv, 0.0);
        result = vec4f(rawAo.rgb, 1.0);
    }
    fragmentOutputs.color = result;
}`;
        var combine = pipeline._ssaoCombinePostProcess;
        combine.updateEffect(null, ["scale", "viewport", "diagnosticMode"], ["textureSampler", "originalColor", "depthSampler", "normalSampler", "rawAoSampler"],
            undefined, undefined, undefined, undefined, "nativePortalAoDiagnostic");
        var start = performance.now();
        var modes = [0, 2, 3, 5, 1, 4, 0];
        var labels = ["combined", "depth", "normals", "raw-ao", "filtered-ao", "color-only", "combined-restored"];
        var lastPhase = -1;
        var observer = combine.onApplyObservable.add(function (effect) {
            var phase = Math.min(modes.length - 1, Math.floor((performance.now() - start) / 15000));
            effect.setFloat("diagnosticMode", modes[phase]);
            effect.setTexture("depthSampler", geometry.getGBuffer().textures[0]);
            effect.setTexture("normalSampler", geometry.getGBuffer().textures[1]);
            effect.setTextureFromPostProcessOutput("rawAoSampler", pipeline._ssaoPostProcess);
            if (phase !== lastPhase) {
                lastPhase = phase;
                var camera = scene.activeCamera;
                reportStatus("native-xr-portal:ao-diagnostic=" + labels[phase] +
                    ":gbuffer=" + JSON.stringify(geometry.getGBuffer().getSize()) +
                    ":camera=" + camera.name + ":viewport=" + JSON.stringify(camera.viewport) +
                    ":projection=" + Array.from(camera.getProjectionMatrix().m).join(","));
            }
        });
        scene.onDisposeObservable.add(function () {
            combine.onApplyObservable.remove(observer);
        });
    };
})();
