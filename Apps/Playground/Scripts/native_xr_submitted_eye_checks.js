// Opt-in stereo diagnostic. Sample the actual acquired images after Babylon's
// endFrame flush; map only the small result buffer, never the swapchain.
globalThis.checkSubmittedXREyes = function (scene) {
    const engine = scene.getEngine();
    const device = engine._device;
    const shader = device.createShaderModule({ code: `
        @group(0) @binding(0) var left: texture_2d<f32>;
        @group(0) @binding(1) var right: texture_2d<f32>;
        @group(0) @binding(2) var<storage, read_write> samples: array<vec2<u32>, 16>;
        @compute @workgroup_size(4, 4)
        fn main(@builtin(local_invocation_id) id: vec3<u32>) {
            let uv = (vec2<f32>(id.xy) + 0.5) / 4.0;
            let l = textureLoad(left, vec2<i32>(uv * vec2<f32>(textureDimensions(left))), 0);
            let r = textureLoad(right, vec2<i32>(uv * vec2<f32>(textureDimensions(right))), 0);
            samples[id.y * 4u + id.x] = vec2<u32>(pack4x8unorm(l), pack4x8unorm(r));
        }` });
    const pipeline = device.createComputePipeline({ layout: "auto", compute: { module: shader, entryPoint: "main" } });
    const eyes = new Map();
    const cameraObserver = scene.onAfterCameraRenderObservable.add(camera => {
        const eye = camera.isLeftCamera ? "left" : camera.isRightCamera ? "right" : null;
        if (eye) {
            eyes.set(eye, camera.outputRenderTarget?.getInternalTexture()?._hardwareTexture?.underlyingResource);
        }
    });
    let sampledFrames = 0;
    const frameObserver = engine.onEndFrameObservable.add(() => {
        const left = eyes.get("left");
        const right = eyes.get("right");
        eyes.clear();
        if (!left || !right) {
            return;
        }
        if (![1, 8, 24].includes(++sampledFrames)) {
            return;
        }
        const frame = sampledFrames;
        if (frame === 24) {
            scene.onAfterCameraRenderObservable.remove(cameraObserver);
            engine.onEndFrameObservable.remove(frameObserver);
        }
        const usage = GPUBufferUsage;
        const size = 16 * 2 * 4;
        const output = device.createBuffer({ size, usage: usage.STORAGE | usage.COPY_SRC });
        const readback = device.createBuffer({ size, usage: usage.COPY_DST | usage.MAP_READ });
        // All commands touching the eye images must be submitted synchronously
        // in this callback. Only the result may outlive the acquired frame.
        const read = async () => {
            try {
                const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
                    { binding: 0, resource: left.createView() }, { binding: 1, resource: right.createView() },
                    { binding: 2, resource: { buffer: output } },
                ] });
                const encoder = device.createCommandEncoder();
                const pass = encoder.beginComputePass();
                pass.setPipeline(pipeline);
                pass.setBindGroup(0, group);
                pass.dispatchWorkgroups(1);
                pass.end();
                encoder.copyBufferToBuffer(output, 0, readback, 0, size);
                device.queue.submit([encoder.finish()]);
                await readback.mapAsync(GPUMapMode.READ);
                const words = new Uint32Array(readback.getMappedRange());
                const summarize = (offset) => {
                    const pixels = Array.from({ length: 16 }, (_, i) => words[i * 2 + offset]);
                    return { nonblack: pixels.filter(pixel => pixel & 0xffffff).length,
                        opaque: pixels.filter(pixel => pixel >>> 24 === 255).length, distinct: new Set(pixels).size };
                };
                console.info("Submitted XR eye samples", JSON.stringify({ frame, shared: left === right, left: summarize(0), right: summarize(1) }));
            } finally {
                if (readback.mapState === "mapped") { readback.unmap(); }
                readback.destroy();
                output.destroy();
            }
        };
        read().catch(error => console.error("Submitted XR eye check failed", String(error)));
    });
};
