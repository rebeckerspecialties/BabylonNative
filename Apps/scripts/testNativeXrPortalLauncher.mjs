import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

test("the launch preset is explicit, and only first-generation Quest defaults to the geometry control", t => {
    const dir = mkdtempSync(join(tmpdir(), "portal-launch-"));
    t.after(() => rmSync(dir, { recursive: true, force: true }));
    writeFileSync(join(dir, "adb"), '#!/bin/sh\nif [ "$4" = getprop ]; then printf "%s\\r\\n" "$MODEL"; else printf "%s\\n" "$@"; fi\n', { mode: 0o755 });
    writeFileSync(join(dir, "xcrun"), '#!/bin/sh\nprintf "%s\\n" "$@"\n', { mode: 0o755 });
    const run = (model, ...args) => spawnSync("sh", [fileURLToPath(new URL("./runPortalAr.sh", import.meta.url)), ...args], {
        encoding: "utf8", env: { ...process.env, MODEL: model, PATH: dir + ":" + process.env.PATH },
    });
    const quest = run("monterey", "adb:test");
    assert.equal(quest.status, 0);
    assert.match(quest.stderr, /gt-vbao-geometry/);
    assert.match(quest.stdout, /native_xr_babylon_portal_demo\.js/);
    assert.match(quest.stdout, /gtvbao_cosine_validation/);
    assert.doesNotMatch(quest.stdout, /profile_prepass/);
    for (const result of [run("monterey", "adb:test", "gt-vbao"), run("other", "adb:test"), run("", "ios-udid")]) {
        assert.equal(result.status, 0);
        assert.match(result.stdout, /native_xr_portal_gtvbao_cosine_validation/);
        assert.match(result.stdout, /native_xr_portal_profile_prepass/);
    }
    assert.match(run("monterey", "adb:test", "ssao2").stdout, /native_xr_portal_profile_prepass/);
    assert.equal(run("monterey", "adb:test", "invalid").status, 2);
    const baked = run("unused", "print:quest1");
    assert.equal(baked.status, 0);
    assert.ok(run("monterey", "adb:test").stdout.includes("'" + baked.stdout.trim() + "'"));
});
