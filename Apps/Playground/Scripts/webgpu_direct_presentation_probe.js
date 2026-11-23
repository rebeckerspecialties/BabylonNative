// Diagnostic only. Run with the native direct-presentation experiment enabled.
globalThis.__babylonPlaygroundSelfManagedScript = true;
(async function () {
    var adapter = await navigator.gpu.requestAdapter();
    var device = await adapter.requestDevice();
    device.addEventListener("uncapturederror", function (event) {
        console.error("direct-probe:uncaptured:" + event.error.message);
        TestUtils.exit(1);
    });
    var width = Math.round(window.innerWidth * window.devicePixelRatio);
    var height = Math.round(window.innerHeight * window.devicePixelRatio);
    if (!(width > 0 && height > 0)) throw new Error("Direct probe needs the current display dimensions.");
    var format = navigator.gpu.getPreferredCanvasFormat();
    var contexts = [navigator.gpu._createCanvasContext(), navigator.gpu._createCanvasContext()];
    var usage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC;
    var readback = device.createBuffer({ size: 512, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    var previous;
    var previousWidth;
    device.pushErrorScope("validation");
    for (var frame = 0; frame < 64; frame++) {
        if (frame % 8 === 0) {
            // Leave complete host frames unused: reservations must be discarded,
            // not presented as blank images or retained without bound.
            await new Promise(function (resolve) { requestAnimationFrame(resolve); });
            await new Promise(function (resolve) { requestAnimationFrame(resolve); });
        }
        var targetWidth = frame >= 24 && frame < 40 ? Math.max(1, Math.floor(width / 2)) : width;
        await new Promise(function (resolve, reject) {
            requestAnimationFrame(function () {
                try {
                    if (targetWidth !== previousWidth) {
                        contexts.forEach(function (context) {
                            context.unconfigure();
                            context.configure({ device: device, format: format, width: targetWidth, height: height, usage: usage });
                        });
                        previous = undefined;
                        previousWidth = targetWidth;
                    }
                    var first = contexts[0].getCurrentTexture();
                    if (first !== contexts[0].getCurrentTexture()) throw new Error("Current texture identity changed within a frame.");
                    if (targetWidth === width && first === previous) throw new Error("Direct canvas texture did not expire between frames.");
                    Promise.resolve().then(function () {
                        if (contexts[0].getCurrentTexture() !== first) throw new Error("A Promise continuation lost its animation-frame texture.");
                        var second = contexts[1].getCurrentTexture();
                        if (first === second) throw new Error("Two canvases shared a current texture.");
                        var encoder = device.createCommandEncoder({ label: "direct-presentation-lifetime-probe" });
                        [first, second].forEach(function (texture, index) {
                            var pass = encoder.beginRenderPass({ colorAttachments: [{ view: texture.createView(), loadOp: "clear", storeOp: "store",
                                clearValue: index === 0 ? [1, 0, 0, 1] : [0, 1, 0, 1] }] });
                            pass.end();
                            encoder.copyTextureToBuffer({ texture: texture }, { buffer: readback, offset: index * 256, bytesPerRow: 256 }, [1, 1]);
                        });
                        if (contexts[0].getCurrentTexture() !== first) throw new Error("Switching canvases lost the direct texture.");
                        device.queue.submit([encoder.finish()]);
                        previous = first;
                        resolve();
                    }).catch(reject);
                } catch (error) { reject(error); }
            });
        });
        await readback.mapAsync(GPUMapMode.READ);
        var halfFloat = format === "rgba16float";
        var pixels = halfFloat ? new Uint16Array(readback.getMappedRange()) : new Uint8Array(readback.getMappedRange());
        var one = halfFloat ? 0x3c00 : 255;
        var stride = 256 / pixels.BYTES_PER_ELEMENT;
        var red = format.indexOf("bgra") === 0 ? [0, 0, one, one] : [one, 0, 0, one];
        [red, [0, one, 0, one]].forEach(function (expected, index) {
            expected.forEach(function (value, channel) {
                if (pixels[index * stride + channel] !== value) throw new Error("Canvas readback mismatch at frame " + frame + ", canvas " + index + ", channel " + channel);
            });
        });
        readback.unmap();
    }
    var error = await device.popErrorScope();
    if (error) throw new Error(error.message);
    contexts.forEach(function (context) { context.unconfigure(); });
    readback.destroy();
    console.log("direct-probe:passed:frames=64:canvases=2:resize-fallback=true:microtasks=true:idle-frames=true:size=" + width + "x" + height);
    TestUtils.exit(0);
})().catch(function (error) {
    console.error("direct-probe:error:" + String(error) + "\n" + (error.stack || ""));
    TestUtils.exit(1);
});
