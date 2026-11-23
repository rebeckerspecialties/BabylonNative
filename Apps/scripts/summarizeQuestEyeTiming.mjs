import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

function median(numbers) {
    if (!numbers.length) return null;
    const sorted = [...numbers].sort((a, b) => a - b);
    const middle = Math.floor(sorted.length / 2);
    return Number(((sorted[middle] + sorted[Math.floor((sorted.length - 1) / 2)]) / 2).toFixed(3));
}

// Only join samples between two nearby, same-date scene reports from the app PID.
// Drop mode transitions so an asynchronously delivered query is not attributed to a new control.
export function summarizeEyeTiming(lines, pid, day) {
    const groups = new Map();
    let previous, mode = "unchanged", changed = false, gpu = [], fps = [], app = [];
    const frames = new Set();
    for (const line of lines) {
        const item = /^\S+\s+\S+\s+(\d+)\s+\d+\s+\w\s+(\S+)\s*:\s*(.*)$/.exec(line);
        if (!item || Number(item[1]) !== pid) continue;
        const [, , tag, message] = item;
        if (tag === "VrApi") {
            const sample = /^FPS=(\d+)\/60,/.exec(message);
            if (sample) {
                fps.push(Number(sample[1]));
                const metric = /,App=([\d.]+)ms,/.exec(message);
                if (metric) app.push(Number(metric[1]));
            }
            continue;
        }
        if (tag !== "BabylonNative") continue;
        if (message.startsWith("XR depth trial invalid")) throw Error(message);
        if (message.startsWith("XR depth mode ")) {
            const state = JSON.parse(message.slice("XR depth mode ".length));
            if (state.invalid) throw Error("Invalid depth control");
            mode = [state.drop ? "discard" : "store", state.tiled ? "tiled" : "", state.blocks ? "blocks" : ""].filter(Boolean).join("+");
            changed = true;
        } else if (message.startsWith("XR GPU frame=")) {
            const sample = /^XR GPU frame=(\d+) eyeMs=([\d.]+) resolveMs=([\d.]+) passes=(\d+)$/.exec(message);
            if (!sample || frames.has(sample[1])) continue;
            frames.add(sample[1]);
            gpu.push({ eye: Number(sample[2]), resolve: Number(sample[3]), passes: Number(sample[4]) });
        } else if (message.startsWith("Quest nav window ")) {
            const current = JSON.parse(message.slice("Quest nav window ".length));
            if (previous?.date?.slice(0, 10) === day && current.date?.slice(0, 10) === day && !changed &&
                current.elapsedMs > previous.elapsedMs && current.elapsedMs - previous.elapsedMs <= 6500) {
                const group = groups.get(mode) ?? { gpu: [], fps: [], app: [] };
                group.gpu.push(...gpu); group.fps.push(...fps); group.app.push(...app);
                groups.set(mode, group);
            }
            previous = current; changed = false; gpu = []; fps = []; app = [];
        }
    }
    return [...groups].map(([mode, g]) => ({
        mode, pairedFrames: g.gpu.length, compositorSeconds: g.fps.length,
        eyeMs: median(g.gpu.map(x => x.eye)), resolveMs: median(g.gpu.map(x => x.resolve)),
        pairedMs: median(g.gpu.map(x => x.eye + x.resolve)), passes: median(g.gpu.map(x => x.passes)),
        fps: median(g.fps), above30: g.fps.filter(x => x > 30).length, appMs: median(g.app),
    }));
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , path, day] = process.argv;
    if (!path || !/^\d{4}-\d{2}-\d{2}$/.test(day)) throw Error("Usage: LOG YYYY-MM-DD");
    const rows = summarizeEyeTiming(readFileSync(path, "utf8").split("\n"), Number(readFileSync(path + ".pid", "utf8")), day);
    if (!rows.some(row => row.pairedFrames)) throw Error("No matching stereo GPU samples");
    console.table(rows);
}
