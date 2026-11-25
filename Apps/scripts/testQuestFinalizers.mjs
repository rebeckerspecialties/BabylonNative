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
    assert.throws(() => summarizeFinalizers(rows, 7, 10, "Education checkpoint"), /Checkpoint/);
    assert.throws(() => summarizeFinalizers([], 7, 9), /No finalizer batches/);
});
