import assert from "node:assert/strict";
import test from "node:test";
import zlib from "node:zlib";
import { decodePngLuma, readManifest, subPixelPopping, wholePixelInstability } from "./analyzePortalStability.mjs";

function crc32(buffer) {
    let crc = 0xffffffff;
    for (const byte of buffer) {
        crc ^= byte;
        for (let bit = 0; bit < 8; bit++) {
            crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
        }
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, body) {
    const head = Buffer.alloc(8);
    head.writeUInt32BE(body.length, 0);
    head.write(type, 4, "latin1");
    const tail = Buffer.alloc(4);
    tail.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), body])), 0);
    return Buffer.concat([head, body, tail]);
}

// Encodes with one PNG filter type per row so every unfilter path is exercised.
function encodePng(width, height, channels, pixels, filterForRow) {
    const stride = width * channels;
    const raw = Buffer.alloc((stride + 1) * height);
    for (let y = 0; y < height; y++) {
        const filter = filterForRow(y);
        raw[y * (stride + 1)] = filter;
        for (let x = 0; x < stride; x++) {
            const value = pixels[y * stride + x];
            const left = x >= channels ? pixels[y * stride + x - channels] : 0;
            const up = y > 0 ? pixels[(y - 1) * stride + x] : 0;
            const upLeft = y > 0 && x >= channels ? pixels[(y - 1) * stride + x - channels] : 0;
            let predicted = 0;
            if (filter === 1) predicted = left;
            if (filter === 2) predicted = up;
            if (filter === 3) predicted = (left + up) >> 1;
            if (filter === 4) {
                const estimate = left + up - upLeft;
                const a = Math.abs(estimate - left);
                const b = Math.abs(estimate - up);
                const c = Math.abs(estimate - upLeft);
                predicted = a <= b && a <= c ? left : b <= c ? up : upLeft;
            }
            raw[y * (stride + 1) + 1 + x] = (value - predicted) & 0xff;
        }
    }
    const header = Buffer.alloc(13);
    header.writeUInt32BE(width, 0);
    header.writeUInt32BE(height, 4);
    header[8] = 8;
    header[9] = channels === 3 ? 2 : 6;
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk("IHDR", header), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0)),
    ]);
}

function pseudoRandom(seed) {
    let state = seed >>> 0;
    return () => {
        state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
        return state / 4294967296;
    };
}

// A scene sampled at world coordinates; frame pixel (x, y) shows world (x - dx, y + dy).
function sceneFrame(width, height, shift, extra = () => 0) {
    const luma = new Float32Array(width * height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const worldX = x - shift[0];
            const worldY = y + shift[1];
            luma[y * width + x] = 128 + 60 * Math.sin(worldX * 0.37) * Math.cos(worldY * 0.23) + extra(x, y);
        }
    }
    return { width, height, luma };
}

test("decodes every PNG filter type for RGB and RGBA", () => {
    for (const channels of [3, 4]) {
        const width = 7;
        const height = 10;
        const random = pseudoRandom(channels);
        const pixels = Uint8Array.from({ length: width * height * channels }, () => Math.floor(random() * 256));
        const decoded = decodePngLuma(encodePng(width, height, channels, pixels, (y) => y % 5));
        assert.equal(decoded.width, width);
        assert.equal(decoded.height, height);
        for (let i = 0; i < width * height; i++) {
            const expected = 0.2126 * pixels[i * channels] + 0.7152 * pixels[i * channels + 1] + 0.0722 * pixels[i * channels + 2];
            assert.ok(Math.abs(decoded.luma[i] - expected) < 1e-3);
        }
    }
});

test("rejects files that are not 8-bit non-interlaced RGB or RGBA", () => {
    assert.throws(() => decodePngLuma(Buffer.from("not a png")), /Not a PNG/);
    const png = encodePng(2, 2, 3, new Uint8Array(12), () => 0);
    png[8 + 8 + 9] = 0; // grayscale color type
    assert.throws(() => decodePngLuma(png), /Only 8-bit/);
});

test("a scene that only shifts by whole pixels is perfectly stable", () => {
    const shifts = [[0, 0], [1, 0], [2, 1], [3, 1], [4, 2], [5, 3], [6, 3], [7, 4]];
    const frames = shifts.map((shift) => sceneFrame(96, 80, shift));
    const { map, width, height } = wholePixelInstability(frames, shifts);
    assert.equal(width, 96 - 2 * 15);
    assert.equal(height, 80 - 2 * 15);
    assert.ok(Math.max(...map) < 1e-3);
});

test("a pattern fixed to the screen registers as instability", () => {
    const shifts = [[0, 0], [1, 0], [2, 1], [3, 1]];
    const frames = shifts.map((shift) => sceneFrame(64, 64, shift, (x, y) => ((x + y) & 1 ? 4 : -4)));
    const { map } = wholePixelInstability(frames, shifts);
    const mean = map.reduce((sum, value) => sum + value, 0) / map.length;
    assert.ok(mean > 2 && mean < 4.01, String(mean));
});

test("whole-pixel analysis refuses fractional shifts and mismatched frames", () => {
    const frame = sceneFrame(64, 64, [0, 0]);
    assert.throws(() => wholePixelInstability([frame, frame], [[0, 0], [0.5, 0]]), /integer shifts/);
    assert.throws(() => wholePixelInstability([frame, sceneFrame(60, 64, [0, 0])], [[0, 0], [1, 0]]), /share one size/);
});

test("smooth sub-pixel motion has no popping and a flickering pixel does", () => {
    const frames = Array.from({ length: 6 }, (_, t) => ({
        width: 32, height: 4,
        luma: Float32Array.from({ length: 128 }, (_, i) => 3 * ((i % 32) - 0.25 * t)),
    }));
    const smooth = subPixelPopping(frames);
    assert.ok(Math.max(...smooth.map) < 1e-3);
    frames.forEach((frame, t) => { frame.luma[40] += t % 2 ? 10 : 0; });
    const popping = subPixelPopping(frames);
    assert.ok(popping.map[40] > 15);
    assert.ok(popping.map[41] < 1e-3);
    assert.throws(() => subPixelPopping(frames.slice(0, 2)), /at least three/);
});

test("reads the manifest from a Playground log", () => {
    const manifest = readManifest('noise\nstability:manifest={"label":"a","view":"ao","mode":"whole-pixel","poses":["car"],"shifts":[[0,0]]}\nmore');
    assert.deepEqual(manifest.poses, ["car"]);
    assert.throws(() => readManifest("nothing here"), /no stability manifest/);
});
