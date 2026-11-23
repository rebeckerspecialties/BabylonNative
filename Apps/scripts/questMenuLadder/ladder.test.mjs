import { test } from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import { readFileSync } from "node:fs";
import { summarize } from "./summarize.mjs";

const guard = vm.runInNewContext(readFileSync(new URL("parent.js", import.meta.url), "utf8") + ";isUnusedParentClear");
const eye = (resource) => ({ outputRenderTarget: { getInternalTexture: () => ({ _hardwareTexture: { underlyingResource: resource } }) } });
test("only skips the non-rendered parent of two known independent eyes on the default framebuffer", () => {
    const left = {},
        right = {},
        parent = { rigCameras: [eye(left), eye(right)] };
    const scene = { activeCamera: parent, getEngine: () => ({ _currentFrameBufferIsDefaultFrameBuffer: () => true }) };
    assert.ok(guard(scene, parent));
    for (const camera of [undefined, ...parent.rigCameras]) assert.equal(guard(scene, camera), false);
    for (const key of ["outputRenderTarget", "_multiviewTexture"]) {
        parent[key] = {};
        assert.equal(guard(scene, parent), false);
        delete parent[key];
    }
    for (const eyes of [[], [eye(left)], [eye(left), eye(left)], [eye(left), eye(undefined)]]) {
        assert.equal(guard({ ...scene, activeCamera: { rigCameras: eyes } }, parent), false);
        parent.rigCameras = eyes;
        assert.equal(guard(scene, parent), false);
    }
    parent.rigCameras = [eye(left), eye(right)];
    assert.equal(guard({ ...scene, prePass: true }, parent), false);
    assert.equal(guard({ ...scene, getEngine: () => ({ _currentFrameBufferIsDefaultFrameBuffer: () => false }) }, parent), false);
});
test("rejects stale compositor windows, unrelated process samples and unfinished stages", () => {
    const line = (t, p, tag, msg) => `11-21 01:02:${t}  ${p}  718 I ${tag} : ${msg}`;
    const begin = 'Menu ladder begin {"stage":"full","views":[]}',
        end =
            'Menu ladder end {"stage":"full","elapsed":2000,"frames":100,"counters":{"drawCallCount":200,"renderPassBeginCount":300,"canvasGpuSnapshotCount":0}}';
    const vr = "FPS=49/72,Tear=0,App=14ms,LCnt=0";
    const lines = [
        line("01.0", 7, "BabylonNative", begin),
        line("01.5", 7, "VrApi", vr),
        line("02.7", 99, "VrApi", vr),
        line("02.9", 7, "VrApi", vr),
        line("03.0", 7, "BabylonNative", end),
        line("04.0", 7, "BabylonNative", begin),
    ];
    const [row] = summarize(lines.join("\n"), 7);
    assert.deepEqual([row.seconds, row.fps, row.eye, row.draws, row.passes, row.sceneFPS], [1, 49, null, 2, 3, 50]);
});
