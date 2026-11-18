import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const repository = fileURLToPath(new URL("../../", import.meta.url));
const root = resolve(process.env.BABYLON_BUILD_SOURCE || join(repository, "build_babylon_bundle/source"));
const ts = createRequire(join(root, "package.json"))("typescript");
const sourcePath = "packages/dev/core/src/Materials/uniformBuffer.ts";
const patch = join(repository, "Patches/BabylonJS/0006-uniform-loop-invariant.patch");

function loadSources(source = readFileSync(join(root, sourcePath))) {
    const directory = mkdtempSync(join(tmpdir(), "babylon-uniform-loop-"));
    try {
        const file = join(directory, sourcePath);
        mkdirSync(dirname(file), { recursive: true });
        writeFileSync(file, source);
        const apply = (...args) => execFileSync("git", ["apply", "--whitespace=error-all", ...args, patch], { cwd: directory, stdio: "pipe" });
        try { apply("--check"); } catch { apply("--reverse", "--check"); apply("--reverse"); }
        const original = readFileSync(file, "utf8");
        apply();
        const optimized = readFileSync(file, "utf8");
        apply("--reverse");
        assert.equal(readFileSync(file, "utf8"), original);
        return { original, optimized };
    } finally {
        rmSync(directory, { recursive: true, force: true });
    }
}

function loadMethod(source) {
    const ast = ts.createSourceFile(sourcePath, source, ts.ScriptTarget.Latest, true);
    const declaration = ast.statements.find(node => ts.isClassDeclaration(node) && node.name?.text === "UniformBuffer");
    const method = declaration.members.find(node => ts.isMethodDeclaration(node) && node.name.getText(ast) === "updateUniform");
    assert.ok(method);
    const result = ts.transpileModule(`class Harness { ${method.getText(ast)} }`, { compilerOptions: { target: ts.ScriptTarget.ES2022 } });
    return vm.runInNewContext(`${result.outputText}\nHarness.prototype.updateUniform`, { Logger: { Error(message) { throw Error(message); } } });
}

const sources = loadSources();
const original = loadMethod(sources.original);
const optimized = loadMethod(sources.optimized);

function harness(method, { size = 16, hardCheck = false, dynamic = false, copyOnWrite = false, existing = true } = {}) {
    const h = {
        _uniformLocations: existing ? { value: 4 } : {},
        _bufferData: new Float32Array(64),
        _buffer: existing ? {} : null,
        _dynamic: dynamic,
        _createBufferOnWrite: copyOnWrite,
        _needSync: false,
        _dataGeneration: 0,
        hardCheck, reads: 0, frames: 0, clones: [], creates: 0,
        _checkNewFrame() { this.frames++; },
        addUniform(name) { this._uniformLocations[name] = 4; },
        create() { this.creates++; this._buffer = {}; },
        _createNewBuffer() {
            this.clones.push(this._bufferData);
            this._bufferData = this._bufferData.slice();
            this._createBufferOnWrite = false;
        },
        update(data, count = size) { method.call(this, "value", data, count); },
    };
    h._engine = { _features: { get uniformBufferHardCheckMatrix() { h.reads++; return h.hardCheck; } } };
    return h;
}

function state(h) {
    return { bytes: Buffer.from(h._bufferData.buffer), generation: h._dataGeneration, sync: h._needSync,
        clones: h.clones.map(data => Buffer.from(data.buffer)), frames: h.frames, creates: h.creates };
}

test("patch round-trips from either source side", () => {
    assert.notEqual(sources.original, sources.optimized);
    assert.deepEqual(loadSources(sources.optimized), sources);
});

for (const size of [0, 1, 4, 16, 32]) {
    for (const hardCheck of [false, true]) {
        test(`same-frame repeated/changed values: size=${size}, matrix check=${hardCheck}`, () => {
            for (const dynamic of [false, true]) {
                for (const copyOnWrite of [false, true]) {
                    const a = harness(original, { size, hardCheck, dynamic, copyOnWrite });
                    const b = harness(optimized, { size, hardCheck, dynamic, copyOnWrite });
                    for (const data of [new Float32Array(size), Array.from({ length: size }, (_, i) => i / 3),
                        Array.from({ length: size }, (_, i) => i / 3), Array.from({ length: size }, (_, i) => i % 2 ? NaN : -0)]) {
                        a.update(data); b.update(data);
                        assert.deepEqual(state(b), state(a));
                    }
                    if (copyOnWrite && b.clones.length) assert.ok(b.clones[0].every(value => value === 0));
                }
            }
        });
    }
}

test("policy changes between calls and independent view buffers remain visible", () => {
    const controls = [harness(original), harness(original)];
    const candidates = [harness(optimized), harness(optimized)];
    for (const [view, hardCheck, value] of [[0, false, 1], [1, true, 2], [0, true, 1], [0, false, 1], [1, false, 3]]) {
        for (const h of [controls[view], candidates[view]]) {
            h.hardCheck = hardCheck;
            h.update(new Float32Array(16).fill(value));
        }
        assert.deepEqual(state(candidates[view]), state(controls[view]));
    }
});

test("reads the matrix policy once per static update, not once per element", () => {
    const a = harness(original);
    const b = harness(optimized);
    a.update(new Float32Array(16)); b.update(new Float32Array(16));
    assert.equal(a.reads, 16);
    assert.equal(b.reads, 1);
    for (const options of [{ size: 0 }, { size: 4 }, { dynamic: true }]) {
        const h = harness(optimized, options);
        h.update(new Float32Array(16));
        assert.equal(h.reads, 0);
    }
});

test("creation, missing-uniform errors, and dirty-state retention are unchanged", () => {
    for (const existing of [false, true]) {
        const a = harness(original, { existing });
        const b = harness(optimized, { existing });
        for (const h of [a, b]) {
            h._needSync = true;
            h.update(new Float32Array(16));
        }
        assert.deepEqual(state(b), state(a));
        delete a._uniformLocations.value;
        delete b._uniformLocations.value;
        assert.throws(() => a.update([]), /Cannot add an uniform after UBO has been created. uniformName=value/);
        assert.throws(() => b.update([]), /Cannot add an uniform after UBO has been created. uniformName=value/);
    }
});
