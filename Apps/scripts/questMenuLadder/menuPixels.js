// Read sparse stereo pixels only in a separate visual run, after submission
// and before the runtime releases the eye images. No driver or per-draw probes.
async function compareMenuEyes(scene) {
    const engine = scene.getEngine(),
        device = engine._device;
    const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const shader = device.createShaderModule({
        code: `
        @group(0) @binding(0) var l: texture_2d<f32>;
        @group(0) @binding(1) var r: texture_2d<f32>;
        @group(0) @binding(2) var<storage,read_write> colors: array<vec2u>;
        @compute @workgroup_size(8,8) fn main(@builtin(global_invocation_id) id:vec3u) {
            let pos=vec2i((vec2f(id.xy)+0.5)*vec2f(textureDimensions(l))/vec2f(128,144));
            colors[id.y*128u+id.x]=vec2u(pack4x8unorm(textureLoad(l,pos,0)),pack4x8unorm(textureLoad(r,pos,0)));
        }`,
    });
    const pipeline = device.createComputePipeline({ layout: "auto", compute: { module: shader, entryPoint: "main" } });
    const size = 128 * 144 * 8;
    const data = device.createBuffer({ size, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
    const read = device.createBuffer({ size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const clear = scene._clearFrameBuffer;
    let skipped = 0,
        skip = false;
    scene._clearFrameBuffer = function (c) {
        if (skip && isUnusedParentClear(scene, c)) {
            ++skipped;
            return;
        }
        return clear.call(this, c);
    };
    async function pixels(omit) {
        skip = omit;
        await sleep(500);
        await new Promise((resolve, reject) =>
            engine.onEndFrameObservable.addOnce(() => {
                try {
                    const textures = scene.activeCamera.rigCameras.map(
                        (c) => c.outputRenderTarget.getInternalTexture()._hardwareTexture.underlyingResource,
                    );
                    if (textures.length !== 2 || textures[0] === textures[1]) throw Error("Expected separate eye textures");
                    const enc = device.createCommandEncoder(),
                        pass = enc.beginComputePass();
                    pass.setPipeline(pipeline);
                    pass.setBindGroup(
                        0,
                        device.createBindGroup({
                            layout: pipeline.getBindGroupLayout(0),
                            entries: [
                                { binding: 0, resource: textures[0].createView() },
                                { binding: 1, resource: textures[1].createView() },
                                { binding: 2, resource: { buffer: data } },
                            ],
                        }),
                    );
                    pass.dispatchWorkgroups(16, 18);
                    pass.end();
                    enc.copyBufferToBuffer(data, 0, read, 0, size);
                    device.queue.submit([enc.finish()]);
                    resolve();
                } catch (e) {
                    reject(e);
                }
            }),
        );
        await read.mapAsync(GPUMapMode.READ);
        const copy = new Uint32Array(read.getMappedRange()).slice();
        read.unmap();
        return copy;
    }
    function diff(a, b) {
        return [0, 1].map((eye) => {
            let total = 0,
                same = 0,
                significant = 0,
                unique = new Set();
            for (let i = eye; i < a.length; i += 2) {
                unique.add(a[i]);
                if (a[i] === b[i]) ++same;
                let max = 0;
                for (let shift = 0; shift < 32; shift += 8)
                    max = Math.max(max, Math.abs(((a[i] >>> shift) & 255) - ((b[i] >>> shift) & 255)));
                if (max > 8) ++significant;
                ++total;
            }
            return { total, same, significant, unique: unique.size };
        });
    }
    try {
        const a = await pixels(false),
            b = await pixels(false),
            c = await pixels(true),
            d = await pixels(false);
        console.info("Menu eye comparison", JSON.stringify({ skipped, control: diff(a, b), skip: diff(b, c), restore: diff(c, d) }));
        if (!skipped) throw Error("Parent clear test did not execute");
    } finally {
        scene._clearFrameBuffer = clear;
        if (read.mapState === "mapped") read.unmap();
        read.destroy();
        data.destroy();
    }
}
