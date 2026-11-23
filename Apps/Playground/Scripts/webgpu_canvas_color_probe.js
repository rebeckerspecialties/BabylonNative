// Diagnostic only: numeric canvas colors must not gain a second transfer curve.
globalThis.__babylonPlaygroundSelfManagedScript = true;
(async function () {
    var device = await (await navigator.gpu.requestAdapter()).requestDevice();
    var format = navigator.gpu.getPreferredCanvasFormat();
    var context = navigator.gpu._createCanvasContext();
    context.configure({ device: device, format: format, width: Math.round(window.innerWidth * window.devicePixelRatio),
        height: Math.round(window.innerHeight * window.devicePixelRatio), toneMapping: { mode: "extended" } });
    for (var value of [0, 0.25, 0.5, 0.75, 1]) {
        await new Promise(function (resolve, reject) {
            requestAnimationFrame(function () {
                try {
                    var encoder = device.createCommandEncoder();
                    var pass = encoder.beginRenderPass({ colorAttachments: [{ view: context.getCurrentTexture().createView({ format: format }),
                        clearValue: [value, value, value, 1], loadOp: "clear", storeOp: "store" }] });
                    pass.end();
                    device.queue.submit([encoder.finish()]);
                    TestUtils.getFrameBufferData(function (pixels, width, height) {
                        try {
                            if (pixels.length !== width * height * 4) throw new Error("Invalid capture dimensions");
                            var expected = Math.round(value * 255);
                            for (var channel = 0; channel < 3; channel++) {
                                if (Math.abs(pixels[channel] - expected) > 1) {
                                    throw new Error("Encoded canvas gray " + value + " became " + pixels[channel] + ", expected " + expected);
                                }
                            }
                            console.log("canvas-color-probe:gray=" + value + ":byte=" + pixels[0] + ":format=" + format);
                            resolve();
                        } catch (error) { reject(error); }
                    });
                } catch (error) { reject(error); }
            });
        });
    }
    context.unconfigure();
    console.log("canvas-color-probe:passed");
    TestUtils.exit(0);
})().catch(function (error) {
    console.error("canvas-color-probe:error:" + error.stack);
    TestUtils.exit(1);
});
