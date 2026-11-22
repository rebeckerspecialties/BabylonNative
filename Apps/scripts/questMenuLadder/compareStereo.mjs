import { readFileSync } from "node:fs";
import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

function decode(log, multiview) {
    const records = log.split("\n");
    const one = (label) => {
        const lines = records.filter(line => line.includes(label));
        assert.equal(lines.length, 1, label);
        return JSON.parse(lines[0].split(label)[1].trim());
    };
    assert.equal(one("Menu stereo marker PASS").multiview, multiview);
    assert.ok(!records.some(l => /Menu GPU validation|Menu ladder FAIL/.test(l)), "GPU or scene error");
    const eyes = one("Menu stereo readback");
    assert.equal(eyes.length, 2);
    const decoded = eyes.map(runs => {
        const pixels = new Uint32Array(128 * 16);
        let count = 0;
        for (const [pixel, n] of runs) {
            assert.ok(Number.isInteger(pixel) && pixel >= 0 && pixel <= 0xffffffff);
            assert.ok(Number.isInteger(n) && n > 0 && n <= pixels.length - count);
            pixels.fill(pixel, count, count + n);
            count += n;
        }
        assert.equal(count, pixels.length);
        return pixels;
    });
    assert.notDeepEqual(decoded[0], decoded[1], "Identical eyes cannot establish stereo");
    return decoded;
}

export function compareStereo(reference, candidate) {
    const a = decode(reference, false);
    const b = decode(candidate, true);
    for (let eye = 0; eye < a.length; eye++) {
        assert.deepEqual(b[eye], a[eye], "Different pixels in eye " + eye);
    }
    return a[0].byteLength + a[1].byteLength;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [a, b] = process.argv.slice(2);
    console.log(compareStereo(readFileSync(a, "utf8"), readFileSync(b, "utf8")), "matching stereo bytes");
}
