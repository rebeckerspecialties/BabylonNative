import { test } from "node:test";
import assert from "node:assert/strict";
import { checkFrames } from "./checkQuestPortalEyes.mjs";

function eyeFrame(left, right) {
    const bytes = Buffer.alloc(640 * 320 * 3);
    for (const [eye, on] of [left, right].entries()) {
        if (!on) continue;
        for (let x = 0; x < (on === true ? 250 : on); x++) {
            const offset = ((160 + Math.floor(x / 320)) * 640 + eye * 320 + x % 320) * 3;
            bytes[offset + 1] = bytes[offset + 2] = 220;
        }
    }
    return bytes;
}

test("accept a single stereo image at the inclusive pixel threshold", async () => {
    assert.equal((await checkFrames([eyeFrame(true, true)])).firstBadFrame, null);
});

test("detect a dropout between good frames, through arbitrary decoder chunk boundaries", async () => {
    const video = Buffer.concat([[true,true], [true,false], [false,true], [false,false], [true,true]].map(pair => eyeFrame(...pair)));
    const result = await checkFrames([video.subarray(0, 17), video.subarray(17, video.length-2), video.subarray(-2)]);
    assert.deepEqual(result, { frames: 5, leftMissing: 1, rightMissing: 1, bothMissing: 1, firstBadFrame: 1, minimumPixels: [0,0], partial: [0, 0] });
});

test("reject a partial eye even when it exceeds the visibility threshold", async () => {
    const result = await checkFrames([eyeFrame(800, 600), eyeFrame(800, 300), eyeFrame(800, 600)]);
    assert.deepEqual(result.partial, [0, 1]);
    assert.equal(result.firstBadFrame, 1);
});

test("reject empty and partial captures even after a complete good frame", async () => {
    await assert.rejects(checkFrames([]), /truncated/);
    await assert.rejects(checkFrames([eyeFrame(true,true), Buffer.alloc(1)]), /truncated/);
});

test("centered capture rejects additional jambs on either eye even when coverage increases", async () => {
    const make = (extra) => {
        const rgb = Buffer.alloc(640 * 320 * 3);
        for (const eye of [0, 1]) {
            for (const x0 of eye === extra ? [60, 125, 198] : [60, 198]) {
                for (let y = 92; y < 251; y++) {
                    for (let x = x0; x < x0 + 14; x++) {
                        const i = 3 * (640 * y + eye * 320 + x);
                        rgb[i] = x - x0 < 4 ? 32 : 255;
                        rgb[i + 1] = rgb[i + 2] = 255;
                    }
                }
            }
        }
        return rgb;
    };
    for (const extra of [0, 1]) {
        const frames = [make(-1), make(extra), make(-1)];
        assert.equal((await checkFrames(frames)).firstBadFrame, null);
        const checked = await checkFrames(frames, true);
        assert.equal(checked.firstBadFrame, 1);
        assert.deepEqual(checked.badJambCounts, extra ? [0, 1] : [1, 0]);
    }
});
