import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

function range(values) {
    if (!values.length) return null;
    values.sort((a,b) => a-b);
    const mid = Math.floor(values.length / 2);
    return { min: values[0], median: (values[mid] + values[(values.length - 1) >> 1]) / 2, max: values.at(-1) };
}

// GC sequence and monotonic clocks belong to one isolate. Never join across threads.
export function summarizeFinalizers(lines, pid, tid, marker = "") {
    const batches = [];
    let started = !marker;
    for (const line of lines) {
        const match = /^\S+ \S+\s+(\d+)\s+(\d+)\s+\w\s+(\w+)\s*:\s*(.*)$/.exec(line);
        if (!match || +match[1] !== pid || +match[2] !== tid) continue;
        if (!started && match[3] === "BabylonNative" && match[4].startsWith(marker)) started = true;
        if (!started || match[3] !== "BabylonFinalize") continue;
        const row = Object.fromEntries(Array.from(match[4].matchAll(/\b(\w+)=(-?\d+)\b/g), ([,k,v]) => [k, Number(v)]));
        if (row.tid !== tid || !(row.queued > 0) || !(row.wallUs >= 0 && row.cpuUs >= 0)) throw Error("Incomplete finalizer batch");
        if (row.typeSample === 1 && !(Number.isSafeInteger(row.popped) && Number.isSafeInteger(row.withoutCallback) &&
            row.withoutCallback >= 0 && row.withoutCallback <= row.popped)) throw Error("Incomplete tracker census");
        batches.push(row);
    }
    if (!started) throw Error("Checkpoint not found on the selected thread");
    if (!batches.length) throw Error("No finalizer batches on the selected thread in this interval");
    return { pid, tid, batches: batches.length, queued: range(batches.map(r=>r.queued)),
        wallUs: range(batches.map(r=>r.wallUs)), cpuUs: range(batches.map(r=>r.cpuUs)),
        delaySinceLastGcUs: range(batches.filter(r=>r.gc > 0 && r.gcEndUs > 0 && r.startUs >= r.gcEndUs).map(r=>r.startUs-r.gcEndUs)),
        maxCallbackUs: range(batches.filter(r=>r.callbackSample !== 0 && r.callbacks > 0).map(r=>r.maxCallbackUs)),
        processNetHeapBytes: range(batches.filter(r=>r.heapSample === 1).map(r=>r.netHeapBytes)),
        trackersProcessed: range(batches.filter(r=>r.typeSample === 1).map(r=>r.popped)),
        trackersWithoutCallback: range(batches.filter(r=>r.typeSample === 1).map(r=>r.withoutCallback)) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , file, pid, tid, marker] = process.argv;
    if (!file || !(pid > 0 && tid > 0)) throw Error("Usage: node summarizeQuestFinalizers.mjs LOG PID JS_TID [CHECKPOINT_PREFIX]");
    console.log(JSON.stringify(summarizeFinalizers(readFileSync(file, "utf8").split("\n"), +pid, +tid, marker), null, 2));
}
