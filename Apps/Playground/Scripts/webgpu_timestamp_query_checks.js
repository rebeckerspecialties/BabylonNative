// Browser-shaped checks: never substitute wall-clock time for a GPU timestamp.
globalThis.__babylonPlaygroundSelfManagedScript = true;
async function checkGpuTimestamps(adapter) {
    const plain = await adapter.requestDevice();
    try {
        if (plain.features.has("timestamp-query")) throw new Error("Unrequested timestamp-query was enabled");
        let rejected = false;
        try {
            plain.createQuerySet({ type: "timestamp", count: 2 });
        } catch (error) {
            rejected = error instanceof TypeError;
        }
        if (!rejected) throw new Error("Timestamp queries without the feature must throw TypeError");
    } finally {
        plain.destroy();
    }
    if (!adapter.features.has("timestamp-query")) return "unsupported";
    const device = await adapter.requestDevice({ requiredFeatures: ["timestamp-query"] });
    const queries = device.createQuerySet({ type: "timestamp", count: 4 });
    const resolved = device.createBuffer({ size: 32, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
    const mapped = device.createBuffer({ size: 32, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const target = device.createTexture({ size: [512, 512], format: "rgba8unorm", usage: GPUTextureUsage.RENDER_ATTACHMENT });
    try {
        if (queries.type !== "timestamp" || queries.count !== 4) throw new Error("Query set descriptor was lost");
        const encoder = device.createCommandEncoder();
        encoder.beginComputePass({ timestampWrites: { querySet: queries, beginningOfPassWriteIndex: 0 } }).end();
        encoder.beginRenderPass({
            colorAttachments: [{ view: target.createView(), clearValue: [1, 0, 0, 1], loadOp: "clear", storeOp: "store" }],
            timestampWrites: { querySet: queries, beginningOfPassWriteIndex: 1, endOfPassWriteIndex: 2 },
        }).end();
        encoder.beginComputePass({ timestampWrites: { querySet: queries, endOfPassWriteIndex: 3 } }).end();
        device.queue.submit([encoder.finish()]);
        const reader = device.createCommandEncoder();
        reader.resolveQuerySet(queries, 0, 4, resolved, 0);
        reader.copyBufferToBuffer(resolved, 0, mapped, 0, 32);
        device.queue.submit([reader.finish()]);
        await mapped.mapAsync(GPUMapMode.READ);
        const [begin, renderBegin, renderEnd, end] = new BigUint64Array(mapped.getMappedRange());
        mapped.unmap();
        if (!(end > begin && renderEnd >= renderBegin && renderBegin > 0n)) {
            throw new Error("Invalid GPU timestamps: " + [begin, renderBegin, renderEnd, end].join(","));
        }
        device.pushErrorScope("validation");
        const invalid = device.createCommandEncoder();
        invalid.resolveQuerySet(queries, 0, 1, resolved, 8);
        invalid.finish();
        if (!(await device.popErrorScope())) throw new Error("Misaligned query resolve passed validation");
        return Number(end - begin);
    } finally {
        queries.destroy();
        resolved.destroy();
        mapped.destroy();
        target.destroy();
        device.destroy();
    }
}

(async function () {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw new Error("No WebGPU adapter");
    const elapsed = await checkGpuTimestamps(adapter);
    console.log(elapsed === "unsupported"
        ? "timestamp-query:unsupported (feature control passed)"
        : "timestamp-query:passed elapsedNs=" + elapsed);
    TestUtils.exit(0);
})().catch(error => {
    console.error("timestamp-query:failed " + (error.stack || error));
    TestUtils.exit(1);
});
