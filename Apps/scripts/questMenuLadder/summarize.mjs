import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

const median = (a) => {
    if (!a.length) return null;
    const v = [...a].sort((x, y) => x - y);
    return +((v[Math.floor(v.length / 2)] + v[Math.floor((v.length - 1) / 2)]) / 2).toFixed(2);
};
export function summarize(log, pid) {
    const rows = [];
    let window;
    for (const line of log.split("\n")) {
        const m = /^\S+ (\d+):(\d+):([\d.]+)\s+(\d+)\s+\d+ \w (\S+)\s*:\s*(.*)$/.exec(line);
        if (!m || +m[4] !== pid) continue;
        const time = +m[1] * 3600 + +m[2] * 60 + +m[3],
            tag = m[5],
            msg = m[6];
        if (tag === "BabylonNative" && msg.startsWith("Menu ladder begin ")) {
            const { stage, views } = JSON.parse(msg.slice(18));
            window = { stage, start: time, views, gpu: [], fps: [], app: [] };
        } else if (window && tag === "BabylonNative" && msg.startsWith("Menu ladder end ")) {
            const { stage, frames, elapsed, counters } = JSON.parse(msg.slice(16));
            if (window.stage !== stage || !frames) throw Error("Mismatched or empty stage: " + stage);
            rows.push({
                stage,
                sceneFPS: +((frames * 1000) / elapsed).toFixed(1),
                fps: median(window.fps),
                seconds: window.fps.length,
                samples: window.gpu.length,
                eye: median(window.gpu.map((x) => x[0])),
                downsample: median(window.gpu.map((x) => x[1])),
                app: median(window.app),
                draws: +(counters.drawCallCount / frames).toFixed(1),
                passes: +(counters.renderPassBeginCount / frames).toFixed(1),
                uploads: counters.canvasGpuSnapshotCount,
                viewSizes: window.views?.map((view) => [view[0]?.width, view[0]?.height]),
            });
            window = undefined;
        } else if (window && time - window.start >= 1.5) {
            const gpu = tag === "BabylonNative" && /^XR GPU frame=\d+ eyeMs=([\d.]+) resolveMs=([\d.]+) passes=\d+$/.exec(msg);
            const vr = tag === "VrApi" && /^FPS=(\d+)\/\d+,.*?,App=([\d.]+)ms,/.exec(msg);
            if (gpu) window.gpu.push([+gpu[1], +gpu[2]]);
            if (vr) {
                window.fps.push(+vr[1]);
                window.app.push(+vr[2]);
            }
        }
    }
    return rows;
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const file = process.argv[2];
    const rows = summarize(readFileSync(file, "utf8"), +readFileSync(file + ".pid", "utf8"));
    if (!rows.length) throw Error("No complete app-PID measurement windows");
    console.table(rows.map(({ viewSizes, ...row }) => row));
}
