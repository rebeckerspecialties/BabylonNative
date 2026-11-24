import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

// Run one interactive layer per process. Logcat timestamps have millisecond precision.
export function summarizeFrameGaps(lines, pid) {
    let start, end, thread, receipt;
    const gaps = [], collections = [];
    for (const line of lines) {
        const m = /^(\d\d-\d\d) (\d\d:\d\d:\d\d\.\d+)\s+(\d+)\s+(\d+)\s+\w\s+(\w+)\s*:\s*(.*)$/.exec(line);
        if (!m || Number(m[3]) !== pid) continue;
        const time = Date.parse("2000-" + m[1] + "T" + m[2] + "Z");
        const [, , , , tid, tag, message] = m;
        if (tag === "BabylonGC") {
            const gc = /\bus=(\d+)\b/.exec(message);
            if (gc) collections.push({ tid, begin: time - Number(gc[1]) / 1000, end: time });
        } else if (tag === "BabylonNative" && message.startsWith("XR displaying layer ")) {
            if (start !== undefined) throw Error("Expected one interactive layer in this process");
            start = time; thread = tid;
        } else if (tag === "BabylonNative" && message.startsWith("XR layer submitted ")) {
            receipt = JSON.parse(message.slice("XR layer submitted ".length)); end = time;
        } else if (tag === "BabylonNative" && message.startsWith("XR predicted gap ")) {
            const v = Object.fromEntries(Array.from(message.matchAll(/(\w+)=(-?[\d.]+)/g), ([, k, n]) => [k, Number(n)]));
            if (![v.wait,v.hostGap,v.prevEnd,v.afterJs,v.jsWork,v.jsQueue].every(n => Number.isFinite(n) && n >= 0)) continue;
            const jsEnd = time - v.wait - v.hostGap - v.prevEnd - v.afterJs;
            gaps.push({ frame: v.frame, time, jsStart: jsEnd-v.jsWork, jsEnd, dispatch: jsEnd-v.jsWork-v.jsQueue, workMs: v.jsWork });
        }
    }
    if (start === undefined || !receipt || end < start || !(receipt.steadyMs > 0)) throw Error("Missing complete timed layer receipt");
    const steadyStart = Math.max(start, end-receipt.steadyMs);
    const active = gaps.filter(g => g.time >= steadyStart && g.time <= end);
    const renderGC = collections.filter(g => g.tid === thread);
    const overlaps = (a,b,g) => a <= g.end+1 && g.begin <= b+1;
    return { reportedMisses:receipt.missedRefreshes, loggedGaps:active.length, renderGcSeen:renderGC.length,
        steadyCollections:renderGC.filter(g => overlaps(steadyStart,end,g)).length,
        gapsDuringCallbackGC:active.filter(f => renderGC.some(g => overlaps(f.jsStart,f.jsEnd,g))).map(f => f.frame),
        gapsDuringDispatchGC:active.filter(f => renderGC.some(g => overlaps(f.dispatch,f.jsStart,g))).map(f => f.frame),
        callbackMs:active.map(f => f.workMs) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    if (!process.argv[2] || !(Number(process.argv[3]) > 0)) throw Error("Usage: node summarizeQuestFrameGaps.mjs LOG APP_PID");
    console.log(JSON.stringify(summarizeFrameGaps(readFileSync(process.argv[2], "utf8").split("\n"), Number(process.argv[3])), null, 2));
}
