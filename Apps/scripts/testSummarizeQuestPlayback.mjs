import assert from "node:assert/strict";
import { test } from "node:test";
import { summarize } from "./summarizeQuestPlayback.mjs";

const line = (tag, message, pid = 42) => `11-21 12:00:01.001 ${pid} 89 I ${tag} : ${message}`;
const fps = (n, pid) => line("VrApi", "FPS=" + n + "/60,Stale=0", pid);
const sample = (elapsedMs, date = "2016-01-10") =>
    line("BabylonNative", "Quest nav window " + JSON.stringify({ elapsedMs, date, indices: 120 }));

test("excludes shell FPS, warm-up, partial windows and calendar transitions", () => {
    const result = summarize([fps(1), sample(5), fps(72, 8), fps(30), fps(28), sample(5005),
        fps(2), sample(10005, "2016-02-01"), fps(60)], 42);
    assert.deepEqual(result, [{ key: "2016-01/original/60", seconds: 2, median: 29, p10: 28.2, below30: 1, medianIndices: 120 }]);
});

test("excludes geometry changes and timer suspensions", () => {
    assert.deepEqual(summarize([sample(5), fps(1), line("BabylonNative", 'Quest ring control {"enabled":true,"rings":4}'),
        sample(5005), fps(25), sample(10005), fps(1), sample(30000)], 42),
        [{ key: "2016-01/reduced/60", seconds: 1, median: 25, p10: 25, below30: 1, medianIndices: 120 }]);
});

test("matches both ends of the exact scene date, excluding movement within the same month", () => {
    const lines = [sample(5, "2016-01-09"), fps(59), sample(5005), fps(19), sample(10005),
        fps(1), sample(15005, "2016-01-11")];
    assert.deepEqual(summarize(lines, 42, "2016-01-10"),
        [{ key: "2016-01-10/original/60", seconds: 1, median: 19, p10: 19, below30: 1, medianIndices: 120 }]);
    assert.deepEqual(summarize(lines, 42, "2016-01-08"), []);
});

test("keeps absent VrApi fields distinct from reported zero", () => {
    const [row] = summarize([sample(5), fps(19), line("VrApi", "FPS=20/60,App=48.72ms,GPU%=0.00"),
        sample(5005)], 42);
    assert.equal(row.medianAppMs, 48.72);
    assert.equal(row.medianGpuLoad, 0);
});
