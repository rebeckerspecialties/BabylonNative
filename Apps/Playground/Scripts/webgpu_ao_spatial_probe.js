// Validation only: compare the installed spatial filter with analytical inputs.
globalThis.__babylonPlaygroundSelfManagedScript = true;
(async function () {
    var shader = BABYLON.ShaderStore.ShadersStoreWGSL.ssao2PixelShader;
    var match = shader.match(/fn portalBlurReciprocalDepth[\s\S]*?return result \/ weightSum;\s*\}/);
    if (!match || /\bfor\s*\(/.test(match[0])) throw new Error("Spatial probe requires the unrolled slope blur prelude.");
    var device = await (await navigator.gpu.requestAdapter()).requestDevice();
    device.addEventListener("uncapturederror", function (event) {
        console.error("ao-spatial:gpu-error:" + event.error.message);
        TestUtils.exit(1);
    });
    var module = device.createShaderModule({ code: `
@group(0) @binding(0) var depthSampler: texture_2d<f32>;
@group(0) @binding(1) var image: texture_2d<f32>;
@group(0) @binding(2) var imageSampler: sampler;
struct Inputs { @builtin(position) position: vec4f }
@vertex fn vertexMain(@builtin(vertex_index) i: u32) -> Inputs {
    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    var result: Inputs; result.position = vec4f(uv * 2.0 - 1.0, 0.0, 1.0); return result;
}
${match[0]}
@fragment fn fragmentMain(input: Inputs) -> @location(0) f32 {
    let size = vec2f(textureDimensions(image));
    return blur13Bilateral(image, imageSampler, input.position.xy / size, vec2f(1.0 / size.x, 0.0));
}
@fragment fn fragmentVertical(input: Inputs) -> @location(0) f32 {
    let size = vec2f(textureDimensions(image));
    return blur13Bilateral(image, imageSampler, input.position.xy / size, vec2f(0.0, 1.0 / size.y));
}` });
    var layout = device.createBindGroupLayout({ entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } }
    ] });
    var pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [layout] });
    var pipeline = device.createRenderPipeline({ layout: pipelineLayout,
        vertex: { module: module, entryPoint: "vertexMain" }, fragment: { module: module, entryPoint: "fragmentMain", targets: [{ format: "r8unorm" }] } });
    var verticalPipeline = device.createRenderPipeline({ layout: pipelineLayout,
        vertex: { module: module, entryPoint: "vertexMain" }, fragment: { module: module, entryPoint: "fragmentVertical", targets: [{ format: "r8unorm" }] } });
    var sampler = device.createSampler({ minFilter: "linear", magFilter: "linear" });
    var tests = 0, maxError = 0;
    for (var dimensions of [
        { ao: [19, 13], depth: [19, 13] }, { ao: [32, 16], depth: [32, 16] },
        { ao: [19, 13], depth: [43, 29] }, { ao: [32, 16], depth: [71, 36] },
        { ao: [13, 19], depth: [29, 43] }
    ]) for (var horizontal of [true, false]) for (var slope of [-0.003, 0, 0.003]) for (var mode of ["plane", "silhouette", "sky", "isolated"]) {
        // Distinct view-local inputs ensure neither eye can read the other's AO.
        for (var eye of [0, 1]) {
            var size = dimensions.ao, depthSize = dimensions.depth;
            var width = size[0], height = size[1], depthWidth = depthSize[0], depthHeight = depthSize[1];
            var depth = new Float32Array(depthWidth * depthHeight), visibility = new Uint8Array(width * height);
            for (var y = 0; y < depthHeight; y++) for (var x = 0; x < depthWidth; x++) {
                var coordinate = horizontal ? x : y, extent = horizontal ? depthWidth : depthHeight;
                var z = 1 / (0.2 + slope * (coordinate - extent / 2));
                if (mode === "silhouette" && coordinate >= extent / 2) z *= 4;
                if (mode === "sky" || mode === "isolated" && coordinate !== Math.floor(extent / 2)) z = 0;
                depth[y * depthWidth + x] = eye ? -z : z;
            }
            for (var y = 0; y < height; y++) for (var x = 0; x < width; x++) {
                visibility[y * width + x] = eye ? 64 : (x * 37 + y * 23) % 256;
            }
            var depthTexture = device.createTexture({ size: depthSize, format: "r32float", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
            var aoTexture = device.createTexture({ size: size, format: "r8unorm", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
            var output = device.createTexture({ size: size, format: "r8unorm", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
            var readback = device.createBuffer({ size: 256 * height, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
            device.queue.writeTexture({ texture: depthTexture }, depth, { bytesPerRow: depthWidth * 4 }, depthSize);
            device.queue.writeTexture({ texture: aoTexture }, visibility, { bytesPerRow: width }, size);
            var group = device.createBindGroup({ layout: layout, entries: [
                { binding: 0, resource: depthTexture.createView() }, { binding: 1, resource: aoTexture.createView() }, { binding: 2, resource: sampler }
            ] });
            await new Promise(function (resolve) {
                requestAnimationFrame(function () {
                    var encoder = device.createCommandEncoder();
                    var pass = encoder.beginRenderPass({ colorAttachments: [{ view: output.createView(), clearValue: [0, 0, 0, 0], loadOp: "clear", storeOp: "store" }] });
                    pass.setPipeline(horizontal ? pipeline : verticalPipeline); pass.setBindGroup(0, group); pass.draw(3); pass.end();
                    encoder.copyTextureToBuffer({ texture: output }, { buffer: readback, bytesPerRow: 256 }, size);
                    device.queue.submit([encoder.finish()]); resolve();
                });
            });
            await readback.mapAsync(GPUMapMode.READ);
            var pixels = new Uint8Array(readback.getMappedRange());
            for (var row = 0; row < height; row++) for (var column = 0; column < width; column++) {
                var sample = function (offset) {
                    var sx = column + (horizontal ? offset : 0), sy = row + (horizontal ? 0 : offset);
                    // Match f32 UV arithmetic and nearest-depth texel selection.
                    var u = Math.fround(Math.fround((column + 0.5) / width) + Math.fround((horizontal ? offset : 0) * Math.fround(1 / width)));
                    var v = Math.fround(Math.fround((row + 0.5) / height) + Math.fround((horizontal ? 0 : offset) * Math.fround(1 / height)));
                    var dx = Math.max(0, Math.min(depthWidth - 1, Math.trunc(Math.fround(u * depthWidth))));
                    var dy = Math.max(0, Math.min(depthHeight - 1, Math.trunc(Math.fround(v * depthHeight))));
                    var d = Math.abs(depth[dy * depthWidth + dx]);
                    return { r: d > 0 ? 1 / d : 0, position: horizontal ? dx : dy, valid: sx >= 0 && sx < width && sy >= 0 && sy < height,
                        ao: visibility[Math.max(0, Math.min(height - 1, sy)) * width + Math.max(0, Math.min(width - 1, sx))] };
                };
                var center = sample(0), left = sample(-1), right = sample(1);
                var ls = (center.r - left.r) / Math.max(center.position - left.position, 1);
                var rs = (right.r - center.r) / Math.max(right.position - center.position, 1);
                var derivative = left.r > 0 && right.r > 0 && ls * rs > 0 ? (Math.abs(ls) < Math.abs(rs) ? ls : rs) : 0;
                var sum = visibility[row * width + column], weightSum = 1;
                for (var offset of [-1, -2, -3, 1, 2, 3]) {
                    var neighbor = sample(offset);
                    if (!neighbor.valid || neighbor.r === 0) continue;
                    var edge = Math.max(0, 1 - Math.abs(neighbor.r - center.r - derivative * (neighbor.position - center.position)) / (center.r * 0.005));
                    var weight = [0, 0.8, 0.4, 0.1][Math.abs(offset)] * edge * edge;
                    sum += neighbor.ao * weight; weightSum += weight;
                }
                var expected = center.r > 0 ? Math.round(sum / weightSum) : 255;
                var error = Math.abs(pixels[row * 256 + column] - expected);
                maxError = Math.max(maxError, error);
                if (error > 1) throw new Error("Spatial filter mismatch " + JSON.stringify({ size: size, depthSize: depthSize, horizontal: horizontal, slope: slope, mode: mode, eye: eye, x: column, y: row, expected: expected, actual: pixels[row * 256 + column] }));
            }
            readback.unmap();
            for (var resource of [readback, output, aoTexture, depthTexture]) resource.destroy();
            tests++;
        }
    }
    console.log("ao-spatial:passed=" + tests + ":maxR8Error=" + maxError);
    TestUtils.exit(0);
})().catch(function (error) {
    console.error("ao-spatial:error:" + error.message + "\n" + error.stack);
    TestUtils.exit(1);
});
