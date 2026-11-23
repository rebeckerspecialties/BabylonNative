// Validation only: execute the installed GT-VBAO kernel against analytical surfaces.
globalThis.__babylonPlaygroundSelfManagedScript = true;
(async function () {
    var shader = BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    var start = shader.indexOf("const PORTAL_SLICE_COUNT:");
    var end = shader.indexOf("#else\n#ifdef BLUR\nuniform outSize");
    if (start < 0 || end < start || !shader.includes("fn portalClipSlab")) {
        throw new Error("Plane probe requires the GT-VBAO prelude.");
    }
    // Keep the actual kernel; replace only Babylon's shader declaration syntax.
    var kernel = shader.slice(start, end).replace("#ifdef NORMAL_WORLDSPACE", "").replace("#endif", "");
    kernel = kernel.replace("fn main(input: FragmentInputs) -> FragmentOutputs {",
        "fn main(input: FragmentInputs) -> FragmentOutputs {\nvar fragmentOutputs: FragmentOutputs;");
    if (kernel.includes("#")) throw new Error("Unexpected kernel preprocessor directive.");
    var declarations = `
struct Uniforms {
    projection: mat4x4f,
    normalWorldToView: mat3x3f,
    xViewport: f32, yViewport: f32, radius: f32, totalStrength: f32,
    base: f32, maxZ: f32,
}
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var depthSampler: texture_2d<f32>;
@group(0) @binding(2) var depthSamplerSampler: sampler;
@group(0) @binding(3) var normalSampler: texture_2d<f32>;
@group(0) @binding(4) var normalSamplerSampler: sampler;
struct FragmentInputs { @builtin(position) position: vec4f, @location(0) vUV: vec2f }
struct FragmentOutputs { @location(0) color: vec4f }
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> FragmentInputs {
    let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    var output: FragmentInputs;
    output.position = vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
    output.vUV = vec2f(uv.x, 1.0 - uv.y);
    return output;
}
`;
    var device = await (await navigator.gpu.requestAdapter()).requestDevice();
    device.addEventListener("uncapturederror", function (event) {
        console.error("gtvbao-plane:gpu-error:" + event.error.message);
        TestUtils.exit(1);
    });
    var layout = device.createBindGroupLayout({ entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "non-filtering" } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "non-filtering" } }
    ] });
    var pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [layout] });
    var sampler = device.createSampler();
    var failures = [];
    var tests = 0;
    for (var samples of [2, 4]) {
        var source = declarations + kernel.replace("PORTAL_SLICE_COUNT: u32 = 2u", "PORTAL_SLICE_COUNT: u32 = " + samples + "u")
            .replace("PORTAL_STEPS_PER_SIDE: u32 = 2u", "PORTAL_STEPS_PER_SIDE: u32 = " + samples + "u");
        var module = device.createShaderModule({ code: source });
        var pipeline = device.createRenderPipeline({ layout: pipelineLayout,
            vertex: { module: module, entryPoint: "vertexMain" },
            fragment: { module: module, entryPoint: "main", targets: [{ format: "rgba8unorm" }] } });
        for (var config of [
            { depth: [48, 32], ao: [48, 32], sign: 1, y: 1, offset: [0, 0] },
            { depth: [48, 32], ao: [24, 16], sign: 1, y: 1, offset: [0, 0] },
            { depth: [48, 32], ao: [19, 13], sign: 1, y: 1, offset: [0.2, -0.3] },
            { depth: [32, 48], ao: [16, 24], sign: -1, y: 1, offset: [-0.2, 0.3] },
            { depth: [32, 48], ao: [13, 19], sign: -1, y: -1, offset: [0.2, -0.3] },
            { depth: [48, 32], ao: [24, 16], sign: 1, y: -1, offset: [0, 0] }
        ]) {
            for (var blocker of [false, true]) {
                var width = config.depth[0], height = config.depth[1];
                var aoWidth = config.ao[0], aoHeight = config.ao[1];
                var normal = blocker ? [0, 0, -config.sign] : [0.1, 0.75, -1.2 * config.sign];
                var length = Math.hypot.apply(Math, normal);
                normal = normal.map(function (value) { return value / length; });
                var depths = new Float32Array(width * height);
                var normals = new Float32Array(width * height * 4);
                for (var y = 0; y < height; y++) for (var x = 0; x < width; x++) {
                    var ray = [(2 * (x + 0.5) / width - 1 - config.offset[0] * config.sign) / 2,
                        (2 * (y + 0.5) / height - 1 - config.offset[1] * config.sign) / (2 * config.y), config.sign];
                    var distance = -4 / (normal[0] * ray[0] + normal[1] * ray[1] + normal[2] * ray[2]);
                    if (blocker && x >= width * 0.4 && x < width * 0.6 && y >= height * 0.25 && y < height * 0.75) distance -= 0.5;
                    depths[y * width + x] = distance * config.sign;
                    normals.set(normal, (y * width + x) * 4);
                }
                var values = new Float32Array(36);
                values[0] = 2; values[5] = 2 * config.y;
                values[8] = config.offset[0]; values[9] = config.offset[1]; values[11] = config.sign;
                values[16] = values[21] = values[26] = 1;
                values.set([0.5, 0.5 / config.y, 1, 1, 0, 100], 28);
                var uniform = device.createBuffer({ size: values.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
                var depthTexture = device.createTexture({ size: [width, height], format: "r32float", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
                var normalTexture = device.createTexture({ size: [width, height], format: "rgba32float", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
                var output = device.createTexture({ size: [aoWidth, aoHeight], format: "rgba8unorm", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
                var readback = device.createBuffer({ size: 256 * aoHeight, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
                device.queue.writeBuffer(uniform, 0, values);
                device.queue.writeTexture({ texture: depthTexture }, depths, { bytesPerRow: width * 4 }, [width, height]);
                device.queue.writeTexture({ texture: normalTexture }, normals, { bytesPerRow: width * 16 }, [width, height]);
                var bindGroup = device.createBindGroup({ layout: layout, entries: [
                    { binding: 0, resource: { buffer: uniform } }, { binding: 1, resource: depthTexture.createView() },
                    { binding: 2, resource: sampler }, { binding: 3, resource: normalTexture.createView() }, { binding: 4, resource: sampler }
                ] });
                await new Promise(function (resolve) {
                    requestAnimationFrame(function () {
                        var encoder = device.createCommandEncoder();
                        var pass = encoder.beginRenderPass({ colorAttachments: [{ view: output.createView(), clearValue: [0, 0, 0, 0], loadOp: "clear", storeOp: "store" }] });
                        pass.setPipeline(pipeline); pass.setBindGroup(0, bindGroup); pass.draw(3); pass.end();
                        encoder.copyTextureToBuffer({ texture: output }, { buffer: readback, bytesPerRow: 256 }, [aoWidth, aoHeight]);
                        device.queue.submit([encoder.finish()]);
                        resolve();
                    });
                });
                await readback.mapAsync(GPUMapMode.READ);
                var bytes = new Uint8Array(readback.getMappedRange());
                var minimum = 255, dark = 0;
                for (var row = 0; row < aoHeight; row++) for (var column = 0; column < aoWidth; column++) {
                    var byte = bytes[row * 256 + column * 4];
                    minimum = Math.min(minimum, byte);
                    if (byte < 254) dark++;
                    if (bytes[row * 256 + column * 4 + 3] !== 255) throw new Error("Probe did not render every pixel.");
                }
                readback.unmap();
                for (var resource of [uniform, depthTexture, normalTexture, output, readback]) resource.destroy();
                var result = { samples: samples, config: config, blocker: blocker, minimum: minimum, dark: dark };
                console.log("gtvbao-plane:case=" + JSON.stringify(result));
                if (blocker ? dark === 0 : dark !== 0) failures.push(result);
                tests++;
            }
        }
    }
    if (failures.length) throw new Error("GT-VBAO planar self-occlusion/blocker failures: " + JSON.stringify(failures));
    console.log("gtvbao-plane:passed=" + tests);
    TestUtils.exit(0);
})().catch(function (error) {
    console.error("gtvbao-plane:error:" + error.message + "\n" + error.stack);
    TestUtils.exit(1);
});
