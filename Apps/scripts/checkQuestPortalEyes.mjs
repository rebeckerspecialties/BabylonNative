// Untimed, post-placement compositor captures. This checks the doorway, not AO or physical registration.
import { spawn } from "node:child_process";
import { once } from "node:events";
import { pathToFileURL } from "node:url";

const width = 640, height = 320, frameBytes = width * height * 3;

export function doorwayPixels(rgb) {
    if (rgb.length !== frameBytes) throw new Error("Incomplete stereo frame");
    const counts = [0, 0];
    for (let y = height / 8; y < height * 7 / 8; y++) {
        for (let x = 0; x < width; x++) {
            const i = (y * width + x) * 3;
            const r = rgb[i], g = rgb[i + 1], b = rgb[i + 2];
            // Unlike the blue controller ray, the doorway is bright in green as well as blue.
            if (g > 165 && b > 175 && g - r > 32 && b - r > 42 && b - g < 37) counts[Number(x >= width / 2)]++;
        }
    }
    return counts;
}

export async function checkFrames(chunks) {
    const frame = Buffer.allocUnsafe(frameBytes);
    let offset = 0;
    const result = { frames: 0, leftMissing: 0, rightMissing: 0, bothMissing: 0, firstBadFrame: null, minimumPixels: [Infinity, Infinity] };
    const coverage = [[], []];
    for await (const chunk of chunks) {
        for (let pos = 0; pos < chunk.length;) {
            const n = Math.min(frameBytes - offset, chunk.length - pos);
            chunk.copy(frame, offset, pos, pos + n);
            pos += n;
            offset += n;
            if (offset !== frameBytes) continue;
            const [l, r] = doorwayPixels(frame);
            coverage[0].push(l);
            coverage[1].push(r);
            result.minimumPixels[0] = Math.min(result.minimumPixels[0], l);
            result.minimumPixels[1] = Math.min(result.minimumPixels[1], r);
            if (l < 250 || r < 250) {
                result.firstBadFrame ??= result.frames;
                if (l < 250 && r < 250) result.bothMissing++;
                else if (l < 250) result.leftMissing++;
                else result.rightMissing++;
            }
            result.frames++;
            offset = 0;
        }
    }
    if (offset !== 0 || !result.frames) throw new Error("Missing or truncated stereo capture");
    // Only for a stationary headset after placement. Compare each eye to its own typical area.
    result.partial = coverage.map(eye => {
        const threshold = eye.toSorted((a, b) => a - b)[Math.floor(eye.length / 2)] * 0.75;
        return eye.reduce((count, pixels, index) => {
            if (pixels < 250 || pixels >= threshold) return count;
            result.firstBadFrame = Math.min(result.firstBadFrame ?? index, index);
            return count + 1;
        }, 0);
    });
    return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const file = process.argv[2];
    if (!file) throw new Error("Expected a side-by-side compositor PNG or video");
    // Raw output has no timestamps. Enumerate decoded frames without synthesizing or dropping any.
    const decoder = spawn("ffmpeg", ["-v", "error", "-i", file, "-vf", `scale=${width}:${height},settb=1/30,setpts=N`, "-enc_time_base", "1:30", "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"], { stdio: ["ignore", "pipe", "inherit"] });
    const [result, [exitCode]] = await Promise.all([checkFrames(decoder.stdout), once(decoder, "close")]);
    if (exitCode !== 0) throw new Error(`FFmpeg exited with status ${exitCode}`);
    console.log(JSON.stringify(result));
    if (result.firstBadFrame !== null) process.exitCode = 1;
}
