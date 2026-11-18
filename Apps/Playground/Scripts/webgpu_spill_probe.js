// Diagnostic only: compare compiler spill reports without scene or PBR work.
globalThis.__babylonPlaygroundSelfManagedScript = true;
(async function () {
    var adapter = await navigator.gpu.requestAdapter();
    var device = await adapter.requestDevice({ requiredFeatures: ["shader-f16"] });
    device.addEventListener("uncapturederror", function (event) {
        console.error("spill-probe:error:" + event.error.message);
        TestUtils.exit(1);
    });
    async function verifySnorm16() {
        var count = 65536;
        var values = new Int16Array(count * 4);
        for (var i = 0; i < count; i++) values.set([i - 32768, 32767 - i, -32768, 32767], i * 4);
        var input = device.createBuffer({ size: values.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
        device.queue.writeBuffer(input, 0, values);
        var texture = device.createTexture({ size: [256, 256], format: "rgba32float", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
        var readback = device.createBuffer({ size: count * 16, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        var shader = device.createShaderModule({ code: `
struct V { @builtin(position) position: vec4f, @location(0) @interpolate(flat) value: vec4f };
@vertex fn vs(@builtin(vertex_index) index: u32, @location(0) value: vec4f) -> V {
    let xy = (vec2f(f32(index % 256u), f32(index / 256u)) + vec2f(0.5)) / 256.0;
    return V(vec4f(xy.x * 2.0 - 1.0, 1.0 - xy.y * 2.0, 0.5, 1.0), value);
}
@fragment fn fs(input: V) -> @location(0) vec4f { return input.value; }` });
        var pipeline = device.createRenderPipeline({ layout: "auto", vertex: { module: shader, entryPoint: "vs",
            buffers: [{ arrayStride: 8, attributes: [{ shaderLocation: 0, offset: 0, format: "snorm16x4" }] }] },
            fragment: { module: shader, entryPoint: "fs", targets: [{ format: "rgba32float" }] }, primitive: { topology: "point-list" } });
        var encoder = device.createCommandEncoder();
        var pass = encoder.beginRenderPass({ colorAttachments: [{ view: texture.createView(), loadOp: "clear", storeOp: "store", clearValue: [2, 2, 2, 2] }] });
        pass.setPipeline(pipeline);
        pass.setVertexBuffer(0, input);
        pass.draw(count);
        pass.end();
        encoder.copyTextureToBuffer({ texture: texture }, { buffer: readback, bytesPerRow: 4096 }, [256, 256]);
        device.queue.submit([encoder.finish()]);
        await readback.mapAsync(GPUMapMode.READ);
        var actual = new Float32Array(readback.getMappedRange());
        var maxError = 0;
        for (var index = 0; index < values.length; index++) {
            var expected = Math.fround(Math.max(values[index] / 32767, -1));
            var error = Math.abs(actual[index] - expected);
            if (!Number.isFinite(actual[index]) || error > 1.1920928955078125e-7) {
                throw new Error("Snorm16 mismatch at " + index + ": " + actual[index] + " versus " + expected);
            }
            maxError = Math.max(maxError, error);
        }
        readback.unmap();
        readback.destroy();
        texture.destroy();
        input.destroy();
        console.log("spill-probe:snorm16-verified:values=" + values.length + ":maxError=" + maxError);
    }
    await verifySnorm16();
    var positions = device.createBuffer({ size: 24, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    var normals = device.createBuffer({ size: 24, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(positions, 0, new Int16Array([-32767, -32767, 0, 32767, 32767, -32767, 0, 32767, -32767, 32767, 0, 32767]));
    device.queue.writeBuffer(normals, 0, new Int16Array([0, 0, 32767, 0, 0, 0, 32767, 0, 0, 0, 32767, 0]));
    var floatPositions = device.createBuffer({ size: 48, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    var floatNormals = device.createBuffer({ size: 48, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(floatPositions, 0, new Float32Array([-1, -1, 0, 1, 1, -1, 0, 1, -1, 1, 0, 1]));
    device.queue.writeBuffer(floatNormals, 0, new Float32Array([0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0]));
    var bytePositions = device.createBuffer({ size: 12, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(bytePositions, 0, new Int8Array([-127, -127, 0, 127, 127, -127, 0, 127, -127, 127, 0, 127]));
    var targets = ["rgba16float", "rgba16float", "r16float"].map(function (format) {
        return device.createTexture({ size: [64, 64], format: format, usage: GPUTextureUsage.RENDER_ATTACHMENT }).createView();
    });
    var depth = device.createTexture({ size: [64, 64], format: "depth24plus-stencil8", usage: GPUTextureUsage.RENDER_ATTACHMENT }).createView();
    var variants = [
        { name: "one-f32", count: 1 },
        { name: "two-f32", count: 2 },
        { name: "three-f32", count: 3 },
        { name: "three-scalar-depth", count: 3, scalar: true },
        { name: "three-f16-math", count: 3, half: true, scalar: true },
        { name: "one-pulled", count: 1, pulled: true },
        { name: "three-pulled", count: 3, pulled: true },
        { name: "one-snorm16-input", count: 1, pulled: true, single: true },
        { name: "one-snorm8-input", count: 1, pulled: true, single: true, format: "snorm8x4", stride: 4 },
        { name: "one-float-input", count: 1, pulled: true, single: true, format: "float32x4", stride: 16 },
        { name: "two-float-inputs", count: 1, pulled: true, format: "float32x4", stride: 16 },
        { name: "storage-input", count: 1, storage: true },
    ];
    device.pushErrorScope("validation");
    variants.forEach(function (variant) {
        var vertex = variant.pulled
            ? (variant.single
                ? "@vertex fn vs(@location(0) p: vec4f) -> V { return V(vec4f(p.xy, 0.5, 1.0), vec3f(0.0, 0.0, 1.0)); }"
                : "@vertex fn vs(@location(0) p: vec4f, @location(1) n: vec4f) -> V { return V(vec4f(p.xy, 0.5, 1.0), n.xyz); }")
            : variant.storage
            ? "@group(0) @binding(0) var<storage, read> points: array<vec4f>; @vertex fn vs(@builtin(vertex_index) index: u32) -> V { return V(vec4f(points[index].xy, 0.5, 1.0), vec3f(0.0, 0.0, 1.0)); }"
            : "@vertex fn vs(@builtin(vertex_index) index: u32) -> V { let x = f32((index << 1u) & 2u); let y = f32(index & 2u); return V(vec4f(vec2f(x,y) * 2.0 - 1.0, 0.5, 1.0), vec3f(0.0, 0.0, 1.0)); }";
        var fields = [];
        var values = [];
        for (var i = 0; i < variant.count; i++) {
            var scalar = variant.scalar && i === 2;
            var type = scalar ? "f32" : "vec4f";
            fields.push("@location(" + i + ") c" + i + ": " + type);
            values.push(scalar ? "0.5" : variant.half
                ? "vec4f(vec4h(vec3h(input.normal) * 0.25h + vec3h(0.5h), 1.0h))"
                : "vec4f(input.normal * 0.25 + vec3f(0.5), 1.0)");
        }
        var code = (variant.half ? "enable f16;\n" : "") +
            "struct V { @builtin(position) position: vec4f, @location(0) normal: vec3f };\n" + vertex +
            "\nstruct F { " + fields.join(", ") + " };\n@fragment fn fs(input: V) -> F { return F(" + values.join(", ") + "); }";
        var shader = device.createShaderModule({ code: code });
        variant.pipeline = device.createRenderPipeline({
            label: "spill-probe-" + variant.name,
            layout: "auto",
            vertex: { module: shader, entryPoint: "vs", buffers: variant.pulled ? (variant.single ? [0] : [0, 1]).map(function (location) {
                return { arrayStride: variant.stride || 8, attributes: [{ shaderLocation: location, offset: 0, format: variant.format || "snorm16x4" }] };
            }) : [] },
            fragment: { module: shader, entryPoint: "fs", targets: ["rgba16float", "rgba16float", "r16float"].slice(0, variant.count).map(function (format) { return { format: format }; }) },
            primitive: { topology: "triangle-list" },
            depthStencil: { format: "depth24plus-stencil8", depthWriteEnabled: true, depthCompare: "always" }
        });
        if (variant.storage) {
            variant.bindGroup = device.createBindGroup({ layout: variant.pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: floatPositions } }] });
        }
    });
    var error = await device.popErrorScope();
    if (error) throw new Error(error.message);
    console.log("spill-probe:ready:" + variants.map(function (variant) { return variant.name; }).join(","));
    var started = performance.now();
    var frames = 0;
    function frame() {
        var encoder = device.createCommandEncoder({ label: "spill-probe" });
        variants.forEach(function (variant) {
            var pass = encoder.beginRenderPass({
                label: "spill-probe-" + variant.name,
                colorAttachments: targets.slice(0, variant.count).map(function (view) { return { view: view, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 0] }; }),
                depthStencilAttachment: { view: depth, depthLoadOp: "clear", depthStoreOp: "discard", depthClearValue: 1, stencilLoadOp: "clear", stencilStoreOp: "discard", stencilClearValue: 0 }
            });
            pass.setPipeline(variant.pipeline);
            if (variant.pulled) {
                pass.setVertexBuffer(0, variant.format === "float32x4" ? floatPositions : variant.format === "snorm8x4" ? bytePositions : positions);
                if (!variant.single) pass.setVertexBuffer(1, variant.format === "float32x4" ? floatNormals : normals);
            }
            if (variant.bindGroup) pass.setBindGroup(0, variant.bindGroup);
            pass.draw(3);
            pass.end();
        });
        device.queue.submit([encoder.finish()]);
        frames++;
        if (performance.now() - started < 30000) {
            requestAnimationFrame(frame);
        } else {
            device.queue.onSubmittedWorkDone().then(function () {
                console.log("spill-probe:complete:frames=" + frames);
                TestUtils.exit(0);
            });
        }
    }
    requestAnimationFrame(frame);
})().catch(function (error) {
    console.error("spill-probe:failed:" + String(error) + "\n" + String(error.stack || ""));
    TestUtils.exit(1);
});
