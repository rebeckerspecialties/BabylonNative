// Measures frame-to-frame instability in captures written by
// Apps/Playground/Scripts/native_xr_portal_stability_capture.js.
//
// whole-pixel: frames are aligned by undoing each shift exactly. Rasterization
//   is identical across frames, so the per-pixel temporal standard deviation is
//   instability locked to the screen (AO grid, sampling pattern, filters).
// sub-pixel: the image moves at a constant sub-pixel velocity. A band-limited
//   image changes smoothly, so the temporal second difference isolates popping.
// All values are 8-bit luma code values.
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

/** Decodes an 8-bit RGB or RGBA non-interlaced PNG into Rec. 709 luma. */
export function decodePngLuma(buffer) {
    if (buffer.length < 8 || !buffer.subarray(0, 8).equals(PNG_SIGNATURE)) {
        throw new Error("Not a PNG file.");
    }
    let width = 0;
    let height = 0;
    let channels = 0;
    const data = [];
    for (let offset = 8; offset + 12 <= buffer.length;) {
        const length = buffer.readUInt32BE(offset);
        const type = buffer.toString("latin1", offset + 4, offset + 8);
        const body = buffer.subarray(offset + 8, offset + 8 + length);
        if (type === "IHDR") {
            width = body.readUInt32BE(0);
            height = body.readUInt32BE(4);
            const colorType = body[9];
            if (body[8] !== 8 || (colorType !== 2 && colorType !== 6) || body[12] !== 0) {
                throw new Error("Only 8-bit, non-interlaced RGB or RGBA PNG files are supported.");
            }
            channels = colorType === 2 ? 3 : 4;
        } else if (type === "IDAT") {
            data.push(body);
        } else if (type === "IEND") {
            break;
        }
        offset += 12 + length;
    }
    if (!channels) {
        throw new Error("PNG file has no header.");
    }
    const stride = width * channels;
    const raw = zlib.inflateSync(Buffer.concat(data));
    if (raw.length !== (stride + 1) * height) {
        throw new Error("PNG pixel data does not match its header.");
    }
    const luma = new Float32Array(width * height);
    let previous = new Uint8Array(stride);
    let current = new Uint8Array(stride);
    for (let y = 0; y < height; y++) {
        const filter = raw[y * (stride + 1)];
        const source = y * (stride + 1) + 1;
        for (let x = 0; x < stride; x++) {
            const left = x >= channels ? current[x - channels] : 0;
            const up = previous[x];
            const upLeft = x >= channels ? previous[x - channels] : 0;
            let predicted = 0;
            if (filter === 1) {
                predicted = left;
            } else if (filter === 2) {
                predicted = up;
            } else if (filter === 3) {
                predicted = (left + up) >> 1;
            } else if (filter === 4) {
                const estimate = left + up - upLeft;
                const distanceLeft = Math.abs(estimate - left);
                const distanceUp = Math.abs(estimate - up);
                const distanceUpLeft = Math.abs(estimate - upLeft);
                predicted = distanceLeft <= distanceUp && distanceLeft <= distanceUpLeft ? left : distanceUp <= distanceUpLeft ? up : upLeft;
            } else if (filter !== 0) {
                throw new Error("Unsupported PNG filter " + filter);
            }
            current[x] = (raw[source + x] + predicted) & 0xff;
        }
        for (let x = 0, pixel = y * width; x < stride; x += channels, pixel++) {
            luma[pixel] = 0.2126 * current[x] + 0.7152 * current[x + 1] + 0.0722 * current[x + 2];
        }
        [previous, current] = [current, previous];
    }
    return { width, height, luma };
}

function percentile(sorted, fraction) {
    return sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(fraction * (sorted.length - 1) + 0.5))] : 0;
}

function summarize(map, mapWidth, mapHeight, region) {
    const x0 = Math.max(0, Math.floor(region[0] * mapWidth));
    const y0 = Math.max(0, Math.floor(region[1] * mapHeight));
    const x1 = Math.min(mapWidth, Math.ceil(region[2] * mapWidth));
    const y1 = Math.min(mapHeight, Math.ceil(region[3] * mapHeight));
    const values = new Float32Array(Math.max(0, x1 - x0) * Math.max(0, y1 - y0));
    let sum = 0;
    let unstable = 0;
    let count = 0;
    for (let y = y0; y < y1; y++) {
        for (let x = x0; x < x1; x++) {
            const value = map[y * mapWidth + x];
            values[count++] = value;
            sum += value;
            unstable += value > 2 ? 1 : 0;
        }
    }
    values.sort();
    return {
        mean: count ? sum / count : 0,
        p50: percentile(values, 0.5),
        p95: percentile(values, 0.95),
        p99: percentile(values, 0.99),
        unstableShare: count ? unstable / count : 0,
    };
}

/**
 * Temporal standard deviation of frames captured at whole-pixel shifts.
 * A positive x shift moves the image right; a positive y shift moves it up.
 * The returned map covers only the area present in every frame.
 */
export function wholePixelInstability(frames, shifts) {
    const { width, height } = frames[0];
    const margin = Math.max(...shifts.map((shift) => Math.max(Math.abs(shift[0]), Math.abs(shift[1])))) + 8;
    const mapWidth = width - 2 * margin;
    const mapHeight = height - 2 * margin;
    if (mapWidth <= 0 || mapHeight <= 0 || frames.some((frame) => frame.width !== width || frame.height !== height)) {
        throw new Error("Frames must share one size larger than the largest shift.");
    }
    const mean = new Float64Array(mapWidth * mapHeight);
    const squares = new Float64Array(mapWidth * mapHeight);
    frames.forEach((frame, index) => {
        const [dx, dy] = shifts[index];
        if (!Number.isInteger(dx) || !Number.isInteger(dy)) {
            throw new Error("Whole-pixel analysis needs integer shifts.");
        }
        for (let y = 0; y < mapHeight; y++) {
            const row = (y + margin - dy) * width + margin + dx;
            for (let x = 0; x < mapWidth; x++) {
                const value = frame.luma[row + x];
                mean[y * mapWidth + x] += value;
                squares[y * mapWidth + x] += value * value;
            }
        }
    });
    const map = new Float32Array(mapWidth * mapHeight);
    for (let i = 0; i < map.length; i++) {
        const average = mean[i] / frames.length;
        map[i] = Math.sqrt(Math.max(0, squares[i] / frames.length - average * average));
    }
    return { map, width: mapWidth, height: mapHeight, margin };
}

/** Mean absolute temporal second difference of frames at constant sub-pixel velocity. */
export function subPixelPopping(frames) {
    const { width, height } = frames[0];
    if (frames.length < 3 || frames.some((frame) => frame.width !== width || frame.height !== height)) {
        throw new Error("Sub-pixel analysis needs at least three frames of one size.");
    }
    const map = new Float32Array(width * height);
    for (let t = 1; t + 1 < frames.length; t++) {
        const before = frames[t - 1].luma;
        const current = frames[t].luma;
        const after = frames[t + 1].luma;
        for (let i = 0; i < map.length; i++) {
            map[i] += Math.abs(after[i] - 2 * current[i] + before[i]);
        }
    }
    for (let i = 0; i < map.length; i++) {
        map[i] /= frames.length - 2;
    }
    return { map, width, height, margin: 0 };
}

// Hill Valley regions as fractions of the frame: [left, top, right, bottom].
export const regions = {
    orbit: { "cafe-wall": [0, 0.035, 0.354, 0.521], "car-ground": [0.303, 0.538, 0.646, 0.844], "open-road": [0.664, 0.361, 0.938, 0.625] },
    cafe: { wall: [0, 0.181, 0.889, 0.59], sidewalk: [0, 0.59, 0.586, 0.757], "open-road": [0.059, 0.799, 0.508, 0.972] },
    car: { ground: [0.152, 0.59, 0.707, 0.809] },
    trees: { crown: [0.332, 0.108, 0.596, 0.59], hedge: [0.303, 0.611, 0.484, 0.792] },
    bush: { hedge: [0, 0.59, 1, 0.844], tree: [0.707, 0, 1, 0.59] },
};

/** Reads the capture manifest that the Playground prints to its log. */
export function readManifest(logText) {
    const match = /stability:manifest=(\{.*\})/.exec(logText);
    if (!match) {
        throw new Error("The log has no stability manifest.");
    }
    return JSON.parse(match[1]);
}

export function analyzeCaptures(directory, manifest) {
    const result = {};
    for (const pose of manifest.poses) {
        const frames = manifest.shifts.map((_, index) => {
            const name = ["stability", manifest.label, manifest.view, manifest.mode, pose, String(index).padStart(2, "0")].join("-") + ".png";
            return decodePngLuma(fs.readFileSync(path.join(directory, name)));
        });
        const measured = manifest.mode === "whole-pixel" ? wholePixelInstability(frames, manifest.shifts) : subPixelPopping(frames);
        const { width, height } = frames[0];
        result[pose] = {};
        for (const [name, region] of Object.entries({ all: [0, 0, 1, 1], ...(regions[pose] || {}) })) {
            // Regions are fractions of the full frame; the map omits a margin.
            const inMap = [
                (region[0] * width - measured.margin) / measured.width, (region[1] * height - measured.margin) / measured.height,
                (region[2] * width - measured.margin) / measured.width, (region[3] * height - measured.margin) / measured.height,
            ];
            result[pose][name] = summarize(measured.map, measured.width, measured.height, inMap);
        }
    }
    return result;
}

export function formatTable(manifest, result) {
    const lines = [`${manifest.label} ${manifest.view} ${manifest.mode} ${manifest.width}x${manifest.height}: mean p50 p95 p99 share>2`];
    for (const [pose, byRegion] of Object.entries(result)) {
        for (const [name, stats] of Object.entries(byRegion)) {
            lines.push(`${pose.padEnd(6)} ${name.padEnd(11)} ${stats.mean.toFixed(3).padStart(7)} ${stats.p50.toFixed(2).padStart(6)} ` +
                `${stats.p95.toFixed(2).padStart(6)} ${stats.p99.toFixed(2).padStart(6)} ${(100 * stats.unstableShare).toFixed(1).padStart(5)}%`);
        }
    }
    return lines.join("\n");
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const [directory, logPath] = process.argv.slice(2);
    if (!directory || !logPath) {
        console.error("Usage: node analyzePortalStability.mjs <capture directory> <playground log>");
        process.exit(2);
    }
    const manifest = readManifest(fs.readFileSync(logPath, "utf8"));
    const result = analyzeCaptures(directory, manifest);
    fs.writeFileSync(path.join(directory, `stability-${manifest.label}-${manifest.view}-${manifest.mode}.json`), JSON.stringify({ manifest, result }, null, 1));
    console.log(formatTable(manifest, result));
}
