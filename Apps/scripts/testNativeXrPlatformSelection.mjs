import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";

const source = readFileSync(new URL("../../CMakeLists.txt", import.meta.url), "utf8");
const selection = source.match(/if\(TVOS\)\n\s+set\(BABYLON_NATIVE_PLUGIN_NATIVEXR[\s\S]*?endif\(\)/)?.[0];
assert.ok(selection, "NativeXR platform selection is missing");

test("Apple host only installs the XR overlay when NativeXR is compiled", () => {
    const host = readFileSync(new URL("../Playground/iOS/ViewController.swift", import.meta.url), "utf8");
    const guards = [];
    let calls = 0;
    for (const line of host.split("\n")) {
        const guard = line.match(/^\s*#if\s+(.+)/);
        if (guard) guards.push(guard[1]);
        if (/^\s*#endif/.test(line)) guards.pop();
        if (line.includes("runtime.setXrView(xrView)")) {
            calls++;
            assert.ok(guards.includes("BABYLON_NATIVE_PLAYGROUND_HAS_NATIVEXR"));
        }
    }
    assert.equal(calls, 1);
});

for (const tvos of [false, true]) {
    for (const ios of [false, true]) {
        for (const previous of [false, true]) {
            test(`NativeXR selection: TVOS=${tvos}, IOS=${ios}, cached=${previous}`, () => {
                const directory = mkdtempSync(join(tmpdir(), "native-xr-platform-"));
                try {
                    const path = join(directory, "platform.cmake");
                    writeFileSync(path, `set(TVOS ${tvos ? "ON" : "OFF"})\nset(IOS ${ios ? "ON" : "OFF"})\nset(BABYLON_NATIVE_PLUGIN_NATIVEXR ${previous ? "ON" : "OFF"} CACHE BOOL "")\n${selection}\nmessage("selected=\${BABYLON_NATIVE_PLUGIN_NATIVEXR}")\n`);
                    const result = spawnSync("cmake", ["-P", path], { encoding: "utf8", timeout: 10000 });
                    assert.equal(result.status, 0, result.stderr || String(result.error));
                    assert.equal(result.stderr.trim(), `selected=${tvos ? "OFF" : "ON"}`);
                } finally {
                    rmSync(directory, { recursive: true, force: true });
                }
            });
        }
    }
}
