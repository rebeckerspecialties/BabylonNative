import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const generators = ["Ninja", "Ninja Multi-Config"];
if (process.platform === "darwin") {
    generators.push("Xcode");
}

for (const generator of generators) {
    for (const config of ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"]) {
        test(`${generator} builds only the ${config} Cargo profile and tracks transitive inputs`, async () => {
            const directory = mkdtempSync(join(tmpdir(), "babylon-rust-profile-"));
            try {
                writeFileSync(join(directory, "Cargo.toml"), "[package]\nname = \"fixture\"\nversion = \"0.0.0\"\n");
                const dependency = join(directory, "dependency.rs");
                writeFileSync(dependency, "first version");
                writeFileSync(join(directory, "record.mjs"), `
import { appendFileSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
const args = process.argv.slice(2);
const release = args.includes("--release");
const target = args[args.indexOf("--target-dir") + 1];
const output = join(target, release ? "release" : "debug");
mkdirSync(output, { recursive: true });
writeFileSync(join(output, "libfixture.a"), "fixture");
writeFileSync(join(output, "libfixture.d"), join(output, "libfixture.a") + ": ${dependency}\\n");
appendFileSync(join(target, "calls.jsonl"), JSON.stringify({ args, env: process.env }) + "\\n");
`);
                writeFileSync(join(directory, "CMakeLists.txt"), `
cmake_minimum_required(VERSION 3.21)
project(RustProfileFixture LANGUAGES C)
set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel)
set(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS ON)
include("${root}/Core/Rust/BabylonRustCrate.cmake")
set(BABYLON_RUST_CARGO_COMMAND "${process.execPath}" "${directory}/record.mjs")
set(BABYLON_RUST_TARGET_TRIPLE "")
set(BABYLON_RUST_TARGET_FLAG "")
babylon_add_rust_static_crate(fixture CRATE_DIR "${directory}" LIB_NAME fixture
    CARGO_ENV "FIXTURE_ENV=preserved value" CARGO_ARGS --features fixture)
file(GENERATE OUTPUT "${directory}/build/selected-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:fixture>")
`);
                const build = join(directory, "build");
                execFileSync("cmake", ["-S", directory, "-B", build, "-G", generator, `-DCMAKE_BUILD_TYPE=${config}`], { stdio: "pipe" });
                execFileSync("cmake", ["--build", build, "--config", config, "--target", "fixture_rust"], { stdio: "pipe" });
                const calls = readFileSync(join(build, "cargo/calls.jsonl"), "utf8").trim().split("\n").map(JSON.parse);
                assert.equal(calls.length, 1);
                const { args, env } = calls[0];
                const release = config !== "Debug";
                assert.equal(args.includes("--release"), release);
                assert.equal(args.includes("build-std=std,panic_abort"), release);
                assert.equal(env.CARGO_PROFILE_RELEASE_CODEGEN_UNITS, release ? "1" : undefined);
                assert.equal(env.CARGO_PROFILE_RELEASE_PANIC, release ? "abort" : undefined);
                assert.equal(env.FIXTURE_ENV, "preserved value");
                assert.deepEqual(args.slice(-2), ["--features", "fixture"]);
                const flags = Object.entries(env).filter(([key]) => key.endsWith("RUSTFLAGS")).map(([, value]) => value).join(" ");
                assert.equal(flags.includes("linker-plugin-lto"), release);
                const profile = release ? "release" : "debug";
                assert.equal(readFileSync(join(build, `selected-${config}.txt`), "utf8"), join(build, "cargo", profile, "libfixture.a"));
                execFileSync("cmake", ["--build", build, "--config", config, "--target", "fixture_rust"], { stdio: "pipe" });
                assert.equal(readFileSync(join(build, "cargo/calls.jsonl"), "utf8").trim().split("\n").length, 1);
                await new Promise(resolve => setTimeout(resolve, 1100));
                writeFileSync(dependency, "second version");
                execFileSync("cmake", ["--build", build, "--config", config, "--target", "fixture_rust"], { stdio: "pipe" });
                assert.equal(readFileSync(join(build, "cargo/calls.jsonl"), "utf8").trim().split("\n").length, 2);
            } finally {
                rmSync(directory, { recursive: true, force: true });
            }
        });
    }
}
