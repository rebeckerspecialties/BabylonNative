import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";

const source = readFileSync(new URL("../../Plugins/NativeWebGPU/CMakeLists.txt", import.meta.url), "utf8");
const condition = source.match(/^if\((BABYLON_NATIVE_BUILD_WEBGPU_TESTS[^\n]+)\)$/m)?.[1];
assert(condition, "NativeWebGPU host-test platform condition is missing");

for (const [platform, enabled, expected] of [
    ["macOS", ["APPLE", "UNIX"], true],
    ["Linux", ["UNIX"], true],
    ["Win32", ["WIN32"], true],
    ["iOS", ["APPLE", "UNIX", "IOS"], false],
    ["visionOS", ["APPLE", "UNIX", "VISIONOS"], false],
    ["tvOS", ["APPLE", "UNIX", "TVOS"], false],
    ["Android", ["UNIX", "ANDROID"], false],
    ["UWP", ["WIN32", "WINDOWS_STORE"], false],
    ["unknown", [], false],
]) {
    for (const requested of [false, true]) {
        test(`WebGPU host tests: ${platform}, requested=${requested}`, () => {
            const directory = mkdtempSync(join(tmpdir(), "webgpu-host-platform-"));
            try {
                const variables = ["APPLE", "UNIX", "WIN32", "IOS", "VISIONOS", "TVOS", "ANDROID", "WINDOWS_STORE"];
                const script = variables.map(name => `set(${name} ${enabled.includes(name) ? "ON" : "OFF"})`).join("\n");
                const path = join(directory, "platform.cmake");
                writeFileSync(path, `${script}\nset(BABYLON_NATIVE_BUILD_WEBGPU_TESTS ${requested ? "ON" : "OFF"})\nif(${condition})\nmessage("enabled")\nelse()\nmessage("disabled")\nendif()\n`);
                const result = spawnSync("cmake", ["-P", path], { encoding: "utf8", timeout: 10000 });
                assert.equal(result.status, 0, result.stderr || String(result.error));
                assert.equal(result.stderr.trim(), requested && expected ? "enabled" : "disabled");
            } finally {
                rmSync(directory, { recursive: true, force: true });
            }
        });
    }
}
