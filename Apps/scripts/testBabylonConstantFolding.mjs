import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import test from "node:test";
import vm from "node:vm";

const root = resolve(process.env.BABYLON_BUILD_SOURCE || new URL("../../build_babylon_bundle/source", import.meta.url).pathname);
const require = createRequire(`${root}/package.json`);
const { minify } = require("terser");
const options = { compress: { passes: 3, dead_code: true }, mangle: { reserved: ["_"] }, format: { comments: false } };

test("folds immutable application constants and eliminates the unused branch", async () => {
    const { code } = await minify(`(() => { const config = { legacy: false, scale: 2 }; if (config.legacy) globalThis.result = "UNUSED_LEGACY_BRANCH"; else globalThis.result = 4 * config.scale; })();`, options);
    assert.doesNotMatch(code, /UNUSED_LEGACY_BRANCH/);
    const context = vm.createContext({});
    vm.runInContext(code, context);
    assert.equal(context.result, 8);
});

test("runtime capabilities, mutable settings, call side effects and async boundaries are preserved", async () => {
    const { code } = await minify(`globalThis.run = async (engine, scene) => { const caps = engine.getCaps(); await Promise.resolve(); return [caps.textureFloatRender, caps.maxTextureSize, scene.useMaterialMeshMap, engine.snapshotRendering]; };`, options);
    const context = vm.createContext({});
    vm.runInContext(code, context);
    let calls = 0;
    const caps = { textureFloatRender: false, maxTextureSize: 2048 };
    const engine = { getCaps() { calls++; return caps; }, snapshotRendering: false };
    const scene = { useMaterialMeshMap: false };
    assert.deepEqual(Array.from(await context.run(engine, scene)), [false, 2048, false, false]);
    caps.textureFloatRender = true;
    caps.maxTextureSize = 8192;
    scene.useMaterialMeshMap = true;
    engine.snapshotRendering = true;
    assert.deepEqual(Array.from(await context.run(engine, scene)), [true, 8192, true, true]);
    assert.equal(calls, 2);
});

test("short-circuit folding retains calls and their exceptions", async () => {
    const { code } = await minify(`globalThis.run = fn => [fn() || true, fn() && false];`, options);
    const context = vm.createContext({});
    vm.runInContext(code, context);
    let calls = 0;
    assert.deepEqual(Array.from(context.run(() => { calls++; return true; })), [true, false]);
    assert.equal(calls, 2);
    const failure = new Error("getter side effect");
    assert.throws(() => context.run(() => { throw failure; }), error => error === failure);
});
