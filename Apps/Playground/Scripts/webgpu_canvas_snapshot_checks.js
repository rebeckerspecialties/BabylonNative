// Validation only: an external image copy is a snapshot, independent of later canvas draws.
async function checkCanvasSnapshots(device) {
    const width = 96;
    const height = 4;
    const sources = [new OffscreenCanvas(width, height), new OffscreenCanvas(width, height)];
    const targets = sources.map(() => device.createTexture({
        size: [width, height], format: "rgba8unorm",
        usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC | GPUTextureUsage.RENDER_ATTACHMENT,
    }));
    const readback = device.createBuffer({ size: 512, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    function fill(canvas, color) {
        const context = canvas.getContext("2d");
        context.clearRect(0, 0, width, height);
        context.fillStyle = color;
        context.fillRect(0, 0, width, height);
    }
    try {
        for (let i = 0; i < targets.length; ++i) {
            fill(sources[i], i === 0 ? "rgba(255,0,0,0.5)" : "hsla(240,100%,50%,100%)");
            device.queue.copyExternalImageToTexture({ source: sources[i], flipY: true }, { texture: targets[i] }, [width, height]);
        }
        fill(sources[0], "#00ff00");
        const encoder = device.createCommandEncoder();
        for (let i = 0; i < targets.length; ++i) {
            encoder.copyTextureToBuffer({ texture: targets[i], origin: [48, 2] }, { buffer: readback, offset: i * 256 }, [1, 1]);
        }
        device.queue.submit([encoder.finish()]);
        await readback.mapAsync(GPUMapMode.READ);
        const bytes = new Uint8Array(readback.getMappedRange());
        const pixels = [...bytes.slice(0, 4), ...bytes.slice(256, 260)];
        readback.unmap();
        const expected = [255, 0, 0, 128, 0, 0, 255, 255];
        if (pixels.some((v, i) => Math.abs(v - expected[i]) > 1)) {
            throw new Error("Independent canvas snapshot pixels: " + pixels.join(","));
        }
        const ctx = sources[0].getContext("2d");
        ctx.clearRect(0, 0, width, height);
        ctx.fillStyle = "#00ff00";
        ctx.fillRect(11, 1, 1, 1);
        // Replace an existing opaque label region, including transparent texels.
        for (let i = 0; i < 12; ++i) {
            const flipY = (i & 1) !== 0;
            const premultipliedAlpha = (i & 2) === 0;
            device.queue.copyExternalImageToTexture({ source: sources[0], origin: [11, 1], flipY },
                { texture: targets[1], origin: [3, 1], premultipliedAlpha }, [1, 2]);
            const verify = device.createCommandEncoder();
            verify.copyTextureToBuffer({ texture: targets[1], origin: [2, 1] },
                { buffer: readback, bytesPerRow: 256 }, [2, 2]);
            device.queue.submit([verify.finish()]);
            await readback.mapAsync(GPUMapMode.READ);
            const rows = new Uint8Array(readback.getMappedRange());
            const actual = [...rows.slice(0, 8), ...rows.slice(256, 264)];
            readback.unmap();
            const blue = [0, 0, 255, 255], green = [0, 255, 0, 255], empty = [0, 0, 0, 0];
            const wanted = [...blue, ...(flipY ? empty : green), ...blue, ...(flipY ? green : empty)];
            if (actual.some((v, i) => v !== wanted[i])) {
                throw new Error("Cropped Canvas snapshot iteration=" + i + ", flipY=" + flipY + ": " + actual.join(","));
            }
        }
        for (let i = 0; i < 6; ++i) {
            ctx.clearRect(0, 0, width, height);
            ctx.fillStyle = "#ff0000";
            const x = 8 + i * 3;
            ctx.fillRect(x, 1, 2, 1);
            device.queue.copyExternalImageToTexture({ source: sources[0], flipY: true }, { texture: targets[1] }, [width, height]);
            const verify = device.createCommandEncoder();
            verify.copyTextureToBuffer({ texture: targets[1], origin: [x - 3, 2] }, { buffer: readback }, [5, 1]);
            device.queue.submit([verify.finish()]);
            await readback.mapAsync(GPUMapMode.READ);
            const actual = [...new Uint8Array(readback.getMappedRange()).slice(0, 20)];
            readback.unmap();
            const wanted = [...Array(12).fill(0), 255, 0, 0, 255, 255, 0, 0, 255];
            if (actual.some((v, j) => v !== wanted[j])) throw new Error("Full redraw " + i + ": " + actual.join(","));
        }
        return pixels;
    } finally {
        readback.destroy();
        for (const texture of targets) texture.destroy();
    }
}

// Three navigations reuse one backing texture; compare to an independently drawn final label.
async function checkReplacedCanvasLabel(device) {
    const width = 128, height = 32, stride = width * 4;
    const canvas = new OffscreenCanvas(width, height);
    const fresh = new OffscreenCanvas(width, height);
    const targets = [canvas, fresh].map(() => device.createTexture({
        size: [width, height], format: "rgba8unorm",
        usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC | GPUTextureUsage.RENDER_ATTACHMENT,
    }));
    const bytes = device.createBuffer({ size: stride * height, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    function draw(source, texture, text) {
        const ctx = source.getContext("2d");
        ctx.clearRect(0, 0, width, height);
        ctx.fillStyle = "#ffffff";
        ctx.font = "16px sans-serif";
        ctx.fillText(text, 2, 22);
        device.queue.copyExternalImageToTexture({ source, flipY: true }, { texture }, [width, height]);
    }
    async function read(texture) {
        const cmd = device.createCommandEncoder();
        cmd.copyTextureToBuffer({ texture }, { buffer: bytes, bytesPerRow: stride }, [width, height]);
        device.queue.submit([cmd.finish()]);
        await bytes.mapAsync(GPUMapMode.READ);
        const result = new Uint8Array(bytes.getMappedRange()).slice();
        bytes.unmap();
        return result;
    }
    try {
        draw(canvas, targets[0], "react-native");
        const before = await read(targets[0]);
        draw(canvas, targets[0], "Babylon.js");
        draw(canvas, targets[0], "XR");
        draw(fresh, targets[1], "XR");
        const actual = await read(targets[0]);
        const expected = await read(targets[1]);
        let newInk = 0, erasedInk = 0;
        for (let i = 3; i < actual.length; i += 4) {
            if (expected[i]) ++newInk;
            if (before[i] && !expected[i]) ++erasedInk;
            if (actual[i] !== expected[i]) throw new Error("Stale navigation label at pixel " + ((i - 3) / 4));
        }
        if (!newInk || !erasedInk) throw new Error("Navigation label control contains no distinguishing glyphs");
        return { newInk, erasedInk };
    } finally {
        bytes.destroy();
        for (const texture of targets) texture.destroy();
    }
}
