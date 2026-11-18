// Executes the installed UMD bundle: a fast snapshot frame must issue the depth/stencil
// clears between rendering groups that the recorded frame issued (patch 0008).
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import test from "node:test";

const require = createRequire(import.meta.url);
const B = require(resolve(process.env.BABYLONJS_BUNDLE || require.resolve("babylonjs/babylon.max.js")));

function fixture(groups) {
    const engine = new B.NullEngine();
    const scene = new B.Scene(engine);
    const camera = new B.FreeCamera("camera", new B.Vector3(0, 0, -10), scene);
    camera.setTarget(B.Vector3.Zero());
    groups.forEach((group, index) => { B.MeshBuilder.CreateBox("box" + index, { size: 1 }, scene).renderingGroupId = group; });
    const snapshot = { enabled: false, mode: B.Constants.SNAPSHOTRENDERING_STANDARD };
    Object.defineProperty(engine, "snapshotRendering", { get: () => snapshot.enabled });
    Object.defineProperty(engine, "snapshotRenderingMode", { get: () => snapshot.mode });
    // NullEngine has no uniform buffers, which the scene finalizes whenever a snapshot is enabled.
    scene.finalizeSceneUbo = () => scene.getSceneUniformBuffer();
    let clears = [];
    engine.clear = (color, backBuffer, depth, stencil) => { if (color === null && !backBuffer) clears.push([depth, !!stencil]); };
    const frame = (mode) => {
        snapshot.enabled = mode !== undefined;
        snapshot.mode = mode ?? B.Constants.SNAPSHOTRENDERING_STANDARD;
        clears = [];
        engine.beginFrame();
        scene.render();
        engine.endFrame();
        return clears;
    };
    return { scene, frame, dispose: () => { scene.dispose(); engine.dispose(); } };
}

test("a fast snapshot frame issues the group clears of the recorded frame", () => {
    const f = fixture([0, 1, 2]);
    const recorded = f.frame(B.Constants.SNAPSHOTRENDERING_STANDARD);
    assert.deepEqual(recorded, [[true, true], [true, true]]);
    assert.deepEqual(f.frame(B.Constants.SNAPSHOTRENDERING_FAST), recorded);
    assert.equal(f.scene.getActiveMeshes().length, 0);
    assert.deepEqual(f.frame(B.Constants.SNAPSHOTRENDERING_FAST), recorded);
    f.dispose();
});

test("a recorded frame without group clears replays none, and the clear flags are kept", () => {
    const single = fixture([0, 0]);
    assert.deepEqual(single.frame(B.Constants.SNAPSHOTRENDERING_STANDARD), []);
    assert.deepEqual(single.frame(B.Constants.SNAPSHOTRENDERING_FAST), []);
    single.dispose();

    const flags = fixture([0, 1, 2]);
    flags.scene.setRenderingAutoClearDepthStencil(1, true, false, true);
    flags.scene.setRenderingAutoClearDepthStencil(2, false);
    const recorded = flags.frame(B.Constants.SNAPSHOTRENDERING_STANDARD);
    assert.deepEqual(recorded, [[false, true]]);
    assert.deepEqual(flags.frame(B.Constants.SNAPSHOTRENDERING_FAST), recorded);
    flags.dispose();
});
