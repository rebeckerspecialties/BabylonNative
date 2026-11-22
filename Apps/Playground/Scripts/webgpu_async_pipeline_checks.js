// Run on a fresh pair of logical devices. No native hooks or timing assumptions.
async function checkAsyncPipelines(adapter) {
    const device = await adapter.requestDevice();
    const other = await adapter.requestDevice();
    const check = (value, message) => { if (!value) throw Error(message); };
    let events = 0;
    device.onuncapturederror = () => ++events;
    const module = device.createShaderModule({ code: `
        @vertex fn v(@builtin(vertex_index) i:u32) -> @builtin(position) vec4f {
            let xy = array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));
            return vec4f(xy[i],0,1);
        }
        @fragment fn f() -> @location(0) vec4f { return vec4f(0,1,0,1); }
        @group(0) @binding(0) var<storage,read_write> value: u32;
        @compute @workgroup_size(1) fn c() { value = 713u; }
    ` });
    const render = {layout:"auto", vertex:{module,entryPoint:"v"}, fragment:{module,entryPoint:"f",targets:[{format:"rgba8unorm"}]}};
    const compute = {layout:"auto", compute:{module,entryPoint:"c"}};
    try {
        device.pushErrorScope("validation");
        const first = device.createRenderPipelineAsync(render);
        const second = device.createComputePipelineAsync(compute);
        render.fragment.entryPoint = compute.compute.entryPoint = "missing";
        const bad = device.createRenderPipelineAsync(render);
        const foreign = other.createShaderModule({code:"@compute @workgroup_size(1) fn c() {}"});
        const wrongOwner = device.createComputePipelineAsync({layout:"auto",compute:{module:foreign,entryPoint:"c"}});
        const popped = device.popErrorScope();
        const [rp, cp, invalid, mixed] = await Promise.allSettled([first, second, bad, wrongOwner]);
        check(rp.status === "fulfilled" && cp.status === "fulfilled", "Input descriptors were not snapshotted");
        for (const result of [invalid, mixed]) {
            check(result.status === "rejected" && result.reason instanceof GPUPipelineError && result.reason.reason === "validation", "Wrong async validation result");
        }
        check(await popped === null, "Compiler error leaked into device scope");
        const texture = device.createTexture({size:[1,1],format:"rgba8unorm",usage:GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC});
        const buffer = device.createBuffer({size:4,usage:GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC});
        const readback = device.createBuffer({size:260,usage:GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST});
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({colorAttachments:[{view:texture.createView(),loadOp:"clear",storeOp:"store",clearValue:[1,0,0,1]}]});
        pass.setPipeline(rp.value); pass.draw(3); pass.end();
        const dispatch = encoder.beginComputePass();
        dispatch.setPipeline(cp.value);
        dispatch.setBindGroup(0,device.createBindGroup({layout:cp.value.getBindGroupLayout(0),entries:[{binding:0,resource:{buffer}}]}));
        dispatch.dispatchWorkgroups(1); dispatch.end();
        encoder.copyTextureToBuffer({texture},{buffer:readback,bytesPerRow:256},[1,1]);
        encoder.copyBufferToBuffer(buffer,0,readback,256,4);
        device.queue.submit([encoder.finish()]);
        await readback.mapAsync(GPUMapMode.READ);
        const words = new Uint32Array(readback.getMappedRange());
        check(words[0] === 0xff00ff00 && words[64] === 713, "Async render or compute pipeline produced incorrect output");
        readback.unmap(); readback.destroy(); buffer.destroy(); texture.destroy();
        const pending = other.createComputePipelineAsync({layout:"auto",compute:{module:foreign,entryPoint:"c"}});
        other.destroy();
        check(await pending instanceof GPUComputePipeline, "Destroy during compilation did not resolve an invalid pipeline");
        check(events === 0, "Compiler error leaked into device event");
    } finally {
        other.destroy();
        device.destroy();
    }
}
