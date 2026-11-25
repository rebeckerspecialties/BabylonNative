import assert from "node:assert/strict";
import test from "node:test";
import { correlate } from "./correlateQuestFrameGaps.mjs";
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

const gap = "11-25 00:00:00.020 41 41 I BabylonNative: XR predicted gap frame=75 wait=1 hostGap=1 prevEnd=1 afterJs=1 jsWork=5 jsQueue=12";
const event = (at, pid, tid, tag, duration) => `${at} ${pid} ${tid} I ${tag}: ${duration}`;
test("correlate distinct queue and execution intervals on the selected isolate", () => {
    const result = correlate([gap,
        event("11-25 00:00:00.004",41,48,"BabylonGC","us=7000"),
        event("11-25 00:00:00.015",41,48,"BabylonFinalize","wallUs=2000"),
        event("11-25 00:00:00.033",41,48,"BabylonGC","us=1000")],41,48);
    assert.deepEqual(result[0].queueOverlap,["BabylonGC"]);
    assert.deepEqual(result[0].workOverlap,["BabylonFinalize"]);
});
test("do not assign another isolate, process, a nearby previous collection, or a touching boundary", () => {
    const result = correlate([gap,
        event("11-25 00:00:00.015",41,49,"BabylonGC","us=6000"),
        event("11-25 00:00:00.015",79,48,"BabylonGC","us=6000"),
        event("11-24 23:59:59.999",41,48,"BabylonGC","us=5000"),
        event("11-25 00:00:00.018",41,48,"BabylonFinalize","wallUs=2000")],41,48);
    assert.deepEqual([result[0].queueOverlap,result[0].workOverlap],[[],[]]);
    assert.deepEqual(correlate([gap.replace("jsWork=5","jsWork=-1")],41,48),[]);
});
