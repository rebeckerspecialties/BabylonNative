// In a browser call runProjectionRotation() from a user gesture. Native needs no activation.
async function runProjectionRotation({ layersFeature = true, replacementScale = 1 } = {}) {
    const adapter = await navigator.gpu.requestAdapter({ xrCompatible: true });
    const device = await adapter.requestDevice();
    const session = await navigator.xr.requestSession("immersive-vr", { requiredFeatures: layersFeature ? ["webgpu", "layers"] : ["webgpu"] });
    const failures = new Set();
    const check = (ok, message) => { if (!ok) failures.add(message); };
    try {
        const space = await session.requestReferenceSpace("local");
        const binding = new XRGPUBinding(session, device);
        const otherDevice = await adapter.requestDevice();
        const otherBinding = new XRGPUBinding(session, otherDevice);
        const constructors = ["createQuadLayer", "createCylinderLayer", "createEquirectLayer", "createCubeLayer"];
        console.info("XR composition entry points", JSON.stringify(constructors.filter(name => typeof binding[name] === "function")));
        const init = { colorFormat: binding.getPreferredColorFormat(), depthStencilFormat: "depth24plus",
            textureUsage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC };
        const layers = [binding.createProjectionLayer(init), binding.createProjectionLayer({ ...init, scaleFactor: replacementScale })];
        const fixed = layers[0].fixedFoveation;
        check(fixed === null || typeof fixed === "number", "fixedFoveation must report support explicitly");
        if (fixed === null) {
            layers[0].fixedFoveation = 0.5;
            check(layers[0].fixedFoveation === null, "unsupported foveation reported as active");
        }
        let index = 0, frames = 0, checks = 0;
        const extents = new Map();
        const samples = 9;
        const read = device.createBuffer({ size: 512 * samples, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const expected = [];
        try {
            device.pushErrorScope("validation");
            const initial = [layers[0]];
            check(session.updateRenderState({ layers: initial }) === undefined, "updateRenderState must return undefined");
            initial[0] = layers[1];
            let retainedView, retainedFrame;
            await new Promise((resolve, reject) => {
                const tick = (time, frame) => {
                    try {
                        const pose = frame.getViewerPose(space);
                        if (!pose) { session.requestAnimationFrame(tick); return; }
                        if (!frames) {
                            const translated = space.getOffsetReferenceSpace(new XRRigidTransform({ x: 0.3, y: -0.2, z: 0.1 }));
                            const forward = frame.getPose(translated, space);
                            const reverse = frame.getPose(space, translated);
                            check(forward !== reverse && Math.abs(forward.transform.position.x - 0.3) < 0.0001 &&
                                Math.abs(reverse.transform.position.y - 0.2) < 0.0001, "relative poses were reversed or aliased");
                            let invalid;
                            try { frame.getPose({}, space); } catch (e) { invalid = e; }
                            check(invalid instanceof TypeError, "non-space accepted by getPose");
                        }
                        for (const source of frame.session.inputSources) {
                            if (!source.gripSpace || !frame.getPose(source.gripSpace, space)) continue;
                            const identity = frame.getPose(source.gripSpace, source.gripSpace);
                            check(identity && identity.transform.matrix.every((v, i) => Math.abs(v - (i % 5 === 0 ? 1 : 0)) < 0.0002),
                                "tracked grip expressed relative to itself was not identity");
                        }
                        retainedFrame = frame;
                        const layer = layers[index];
                        const snapshot = session.renderState;
                        check(snapshot.layers[0] === layer, "new layer inactive on next frame");
                        if (pose.views.length !== 2) throw Error("Expected stereo views");
                        const sub = pose.views.map(view => binding.getViewSubImage(layer, view));
                        const extent = sub[0].colorTexture.width + "x" + sub[0].colorTexture.height;
                        if (extents.has(index)) check(extents.get(index) === extent, "projection dimensions changed on reacquisition");
                        extents.set(index, extent);
                        const cmd = device.createCommandEncoder();
                        const sample = frames % 10 === 0;
                        if (frames === 1) {
                            let error;
                            try { binding.getViewSubImage(layer, retainedView); } catch (e) { error = e; }
                            check(error?.name === "InvalidStateError", "prior-frame view acquired a new subimage");
                        }
                        if (!frames) {
                            retainedView = pose.views[0];
                            console.info("XR application eye extent", JSON.stringify(sub.map(s =>
                                [s.colorTexture.width, s.colorTexture.height, s.viewport.width, s.viewport.height, s.colorTexture.format])));
                            let error;
                            try { binding.getSubImage(layer, frame, "left"); } catch (e) { error = e; }
                            check(layersFeature ? error instanceof TypeError : error?.name === "NotSupportedError",
                                "getSubImage accepted a projection layer or raised the wrong error");
                            error = null;
                            try { binding.getViewSubImage(layer, { eye: "left" }); } catch (e) { error = e; }
                            check(error instanceof TypeError, "getViewSubImage accepted a non-XRView or raised the wrong error");
                            error = null;
                            try { otherBinding.getViewSubImage(layer, pose.views[0]); } catch (e) { error = e; }
                            check(error?.name === "InvalidStateError", "another logical device accessed a layer");
                        }
                        for (let eye = 0; eye < 2; eye++) {
                            const s = sub[eye], desc = s.getViewDescriptor();
                            check(s.colorTexture === sub[0].colorTexture && desc.baseArrayLayer === eye, "wrong stereo slice");
                            // Only change blue slowly between captures. Opposite eye/swapchain contents must not leak.
                            const blue = (Math.floor(frames / 10) + 1) / 16;
                            const color = [eye ? 0 : 0.25, eye ? 0.25 : 0, blue, 1];
                            cmd.beginRenderPass({ colorAttachments: [{ view: s.colorTexture.createView(desc),
                                loadOp: "clear", storeOp: "store", clearValue: color }] }).end();
                            if (sample) {
                                cmd.copyTextureToBuffer({ texture: s.colorTexture, origin: [0, 0, desc.baseArrayLayer] },
                                    { buffer: read, offset: checks * 512 + eye * 256, bytesPerRow: 256 }, [1, 1, 1]);
                                expected.push({ color, format: s.colorTexture.format, offset: checks * 512 + eye * 256 });
                            }
                        }
                        device.queue.submit([cmd.finish()]);
                        if (sample) checks++;
                        if (++frames === 31 || frames === 61) {
                            index ^= 1;
                            session.updateRenderState({ layers: [layers[index]] });
                            check(snapshot.layers[0] === layer && session.renderState.layers[0] === layer, "layer changed mid-frame");
                            try { binding.getViewSubImage(layer, pose.views[1]); }
                            catch { failures.add("outgoing right eye became inaccessible mid-frame"); }
                        }
                        if (frames === 90) resolve(); else session.requestAnimationFrame(tick);
                    } catch (e) { reject(e); }
                };
                session.requestAnimationFrame(tick);
            });
            await new Promise(resolve => setTimeout(resolve, 0));
            let error;
            try { binding.getViewSubImage(layers[index], retainedView); } catch (e) { error = e; }
            check(error?.name === "InvalidStateError", "subimage escaped its animation callback");
            let poseError;
            try { retainedFrame.getPose(space, space); } catch (e) { poseError = e; }
            check(poseError?.name === "InvalidStateError", "getPose escaped its active frame");
            await read.mapAsync(GPUMapMode.READ);
            const validationError = await device.popErrorScope();
            if (validationError) failures.add(validationError.message);
            const pixels = new Uint8Array(read.getMappedRange());
            for (const { color, format, offset } of expected) {
                if (!["bgra8unorm", "rgba8unorm", "bgra8unorm-srgb", "rgba8unorm-srgb"].includes(format)) throw Error("Unsupported readback format: " + format);
                const srgb = x => x <= 0.0031308 ? 12.92 * x : 1.055 * x ** (1 / 2.4) - 0.055;
                const rgb = color.slice(0, 3).map(x => Math.round(255 * (format.endsWith("-srgb") ? srgb(x) : x)));
                if (format.startsWith("bgra")) rgb.reverse();
                const actual = pixels.slice(offset, offset + 4);
                check([...rgb, 255].every((n, c) => Math.abs(n - actual[c]) <= 1),
                    "eye pixel " + offset + ": got " + actual.join(",") + " expected " + [...rgb, 255].join(","));
            }
            console.info("XR projection rotation result", JSON.stringify({ layersFeature, frames, extents: [...extents.values()], eyePixels: expected.length, failures: [...failures] }));
            if (failures.size) throw Error([...failures].join("; "));
        } finally { if (read.mapState === "mapped") read.unmap(); read.destroy(); otherDevice.destroy(); }
    } finally { await session.end(); device.destroy(); }
}
if (typeof _native !== "undefined") {
    runProjectionRotation().then(() => runProjectionRotation({ layersFeature: false, replacementScale: 0.75 }))
        .then(() => console.info("XR projection sessions complete"))
        .catch(e => console.error("XR projection rotation FAIL", String(e), e.stack));
}
