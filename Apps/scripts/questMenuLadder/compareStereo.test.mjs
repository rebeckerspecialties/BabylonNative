import { test } from "node:test";
import assert from "node:assert/strict";
import { compareStereo } from "./compareStereo.mjs";

const eyes = [[[1, 1], [0, 2047]], [[0, 1], [1, 1], [0, 2046]]];
const log = (mv, pixels = eyes) => "Menu stereo readback " + JSON.stringify(pixels) + "\nMenu stereo marker PASS " + JSON.stringify({multiview:mv});
test("compares both complete eye readbacks against a distinct two-pass reference", () => {
    assert.equal(compareStereo(log(false), log(true)), 16384);
});
test("rejects duplicated eyes, swapped eyes, a short readback and a silent fallback", () => {
    for (const other of [log(true, [eyes[0], eyes[0]]), log(true, eyes.slice().reverse()), log(true, [[[1, 1]], eyes[1]]), log(false)]) {
        assert.throws(() => compareStereo(log(false), other));
    }
});
test("rejects a second render, validation failures and truncated records", () => {
    for (const other of [log(true) + "\n" + log(true), log(true) + "\nMenu GPU validation", "Menu stereo marker PASS {}"]) {
        assert.throws(() => compareStereo(log(false), other));
    }
});
