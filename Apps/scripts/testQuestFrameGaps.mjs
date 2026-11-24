import assert from "node:assert/strict";
import test from "node:test";
import { summarizeFrameGaps } from "./summarizeQuestFrameGaps.mjs";

test("matches only the prior callback or queue interval on the rendering isolate", () => {
    const row = (time, tid, tag, message, pid=10) => `11-24 12:00:${time}  ${pid}  ${tid} I ${tag}: ${message}`;
    const gap = "XR predicted gap frame=31 delta=28ms jsWork=8 jsQueue=4 prevEnd=1 afterJs=1 hostGap=1 wait=4";
    const lines = [
        row("00.000",12,"BabylonNative","XR displaying layer Quad image"),
        row("01.000",10,"BabylonNative",gap),
        row("02.983",12,"BabylonGC","type=1 us=2000"), // Queue (2.981–2.985).
        row("02.990",12,"BabylonGC","type=1 us=2000"), // Callback (2.985–2.993).
        row("03.000",10,"BabylonNative",gap),
        row("03.990",13,"BabylonGC","type=1 us=2000"), // Other isolate.
        row("03.990",12,"BabylonGC","type=1 us=2000",20), // Other process.
        row("04.000",10,"BabylonNative",gap.replace("31","66")),
        row("04.900",12,"BabylonGC","type=1 us=2000"),
        row("05.000",12,"BabylonNative",'XR layer submitted {"steadyMs":2500,"missedRefreshes":2}')
    ];
    assert.deepEqual(summarizeFrameGaps(lines,10), {reportedMisses:2, loggedGaps:2, renderGcSeen:3, steadyCollections:3,
        gapsDuringCallbackGC:[31], gapsDuringDispatchGC:[31], callbackMs:[8,8]});
    assert.throws(() => summarizeFrameGaps(lines.slice(0,-1),10), /Missing/);
});
