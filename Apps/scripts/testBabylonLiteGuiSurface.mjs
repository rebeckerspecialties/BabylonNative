import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { runInNewContext } from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/babylon_lite_bootstrap.js", import.meta.url), "utf8");
function load(extra = {}) {
    const context = { console: { error() {} }, navigator: {}, performance, URL, setInterval() {}, ...extra };
    runInNewContext(source, context);
    return context;
}

test("non-layout font probes have empty bounds while the render canvas retains its viewport", () => {
    const { document } = load();
    const rect = document.createElement("span").getBoundingClientRect();
    assert.deepEqual(Object.values(rect), Array(8).fill(0));
    assert.equal(document.getElementById("renderCanvas").getBoundingClientRect().width, 1280);
});

test("GUI images use the native decoder and resolve against their owning document", () => {
    class Image {
        get src() { return this.url; }
        set src(url) { this.url = url; }
    }
    const { document } = load({ Image });
    const image = document.createElement("IMG");
    assert.ok(image instanceof Image);
    assert.equal(image.ownerDocument, document);
    document.baseURI = "app:///Scripts/";
    image.src = "intro.png";
    assert.equal(image.src, "app:///Scripts/intro.png");
    image.src = "https://example.org/assets/intro.png";
    assert.equal(image.src, "https://example.org/assets/intro.png");
});

test("a canvas without a native backing does not advertise a native texture", () => {
    assert.equal(load().document.createElement("canvas").getCanvasTexture, undefined);
    class Canvas {
        getCanvasTexture() { return this; }
    }
    const { OffscreenCanvas } = load({ _native: { Canvas } });
    const offscreen = new OffscreenCanvas(2, 2);
    assert.ok(offscreen.getCanvasTexture() instanceof Canvas);
    assert.equal(Object.keys(offscreen).includes("getCanvasTexture"), false);
});
