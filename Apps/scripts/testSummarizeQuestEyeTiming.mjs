import assert from "node:assert/strict";
import { test } from "node:test";
import { summarizeEyeTiming } from "./summarizeQuestEyeTiming.mjs";

const day = "2016-01-10";
const line = (message, pid = 42, tag = "BabylonNative") => `11-21 00:00:00.000 ${pid} 19 I ${tag}: ${message}`;
const window = (ms, date = day) => line("Quest nav window " + JSON.stringify({ elapsedMs: ms, date }));
const gpu = (frame, eye, resolve, pid) => line(`XR GPU frame=${frame} eyeMs=${eye} resolveMs=${resolve} passes=4`, pid);

test("joins paired timestamps, excluding background processes, duplicates, date boundaries and mode switches", () => {
    const rows = summarizeEyeTiming([window(0, "2016-01-09"), gpu(1, 90, 5), window(5000),
        gpu(2, 30, 4), gpu(2, 30, 4), gpu(3, 90, 99, 7), gpu(4, 34, 2), line("FPS=20/60,App=48.00ms,", 42, "VrApi"),
        window(10000), line('XR depth mode {"drop":true}'), gpu(5, 20, 2), window(15000),
        gpu(6, 27, 3), window(20000), gpu(7, 2, 2), window(50000)], 42, day);
    assert.deepEqual(rows.map(r => [r.mode, r.pairedFrames, r.eyeMs, r.resolveMs, r.pairedMs, r.fps, r.appMs]),
        [["unchanged", 2, 32, 3, 35, 20, 48], ["discard", 1, 27, 3, 30, null, null]]);
});

test("rejects a discard experiment invalidated by a later consumer", () => {
    assert.throws(() => summarizeEyeTiming([line("XR depth trial invalid: later load")], 42, day), /later load/);
});
