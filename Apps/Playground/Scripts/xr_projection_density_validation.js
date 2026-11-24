// Loaded only by Android qualification intents. Probe the application's own layer,
// not a replacement with different dimensions, texture layout or MSAA.
globalThis.validateProjectionDensity = async function (session, device, space, layer, sampleCount = 1) {
    const binding = new XRGPUBinding(session, device);
    const previous = layer.fixedFoveation;
    if (typeof previous !== "number") throw Error("Active projection has no fixed foveation");
    // Startup can deliver frames before tracking. Cancel the callback before
    // releasing its GPU resources if the session never becomes visible.
    function trackedFrame(draw) {
        return new Promise((resolve, reject) => {
            let id,
                expired = false;
            const timer = setTimeout(() => {
                expired = true;
                session.cancelAnimationFrame?.(id);
                reject(Error("Timed out waiting for a tracked projection frame"));
            }, 8000);
            function frameCallback(_, frame) {
                if (expired) return;
                try {
                    const pose = frame.getViewerPose(space);
                    if (!pose) {
                        id = session.requestAnimationFrame(frameCallback);
                        return;
                    }
                    draw(pose);
                    resolve();
                } catch (error) {
                    reject(error);
                }
                clearTimeout(timer);
            }
            id = session.requestAnimationFrame(frameCallback);
        });
    }
    const read = device.createBuffer({ size: 16384, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const module = device.createShaderModule({
        code: `
        @vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {
            return vec4f(vec2f(f32((i<<1u)&2u),f32(i&2u))*2.-1.,0.,1.);
        }
        @fragment fn fs(@builtin(position) p:vec4f)->@location(0) vec4f {
            return vec4f(vec3f(f32((u32(p.x)^u32(p.y))&1u)),1.);
        }`,
    });
    const counts = [],
        targets = [];
    let pipeline;
    try {
        for (const level of [0, 1, 0]) {
            layer.fixedFoveation = level;
            if (layer.fixedFoveation !== level) throw Error("Foveation setting not retained");
            for (let k = 0; k < 5; ++k) {
                await trackedFrame((pose) => {
                    if (
                        !session.renderState.layers.includes(layer) ||
                        pose.views.length !== 2 ||
                        !pose.views.some((view) => view.eye === "left") ||
                        !pose.views.some((view) => view.eye === "right")
                    )
                        throw Error("Expected active tracked stereo projection");
                    const enc = device.createCommandEncoder();
                    for (const view of pose.views) {
                        const eye = view.eye === "left" ? 0 : 1;
                        const sub = binding.getViewSubImage(layer, view),
                            tex = sub.colorTexture;
                        const desc = sub.getViewDescriptor(),
                            dst = tex.createView(desc),
                            vp = sub.viewport;
                        pipeline ??= device.createRenderPipeline({
                            layout: "auto",
                            vertex: { module, entryPoint: "vs" },
                            fragment: { module, entryPoint: "fs", targets: [{ format: tex.format }] },
                            multisample: { count: sampleCount },
                        });
                        if (sampleCount > 1 && !targets[eye])
                            targets[eye] = device.createTexture({
                                size: [tex.width, tex.height],
                                format: tex.format,
                                sampleCount,
                                usage: GPUTextureUsage.RENDER_ATTACHMENT,
                            });
                        const pass = enc.beginRenderPass({
                            colorAttachments: [
                                {
                                    view: sampleCount === 1 ? dst : targets[eye].createView(),
                                    ...(sampleCount === 1 ? {} : { resolveTarget: dst }),
                                    loadOp: "clear",
                                    storeOp: sampleCount === 1 ? "store" : "discard",
                                    clearValue: [0, 0, 0, 1],
                                },
                            ],
                        });
                        pass.setViewport(vp.x, vp.y, vp.width, vp.height, 0, 1);
                        pass.setPipeline(pipeline);
                        pass.draw(3);
                        pass.end();
                        if (k === 4)
                            for (const [region, f] of [0.5, 0.93].entries()) {
                                enc.copyTextureToBuffer(
                                    {
                                        texture: tex,
                                        origin: [
                                            vp.x + Math.floor(vp.width * f) - 8,
                                            vp.y + Math.floor(vp.height * f) - 8,
                                            desc.baseArrayLayer ?? 0,
                                        ],
                                    },
                                    { buffer: read, offset: (eye * 2 + region) * 4096, bytesPerRow: 256 },
                                    [16, 16, 1],
                                );
                            }
                    }
                    device.queue.submit([enc.finish()]);
                });
            }
            await read.mapAsync(GPUMapMode.READ);
            try {
                const bytes = new Uint8Array(read.getMappedRange());
                counts.push(
                    [0, 1, 2, 3].map((region) => {
                        let n = 0;
                        for (let y = 0; y < 16; ++y)
                            for (let x = 1; x < 16; ++x) {
                                const i = region * 4096 + y * 256 + x * 4;
                                n += Math.abs(bytes[i] - bytes[i - 4]) > 127 ? 1 : 0;
                            }
                        return n;
                    }),
                );
            } finally {
                read.unmap();
            }
        }
        for (const i of [0, 2]) {
            if (
                counts[0][i] !== 240 ||
                counts[0][i + 1] !== 240 ||
                counts[1][i] !== 240 ||
                counts[1][i + 1] >= 200 ||
                counts[2][i] !== 240 ||
                counts[2][i + 1] !== 240
            )
                throw Error("Unexpected projection density: " + JSON.stringify(counts));
        }
        return { sampleCount, counts };
    } finally {
        layer.fixedFoveation = previous;
        for (const tex of targets) tex.destroy();
        read.destroy();
    }
};
