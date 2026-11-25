// Untimed desk placement only. Tests visibility of the emissive doorway, not AO or floor lock.
import { spawnSync } from "node:child_process";

const [file] = process.argv.slice(2);
if (!file) throw new Error("Expected a side-by-side Quest compositor PNG.");
const width = 640, height = 320;
const decoded = spawnSync("ffmpeg", ["-v", "error", "-i", file, "-frames:v", "1", "-vf", `scale=${width}:${height}`, "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"]);
if (decoded.error) throw decoded.error;
if (decoded.status !== 0 || decoded.stdout.length !== width * height * 3) throw new Error("Cannot decode the stereo capture: " + decoded.stderr);
const counts = [0, 0];
for (let y = height / 8; y < height * 7 / 8; y++) {
    for (let x = 0; x < width; x++) {
        const i = (y * width + x) * 3;
        const r = decoded.stdout[i], g = decoded.stdout[i + 1], b = decoded.stdout[i + 2];
        // The cyan doorway, unlike the blue controller ray, is bright in green as well as blue.
        if (g > 165 && b > 175 && g - r > 32 && b - r > 42 && b - g < 37) counts[Number(x >= width / 2)]++;
    }
}
console.log(`Doorway pixels: left=${counts[0]} right=${counts[1]}`);
if (counts.some(n => n < 250)) process.exitCode = 1;
