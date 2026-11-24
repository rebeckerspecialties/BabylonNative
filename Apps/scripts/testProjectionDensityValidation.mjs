import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { runInNewContext } from "node:vm";

const script = readFileSync(new URL("../Playground/Scripts/xr_projection_density_validation.js", import.meta.url), "utf8");

function fixture({ brokenEye, samples = 1, eyes = ["right", "left"] } = {}) {
    const layer = { fixedFoveation: 0.5 },
        pixels = new Uint8Array(16384);
    let submissions = 0,
        calls = 0,
        destroyed = 0,
        resolved = 0;
    const session = {
        renderState: { layers: [layer] },
        requestAnimationFrame(cb) {
            queueMicrotask(() =>
                cb(0, { getViewerPose: () => (++calls === 1 ? null : { views: eyes.map((eye) => ({ eye })) }) }),
            );
        },
    };
    const texture = { width: 100, height: 100, format: "rgba8unorm", createView: () => ({}) };
    const ctx = {
        setTimeout,
        clearTimeout,
        GPUBufferUsage: { COPY_DST: 1, MAP_READ: 2 },
        GPUTextureUsage: { RENDER_ATTACHMENT: 16 },
        GPUMapMode: { READ: 1 },
        XRGPUBinding: class {
            getViewSubImage(active, view) {
                assert.equal(active, layer);
                return {
                    colorTexture: texture,
                    getViewDescriptor: () => ({ baseArrayLayer: view.eye === "left" ? 0 : 1 }),
                    viewport: { x: 4, y: 4, width: 90, height: 90 },
                };
            }
        },
    };
    const device = {
        createBuffer: () => ({
            mapAsync() {},
            unmap() {},
            getMappedRange: () => pixels.buffer,
            destroy() {
                ++destroyed;
            },
        }),
        createTexture: () => ({
            createView: () => ({}),
            destroy() {
                ++destroyed;
            },
        }),
        createShaderModule() {},
        createRenderPipeline(d) {
            assert.equal(d.multisample.count, samples);
            return {};
        },
        createCommandEncoder: () => ({
            beginRenderPass({ colorAttachments: [color] }) {
                assert.equal(color.storeOp, samples > 1 ? "discard" : "store");
                if (color.resolveTarget) ++resolved;
                return { setViewport() {}, setPipeline() {}, draw() {}, end() {} };
            },
            copyTextureToBuffer({ origin: [x, y, index] }, { offset, bytesPerRow }) {
                const peripheral = x === 79;
                assert.equal(x, y);
                assert.ok(peripheral || x === 41);
                assert.equal(offset, (index * 2 + Number(peripheral)) * 4096);
                const coarse = layer.fixedFoveation === 1 && peripheral && index !== brokenEye;
                for (let row = 0; row < 16; ++row)
                    for (let col = 0; col < 16; ++col) {
                        pixels[offset + row * bytesPerRow + col * 4] = coarse ? 0 : ((row ^ col) & 1) * 255;
                    }
            },
            finish() {},
        }),
        queue: {
            submit() {
                ++submissions;
            },
        },
    };
    runInNewContext(script, ctx);
    return {
        run: () => ctx.validateProjectionDensity(session, device, {}, layer, samples),
        layer,
        counts: () => ({ submissions, destroyed, resolved }),
    };
}

for (const samples of [1, 4])
    test(`checks the active layer including ${samples}x attachment and restores its setting`, async () => {
        const f = fixture({ samples });
        const { counts } = await f.run();
        assert.deepEqual(Array.from(counts[1]), [240, 0, 240, 0]);
        assert.equal(f.layer.fixedFoveation, 0.5);
        assert.deepEqual(f.counts(), {
            submissions: 15,
            destroyed: samples === 1 ? 1 : 3,
            resolved: samples === 1 ? 0 : 30,
        });
    });
for (const brokenEye of [0, 1])
    test(`rejects an unfoveated periphery in eye ${brokenEye}`, async () => {
        const f = fixture({ brokenEye });
        await assert.rejects(f.run(), /Unexpected projection density/);
        assert.equal(f.layer.fixedFoveation, 0.5);
    });
test("does not accept two copies of the left eye as stereo", async () => {
    const f = fixture({ eyes: ["left", "left"] });
    await assert.rejects(f.run(), /active tracked stereo/);
    assert.equal(f.counts().submissions, 0);
    assert.equal(f.layer.fixedFoveation, 0.5);
});
