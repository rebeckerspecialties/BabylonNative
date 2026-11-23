// Compare Canvas text metrics to its own rasterization, not to another font renderer.
(async function () {
    const canvas = typeof document === "undefined" ? new _native.Canvas() : document.createElement("canvas");
    canvas.width = 240;
    canvas.height = 112;
    const deadline = Date.now() + 10000;
    let context;
    while (!(context = canvas.getContext("2d"))) {
        if (Date.now() >= deadline) throw Error("Canvas2D context did not become available");
        await new Promise(resolve => setTimeout(resolve, 100));
    }
    const baseline = 76;
    for (const size of [24, 48]) {
        context.font = "bold " + size + "px sans-serif";
        context.textBaseline = "alphabetic";
        for (const text of ["H", "gyp", "Stats", " "]) {
            context.clearRect(0, 0, canvas.width, canvas.height);
            context.fillStyle = "white";
            context.fillText(text, 12, baseline);
            const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
            let first = canvas.height, last = -1;
            for (let y = 0; y < canvas.height; ++y) {
                for (let x = 0; x < canvas.width; ++x) {
                    if (pixels[4 * (y * canvas.width + x) + 3] > 16) {
                        first = Math.min(first, y);
                        last = y;
                        break;
                    }
                }
            }
            const m = context.measureText(text);
            const hasInk = text !== " ";
            const top = hasInk ? baseline - first : 0;
            const bottom = hasInk ? last + 1 - baseline : 0;
            if (hasInk !== (last >= first) || m.width <= 0 ||
                !(Math.abs(m.actualBoundingBoxAscent - top) <= 2 && Math.abs(m.actualBoundingBoxDescent - bottom) <= 2)) {
                throw new Error(JSON.stringify({text, size, top, bottom, ascent: m.actualBoundingBoxAscent, descent: m.actualBoundingBoxDescent}));
            }
        }
    }
    console.info("Canvas ink bounds PASS: 8 raster comparisons");
})();
