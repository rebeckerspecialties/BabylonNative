// Dedicated immersive test: acquire each layer on several consecutive XR frames.
async function checkXRProjectionScale() {
    const adapter = await navigator.gpu.requestAdapter({ xrCompatible: true });
    const device = await adapter.requestDevice();
    const session = await navigator.xr.requestSession("immersive-vr", { requiredFeatures: ["webgpu", "local-floor"] });
    try {
        const binding = new XRGPUBinding(session, device);
        const space = await session.requestReferenceSpace("local-floor");
        let reference;
        for (const scale of [1, 0.5, 1.25, 1]) {
            const layer = binding.createProjectionLayer({
                colorFormat: binding.getPreferredColorFormat(), depthStencilFormat: "depth24plus-stencil8", scaleFactor: scale,
            });
            session.updateRenderState({ layers: [layer] });
            for (let n = 0; n < 5; ++n) {
                await new Promise((resolve, reject) => session.requestAnimationFrame((_time, frame) => {
                    try {
                        const pose = frame.getViewerPose(space);
                        if (frame.session !== session) throw Error("Frame belongs to the wrong session");
                        if (pose?.views.length !== 2) throw Error("Expected two tracked views");
                        const encoder = device.createCommandEncoder();
                        for (const view of pose.views) {
                            const sub = binding.getViewSubImage(layer, view);
                            const color = sub.colorTexture, depth = sub.depthStencilTexture;
                            if (!reference) reference = [color.width, color.height];
                            const width = Math.floor(reference[0] * scale), height = Math.floor(reference[1] * scale);
                            if (color.width !== width || depth.width !== width || color.height !== height || depth.height !== height ||
                                sub.viewport.width !== width || sub.viewport.height !== height || layer.textureWidth !== width || layer.textureHeight !== height) {
                                throw Error("Wrong color/depth/viewport/layer extent at scale " + scale + ": " + color.width + "x" + color.height);
                            }
                            const colorValue = view.eye === "left" ? [0.25, 0, 0, 1] : [0, 0.5, 0, 1];
                            const pass = encoder.beginRenderPass({ colorAttachments: [{
                                view: color.createView(), clearValue: colorValue, loadOp: "clear", storeOp: "store",
                            }] });
                            pass.end();
                        }
                        device.queue.submit([encoder.finish()]);
                        resolve();
                    } catch (e) { reject(e); }
                }));
            }
            console.info("XR scale verified", scale, reference[0], reference[1]);
        }
    } finally {
        await session.end();
        device.destroy();
    }
}
