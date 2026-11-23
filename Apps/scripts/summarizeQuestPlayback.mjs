import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

function quantile(values, p) {
    if (!values.length) return null;
    const sorted = [...values].sort((a, b) => a - b);
    const at = (sorted.length - 1) * p, lo = Math.floor(at);
    return +(sorted[lo] + (sorted[Math.ceil(at)] - sorted[lo]) * (at - lo)).toFixed(2);
}

/** Join only the app's VrApi windows, between consecutive playback-date samples. */
export function summarize(lines, pid, date) {
    const rows = new Map();
    let prior, windows = [], mode = "original", changed = false;
    for (const line of lines) {
        const entry = /^\S+\s+(\S+)\s+(\d+)\s+\d+\s+\w\s+(\S+)\s*:\s*(.*)$/.exec(line);
        if (!entry || Number(entry[2]) !== pid) continue;
        const [, , , tag, message] = entry;
        if (tag === "VrApi" && message.startsWith("FPS=")) {
            const fps = /^FPS=(\d+)\/(\d+),/.exec(message);
            if (fps && prior) {
                const app = /,App=([\d.]+)ms(?:,|$)/.exec(message);
                const gpu = /,GPU%=([\d.]+)(?:,|$)/.exec(message);
                windows.push({ fps: Number(fps[1]), hz: Number(fps[2]),
                    appMs: app && Number(app[1]), gpuLoad: gpu && Number(gpu[1]) });
            }
            continue;
        }
        if (tag !== "BabylonNative") continue;
        if (message.startsWith("Quest ring control ")) {
            const { enabled, rings, skipHitbox } = JSON.parse(message.slice("Quest ring control ".length));
            if (!rings) throw Error("No ring meshes matched the comparison");
            mode = enabled ? "reduced" : "original";
            if (skipHitbox) mode += "+no-hitbox-draw";
            changed = true;
            continue;
        }
        if (!message.startsWith("Quest nav window ")) continue;
        const current = JSON.parse(message.slice("Quest nav window ".length));
        const month = current.date?.slice(0, 7);
        // Do not mix a mode switch, load/reset, timer suspension, or a calendar boundary into a bucket.
        if (prior && month && month === prior.date?.slice(0, 7) && !changed &&
            (!date || (current.date?.slice(0, 10) === date && prior.date?.slice(0, 10) === date)) &&
            current.elapsedMs - prior.elapsedMs <= 6500 && current.elapsedMs > prior.elapsedMs) {
            for (const window of windows) {
                const key = [date ?? month, mode, window.hz].join("/");
                const row = rows.get(key) ?? { fps: [], indices: [], appMs: [], gpuLoad: [] };
                row.fps.push(window.fps);
                row.indices.push(current.indices);
                if (Number.isFinite(window.appMs)) row.appMs.push(window.appMs);
                if (Number.isFinite(window.gpuLoad)) row.gpuLoad.push(window.gpuLoad);
                rows.set(key, row);
            }
        }
        prior = current; windows = []; changed = false;
    }
    return [...rows].map(([key, { fps, indices, appMs, gpuLoad }]) => ({
        key, seconds: fps.length, median: quantile(fps, 0.5), p10: quantile(fps, 0.1),
        below30: fps.filter(x => x < 30).length, medianIndices: quantile(indices, 0.5),
        ...(appMs.length && { medianAppMs: quantile(appMs, 0.5) }),
        ...(gpuLoad.length && { medianGpuLoad: quantile(gpuLoad, 0.5) }),
    }));
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const args = process.argv.slice(2);
    let date;
    if (args[0] === "--date") {
        args.shift();
        date = args.shift();
        if (!/^\d{4}-\d{2}-\d{2}$/.test(date ?? "")) throw Error("Expected --date YYYY-MM-DD");
    }
    if (!args.length) throw Error("Expected [--date YYYY-MM-DD] LOG...");
    for (const path of args) {
        const pid = Number(readFileSync(path + ".pid", "utf8"));
        if (!Number.isInteger(pid) || pid <= 0) throw Error("Missing app PID for " + path);
        const windows = summarize(readFileSync(path, "utf8").split("\n"), pid, date);
        if (!windows.length) throw Error("No attributed playback windows in " + path);
        console.log(path);
        console.table(windows);
    }
}
