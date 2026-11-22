// Device-only fixture for an app using Babylon's WebGPU XR layer provider.
// No compositor access or synthetic head pose is required.
globalThis.checkXRAttachmentPixels = async function (scene, Provider) {
    const engine = scene.getEngine();
    const device = engine._device;
    const provider = new Provider({ scene }, {}, { isMultiview: false });
    const images = [];
    let readback;
    try {
        const size = { width: 32, height: 32 };
        const colors = [[1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0]];
        for (const color of colors) {
            images.push({
                colorTexture: device.createTexture({ size, format: "rgba8unorm", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC }),
                depthStencilTexture: device.createTexture({ size, format: "depth24plus-stencil8", usage: GPUTextureUsage.RENDER_ATTACHMENT }),
                textureWidth: size.width, textureHeight: size.height, color,
            });
        }
        const count = () => navigator.gpu._backendStats().textureCreateCount;
        const before = count();
        const targets = images.map((image, i) => provider._getRenderTargetForSubImage(image, i % 2 ? "right" : "left"));
        const extra = count() - before;
        if (extra !== 0) { throw new Error("Unexpected replacement eye allocations: " + extra); }
        const encoder = device.createCommandEncoder();
        for (const [i, image] of images.entries()) {
            const target = provider._getRenderTargetForSubImage(image, i % 2 ? "right" : "left");
            if (target !== targets[i]) { throw new Error("Swapchain target was not reused for image " + i); }
            const wrapper = target.renderTarget;
            const [r, g, b] = image.color;
            encoder.beginRenderPass({
                colorAttachments: [{ view: wrapper.texture._hardwareTexture.underlyingResource.createView(), loadOp: "clear", storeOp: "store", clearValue: { r, g, b, a: 1 } }],
                depthStencilAttachment: { view: wrapper.depthStencilTexture._hardwareTexture.underlyingResource.createView(), depthLoadOp: "clear", depthClearValue: 1,
                    depthStoreOp: "discard", stencilLoadOp: "clear", stencilClearValue: 0, stencilStoreOp: "discard" },
            }).end();
        }
        device.queue.submit([encoder.finish()]);
        provider.dispose();
        engine._textureHelper.destroyDeferredTextures();
        readback = device.createBuffer({ size: 1024, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
        const copy = device.createCommandEncoder();
        for (const [i, image] of images.entries()) {
            copy.copyTextureToBuffer({ texture: image.colorTexture }, { buffer: readback, offset: i * 256, bytesPerRow: 256 }, [1, 1]);
        }
        device.queue.submit([copy.finish()]);
        await readback.mapAsync(GPUMapMode.READ);
        const bytes = new Uint8Array(readback.getMappedRange());
        for (const [i, image] of images.entries()) {
            const expected = [...image.color, 1].map(channel => channel * 255);
            if (expected.some((value, channel) => bytes[i * 256 + channel] !== value)) {
                throw new Error("Wrong pixel after release for image " + i + ": " + bytes.slice(i * 256, i * 256 + 4));
            }
        }
        console.log("XR attachment pixels PASS: four rotating color/depth targets, zero replacement allocations, four distinct pixels after disposal");
    } finally {
        if (readback) {
            if (readback.mapState === "mapped") { readback.unmap(); }
            readback.destroy();
        }
        for (const image of images) {
            image.colorTexture.destroy();
            image.depthStencilTexture.destroy();
        }
    }
};
