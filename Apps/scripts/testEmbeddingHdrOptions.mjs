import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = fileURLToPath(new URL("../../", import.meta.url));
const read = (path) => readFileSync(join(root, path), "utf8");

test("HDR defaults off and remains independent of MSAA/debug options", () => {
    const directory = mkdtempSync(join(tmpdir(), "embedding-hdr-"));
    try {
        const source = join(directory, "options.cpp");
        const executable = join(directory, "options");
        writeFileSync(source, `#include <Babylon/Embedding/RuntimeOptions.h>
int main() {
    Babylon::Embedding::RuntimeOptions options{};
    if (options.hdr10 || options.msaaSamples != 4 || options.enableDebugger) return 1;
    options.hdr10 = true;
    if (!options.hdr10 || options.msaaSamples != 4 || options.enableDebugger) return 2;
    options.hdr10 = false;
    return options.hdr10 ? 3 : 0;
}
`);
        const compile = spawnSync("clang++", ["-std=c++17", "-DBABYLON_NATIVE_PLUGIN_SHADERCACHE=0", "-I", join(root, "Embedding/Include/Shared"), source, "-o", executable], { encoding: "utf8", timeout: 30000 });
        assert.equal(compile.status, 0, compile.stderr || String(compile.error));
        const result = spawnSync(executable, [], { encoding: "utf8", timeout: 5000 });
        assert.equal(result.status, 0, result.stderr || String(result.error));
    } finally {
        rmSync(directory, { recursive: true, force: true });
    }
});

test("first view attach forwards the runtime HDR option to graphics", () => {
    assert.match(read("Embedding/Source/View.cpp"), /config\.Hdr10 = m_runtime\.m_options\.hdr10;/);
});

test("Apple runtime exposes and forwards HDR instead of hardcoding it", () => {
    assert.match(read("Embedding/Include/Platform/Apple/Babylon/Embedding/Apple/BNRuntime.h"), /@property \(nonatomic\) BOOL hdr10;/);
    assert.match(read("Embedding/Apple/Source/BNRuntime.mm"), /options\.hdr10 = runtimeOptions\.hdr10 \? true : false;/);
});

test("Apple Playground selects HDR before constructing the runtime", () => {
    const source = read("Apps/Playground/iOS/AppDelegate.swift");
    const select = source.indexOf('runtimeOptions.hdr10 = CommandLine.arguments.contains("--hdr10")');
    assert.ok(select >= 0);
    assert.ok(select < source.indexOf("BNRuntime(options: runtimeOptions)"));
    assert.match(read("Apps/Playground/AppleShared/PlaygroundBootstrap.mm"), /js\.Set\("hdr10", Napi::Boolean::New\(env, options\.Hdr10\)\);/);
});

test("macOS Playground forwards HDR and tags the layer with the canvas transfer", () => {
    const source = read("Apps/Playground/macOS/ViewController.mm");
    const select = source.indexOf("options.hdr10 = playgroundOptions.Hdr10 ? YES : NO;");
    assert.ok(select >= 0);
    assert.ok(select < source.indexOf("[[BNRuntime alloc] initWithOptions:options]"));
    // The canvas is sRGB encoded even when it is float: a linear or HDR10 tag lifts the midtones.
    assert.match(source, /CGColorSpaceCreateWithName\(kCGColorSpaceExtendedSRGB\)/);
    assert.doesNotMatch(source, /kCGColorSpaceExtendedLinear|HDR10MetadataWith/);
});
