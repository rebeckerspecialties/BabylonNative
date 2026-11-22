const { test } = require("node:test");
const assert = require("node:assert/strict");
const { needsSeparateEyeClear } = require("./eyeClear.cjs");
const target = (texture, layerIndex) => ({ layerIndex, getInternalTexture: () => texture && ({ _hardwareTexture: { underlyingResource: texture } }) });
test("only independently cleared eye subresources need a second clear", () => {
    const texture = {}, left = target(texture, 0);
    assert.equal(needsSeparateEyeClear(left, left), false);
    assert.equal(needsSeparateEyeClear(left, target(texture)), false);
    assert.equal(needsSeparateEyeClear(left, target(texture, 1)), true);
    assert.equal(needsSeparateEyeClear(left, target({}, 0)), true);
    assert.equal(needsSeparateEyeClear(null, left), false);
    assert.equal(needsSeparateEyeClear(left, target(null, 1)), false);
});
