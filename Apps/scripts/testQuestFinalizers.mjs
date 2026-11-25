import assert from "node:assert/strict";
import test from "node:test";
import { summarizeFinalizers } from "./summarizeQuestFinalizers.mjs";

test("separates delayed drains from GC and ignores startup, other processes and workers", () => {
    const line = (message, pid = 7, tid = 9) => `11-24 12:00:00.123 ${pid} ${tid} I ${message}`;
    const batch = "BabylonFinalize: tid=9 queued=5 wallUs=200 cpuUs=185 gc=2 gcEndUs=10000 startUs=26000 callbackSample=0 callbacks=0 heapSample=0 netHeapBytes=0";
    const rows = [line(batch), line("BabylonNative: Education checkpoint {}"), line(batch, 6), line(batch, 7, 10), line(batch)];
    const result = summarizeFinalizers(rows, 7, 9, "Education checkpoint");
    assert.equal(result.batches, 1);
    assert.equal(result.delaySinceLastGcUs.median, 16000);
    assert.equal(result.cpuUs.median, 185);
    assert.equal(result.maxCallbackUs, null);
    assert.equal(result.processNetHeapBytes, null);
    assert.equal(result.trackersWithoutCallback, null);
    assert.throws(() => summarizeFinalizers(rows, 7, 10, "Education checkpoint"), /Checkpoint/);
    assert.throws(() => summarizeFinalizers([], 7, 9), /No finalizer batches/);
});

test("uses completed tracker visits, not queue size at drain entry", () => {
    const row = "11-24 12:00:00.123 7 9 I BabylonFinalize: tid=9 queued=5 wallUs=20 cpuUs=19";
    const read = end => summarizeFinalizers([row + end], 7, 9);
    assert.equal(read(" typeSample=0 popped=0 withoutCallback=0").trackersProcessed, null);
    assert.equal(read(" typeSample=1 popped=7 withoutCallback=2").trackersProcessed.median, 7);
    assert.equal(read(" typeSample=1 popped=3 withoutCallback=0").trackersWithoutCallback.max, 0);
    assert.throws(() => read(" typeSample=1 popped=3 withoutCallback=4"), /census/);
    assert.throws(() => read(" typeSample=1 popped=3"), /census/);
});
