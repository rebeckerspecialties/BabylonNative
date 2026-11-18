import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const source = process.env.BABYLON_WGPU_SHADER_SOURCE;
assert.ok(source, "Set BABYLON_WGPU_SHADER_SOURCE to the pinned wgpu checkout (no network access is required).");
const cmake = readFileSync(join(root, "CMakeLists.txt"), "utf8");
const revision = cmake.match(/set\(BABYLON_NATIVE_WGPU_SHADER_REV ([a-f0-9]{40})\)/)[1];
const patches = ["0001-metal-snorm16-vertex-unpacking.patch", "0002-explicit-shader-workspace.patch"];

test("pinned shader patches apply once, reapply safely, and reject conflicting source", () => {
    const fixture = mkdtempSync(join(tmpdir(), "babylon-naga-patch-"));
    try {
        // Use only the original files touched by the maintained patches.
        for (const name of patches) {
            const patch = readFileSync(join(root, "Patches/wgpu", name), "utf8");
            for (const [, file] of patch.matchAll(/^diff --git a\/(\S+) b\//gm)) {
                const target = join(fixture, file);
                mkdirSync(dirname(target), { recursive: true });
                writeFileSync(target, execFileSync("git", ["show", `${revision}:${file}`], { cwd: source }));
            }
        }
        const apply = name => spawnSync("cmake", [
            `-DPATCH_FILE=${join(root, "Patches/wgpu", name)}`,
            "-P", join(root, "Patches/apply_patch_if_needed.cmake"),
        ], { cwd: fixture, encoding: "utf8" });
        for (const name of patches) {
            const first = apply(name);
            assert.equal(first.status, 0, first.stderr);
            const repeated = apply(name);
            assert.equal(repeated.status, 0, repeated.stderr);
        }
        const writer = join(fixture, "naga/src/back/msl/writer.rs");
        const patched = readFileSync(writer, "utf8");
        assert.match(patched, /as_type<short>/);
        writeFileSync(writer, patched.replaceAll("32767.0", "32766.0"));
        const drift = apply(patches[0]);
        assert.notEqual(drift.status, 0);
        assert.match(drift.stderr, /neither applies cleanly nor appears to be already applied/);
        assert.equal(readFileSync(writer, "utf8"), patched.replaceAll("32767.0", "32766.0"));
    } finally {
        rmSync(fixture, { recursive: true, force: true });
    }
});

test("Cargo's wgpu dependency and shader compiler use the same revision", () => {
    const cargo = readFileSync(join(root, "Cargo.toml"), "utf8");
    const wgpu = [...cargo.matchAll(/^wgpu = \{ git = "https:\/\/github.com\/gfx-rs\/wgpu.git", rev = "([a-f0-9]+)"/gm)];
    assert.equal(wgpu.length, 2);
    for (const entry of wgpu) assert.equal(entry[1], revision);
    assert.match(cmake, /git apply --reverse --check/);
});
