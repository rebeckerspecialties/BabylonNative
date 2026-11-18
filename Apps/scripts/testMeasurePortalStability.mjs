import assert from "node:assert/strict";
import test from "node:test";
import { buildScriptList, parseArguments } from "./measurePortalStability.mjs";

test("names the required arguments that are missing", () => {
    assert.throws(() => parseArguments(["--playground", "Playground"]), /^Error: Missing --out, --label\. Usage:/);
    assert.throws(() => parseArguments([]), /Missing --playground, --out, --label\./);
    assert.throws(() => parseArguments(["--playground", "p", "--out", "o", "--label", "Candidate"]), /--label must be lowercase letters, digits and hyphens\./);
    assert.throws(() => parseArguments(["--playground", "p", "--out", "o", "--label", "a", "--views", "depth"]), /Invalid --views/);
});

test("preludes load after the configuration and before the workload", () => {
    const options = parseArguments(["--playground", "p", "--out", "o", "--label", "a", "--prelude", "/tmp/layout.js"]);
    assert.deepEqual(options.views, ["final", "ao"]);
    assert.deepEqual(options.modes, ["whole-pixel"]);
    const scripts = buildScriptList(options, "/tmp/configuration.js");
    assert.equal(scripts[0], "file:///tmp/configuration.js");
    assert.equal(scripts[1], "file:///tmp/layout.js");
    assert.match(scripts[2], /native_xr_portal_hillvalley_stress\.js$/);
    assert.match(scripts[6], /native_xr_portal_gtvbao_cosine_validation\.js$/);
});
