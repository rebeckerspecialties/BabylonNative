// Pass the contents of Core/GraphicsWgpu/Rust/src/xr_eye_resolve.wgsl, not a shader copy.
async function checkXREyeResolve(device, shader) {
    const module = device.createShaderModule({ code: shader });
    const pipeline = device.createRenderPipeline({
        layout: "auto",
        vertex: { module, entryPoint: "vs" },
        fragment: { module, entryPoint: "fs", targets: [{ format: "rgba16float" }] },
    });
    const half = (u) => {
        const exponent = (u >> 10) & 31, mantissa = u & 1023;
        if (u >> 15 || exponent === 31) throw Error("Unexpected non-finite or negative color: " + u);
        return exponent ? (1 + mantissa / 1024) * 2 ** (exponent - 15) : mantissa * 2 ** -24;
    };
    for (const [width, height, outWidth, outHeight] of [[9, 7, 5, 4], [5, 3, 13, 7], [1, 5, 3, 7], [7, 1, 3, 1], [9, 7, 9, 7]]) {
        const pixels = new Uint8Array(width * height * 4);
        const levels = [0, 10, 11, 12, 25, 64, 106, 164, 255];
        for (let y = 0; y < height; ++y) {
            for (let x = 0; x < width; ++x) {
                pixels.set([levels[(x + y) % levels.length], ((x ^ y) & 1) * 255, y * 29, (17 * x + 53 * y) % 256], (y * width + x) * 4);
            }
        }
        const source = device.createTexture({ size: [width, height], format: "rgba8unorm", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        const target = device.createTexture({ size: [outWidth, outHeight], format: "rgba16float", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
        const pitch = 256;
        const readback = device.createBuffer({ size: pitch * outHeight, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        try {
            device.queue.writeTexture({ texture: source }, pixels, { bytesPerRow: width * 4 }, [width, height]);
            const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: source.createView() }] });
            const encoder = device.createCommandEncoder();
            const pass = encoder.beginRenderPass({ colorAttachments: [{ view: target.createView(), loadOp: "clear", storeOp: "store", clearValue: {r: 1, g: 0, b: 1, a: 1} }] });
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, group);
            pass.draw(3);
            pass.end();
            encoder.copyTextureToBuffer({ texture: target }, { buffer: readback, bytesPerRow: pitch }, [outWidth, outHeight]);
            device.queue.submit([encoder.finish()]);
            await readback.mapAsync(GPUMapMode.READ);
            const actual = new Uint16Array(readback.getMappedRange());
            const sample = (x, y, c) => {
                const i = Math.min(height - 1, Math.max(0, y)) * width + Math.min(width - 1, Math.max(0, x));
                const s = pixels[4 * i + c] / 255;
                return c === 3 ? s : s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
            };
            for (let y = 0; y < outHeight; ++y) {
                for (let x = 0; x < outWidth; ++x) {
                    const px = (x + 0.5) * width / outWidth - 0.5, py = (y + 0.5) * height / outHeight - 0.5;
                    const x0 = Math.floor(px), y0 = Math.floor(py), fx = px - x0, fy = py - y0;
                    for (let c = 0; c < 4; ++c) {
                        const a = sample(x0, y0, c) * (1 - fx) + sample(x0 + 1, y0, c) * fx;
                        const b = sample(x0, y0 + 1, c) * (1 - fx) + sample(x0 + 1, y0 + 1, c) * fx;
                        const expected = a * (1 - fy) + b * fy;
                        const got = half(actual[y * pitch / 2 + x * 4 + c]);
                        if (Math.abs(got - expected) > Math.max(0.000003, expected * 0.0015)) {
                            throw Error(`Resolve ${width}x${height}->${outWidth}x${outHeight} pixel(${x},${y}) channel ${c}: expected ${expected}, got ${got}`);
                        }
                    }
                }
            }
            readback.unmap();
            console.info("XR resolve verified " + [width, height, outWidth, outHeight].join("x"));
        } finally {
            readback.destroy();
            target.destroy();
            source.destroy();
        }
    }
}
