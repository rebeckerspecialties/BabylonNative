import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

export function gapInterval(message, time) {
    const v = Object.fromEntries(Array.from(message.matchAll(/(\w+)=(-?[\d.]+)/g), ([, k, n]) => [k, Number(n)]));
    const {frame, wait, hostGap, prevEnd, afterJs, jsWork, jsQueue} = v;
    if (![frame, wait, hostGap, prevEnd, afterJs, jsWork, jsQueue].every(n => Number.isFinite(n) && n >= 0)) return null;
    const end = time - wait - hostGap - prevEnd - afterJs;
    return {frame, end, start: end - jsWork, queued: end - jsWork - jsQueue, jsWork, jsQueue, afterJs};
}

// Android threadtime is millisecond-resolution wall time. Use intervals, not the
// nearest collection, and do not equate a temporal overlap with sole causation.
export function correlate(lines, pid, tid) {
    const events = [], gaps = [];
    for (const line of lines) {
        const m = /^(\d\d-\d\d) (\d\d:\d\d:\d\d\.\d+)\s+(\d+)\s+(\d+)\s+\w\s+(\w+)\s*:\s*(.*)$/.exec(line);
        if (!m || +m[3] !== pid) continue;
        const t = Date.parse("2000-" + m[1] + "T" + m[2] + "Z");
        if (+m[4] === tid && (m[5] === "BabylonGC" || m[5] === "BabylonFinalize")) {
            const duration = Number((m[5] === "BabylonGC" ? /\bus=(\d+)/ : /\bwallUs=(\d+)/).exec(m[6])?.[1]);
            if (duration > 0) events.push({ kind: m[5], start: t-duration/1000, end: t });
        }
        if (m[5] !== "BabylonNative" || !m[6].startsWith("XR predicted gap ")) continue;
        const interval = gapInterval(m[6], t);
        if (interval) gaps.push({ time: m[2], ...interval });
    }
    function overlapping(start,end) {
        return [...new Set(events.filter(e => Math.min(end, e.end) > Math.max(start, e.start)).map(e => e.kind))];
    }
    return gaps.map(({frame,time,end,start,queued,jsWork,jsQueue,afterJs}) => ({
        frame,time,jsWork,jsQueue,afterJs,queueOverlap:overlapping(queued,start),workOverlap:overlapping(start,end),
    }));
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , log, pid, tid] = process.argv;
    if (!log || !(+pid > 0 && +tid > 0)) throw Error("Usage: node correlateQuestFrameGaps.mjs LOG PID RENDER_ISOLATE_TID");
    const gaps = correlate(readFileSync(log,"utf8").split("\n"), +pid, +tid);
    if (!gaps.length) throw Error("No complete frame timing records for the selected process");
    console.log(JSON.stringify(gaps, null, 2));
}
