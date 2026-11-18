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
const require = createRequire(join(root, "package.json"));
const ts = require("typescript");
const sourcePath = "packages/dev/core/src/Engines/WebGPU/webgpuRenderPassCommandStream.ts";
const fixturePath = "packages/dev/core/test/unit/Engines/WebGPU/webgpuRenderPassCommandStream.test.ts";
const patch = join(repository, "Patches/BabylonJS/0004-command-stream-inline-words.patch");

// Work only on a copy, accepting either side of the maintained patch as the supplied source.
function loadSources(source = readFileSync(join(root, sourcePath))) {
    const directory = mkdtempSync(join(tmpdir(), "babylon-command-encoding-"));
    try {
        const file = join(directory, sourcePath);
        mkdirSync(dirname(file), { recursive: true });
        writeFileSync(file, source);
        const apply = (...args) => execFileSync("git", ["apply", "--whitespace=error-all", ...args, patch], {
            cwd: directory, stdio: "pipe", timeout: 10000,
        });
        try {
            apply("--check");
        } catch {
            apply("--reverse", "--check");
            apply("--reverse");
        }
        const original = readFileSync(file, "utf8");
        apply("--check");
        apply();
        const optimized = readFileSync(file, "utf8");
        apply("--reverse", "--check");
        apply("--reverse");
        assert.equal(readFileSync(file, "utf8"), original);
        return { original, optimized };
    } finally {
        rmSync(directory, { recursive: true, force: true });
    }
}

function executeTypeScript(source, filename, imports = {}) {
    const result = ts.transpileModule(source, {
        fileName: filename,
        compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS },
        reportDiagnostics: true,
    });
    assert.deepEqual(result.diagnostics?.filter(diagnostic => diagnostic.category === ts.DiagnosticCategory.Error), []);
    const module = { exports: {} };
    const run = vm.runInNewContext(`(function(require, module, exports) { ${result.outputText}\n})`, {
        Uint32Array, Error,
    }, { filename, timeout: 5000 });
    run(name => {
        assert.ok(Object.hasOwn(imports, name), `Unexpected import: ${name}`);
        return imports[name];
    }, module, module.exports);
    return module.exports;
}

const sources = loadSources();
const constants = executeTypeScript(readFileSync(join(root, "packages/dev/core/src/Engines/WebGPU/webgpuConstants.ts"), "utf8"), "webgpuConstants.ts");
function loadEncoder(source) {
    const errors = [];
    const Logger = { Error: message => errors.push(message) };
    return {
        api: executeTypeScript(source, sourcePath, { "../../Misc/logger": { Logger }, "./webgpuConstants": constants }),
        errors,
    };
}
const original = loadEncoder(sources.original);
const optimized = loadEncoder(sources.optimized);

// Reuse the upstream fixture constructors, not a second implementation of their command layout.
// The upstream describe body is not run here; the focused Node tests below exercise both encoders.
const fixtures = executeTypeScript(readFileSync(join(root, fixturePath), "utf8") + `
export { createCommand, createNonIndexedCommand, createRecordingRenderPass, createProvider,
    createResource, createVertexBuffer, createDataBuffer, setBackendResourceId,
    getRetainedResources };`, fixturePath, {
    vitest: { describe() {}, vi: { fn: implementation => implementation } },
    "core/Engines/WebGPU/webgpuRenderPassCommandStream": original.api,
    "core/Engines/WebGPU/webgpuConstants": constants,
    "core/Misc/logger": { Logger: { Error() {} } },
});

const same = (actual, expected) => assert.deepEqual(structuredClone(actual), structuredClone(expected));
const bytes = words => Buffer.from(Uint32Array.from(words).buffer);
const retained = stream => Array.from(fixtures.getRetainedResources(stream));

function harness(encoder, capacity = 256, recordResult = true) {
    const pass = fixtures.createRecordingRenderPass(recordResult);
    const disabled = [];
    const provider = fixtures.createProvider(true, "atomic decoder rejection");
    provider.onDisabled = info => disabled.push(info);
    const stream = new encoder.api.WebGPURenderPassCommandStream({ getProvider: () => provider });
    stream._commandWords = new Uint32Array(capacity);
    return { ...pass, stream, provider, disabled };
}

function assertRecords(actual, expected) {
    same(actual, expected);
    for (let i = 0; i < actual.length; i++) {
        assert.deepEqual(bytes(actual[i].words), bytes(expected[i].words));
        assert.equal(actual[i].wordCount, actual[i].words.length);
        assert.equal(actual[i].multiDrawCallCount, 0);
        assert.equal(actual[i].multiDrawDrawCount, 0);
    }
}

function append(h, command) {
    // A zero-capacity growth regression must fail, not hang the entire test process.
    return vm.runInNewContext("stream.tryAppend(command)", { stream: h.stream, command }, { timeout: 2000 });
}

function variedCommands(renderPass, bindings = 2) {
    const commands = [];
    for (let i = 0; i < 24; i++) {
        const nonIndexed = i % 2 === 0;
        const create = nonIndexed ? fixtures.createNonIndexedCommand : fixtures.createCommand;
        commands.push(create({
            renderPass,
            pipeline: fixtures.createResource(2 ** 32 + 100 + Math.floor(i / 6)),
            bindGroups: Array.from({ length: bindings }, (_, j) => fixtures.createResource(2 ** 40 + 201 + j + (i % 3))),
            vertexBuffers: Array.from({ length: bindings }, (_, j) => j === 1 ? { effectiveBuffer: null } : fixtures.createVertexBuffer(2 ** 45 + 301 + j, 2 ** 33 + i * 16, false)),
            ...(nonIndexed ? {} : { indexBuffer: fixtures.createDataBuffer(Number.MAX_SAFE_INTEGER), indexFormat: i % 3 ? "uint32" : "uint16" }),
            count: 12 + i, instancesCount: i % 4, start: i * 12,
            indirectDrawBuffer: i % 4 >= 2 ? fixtures.createResource(2 ** 48 + 501) : null,
            indirectDrawByteOffset: 2 ** 34 + i * 20,
        }));
    }
    return commands;
}

test("patch round-trips and leaves the protocol unchanged", () => {
    assert.notEqual(sources.original, sources.optimized);
    for (const key of ["WebGPURenderPassCommandStreamProtocolVersion", "WebGPURenderPassCommandStreamMaxDrawsPerBatch", "WebGPURenderPassCommandOpcode", "WebGPURenderPassDrawKind"]) {
        same(optimized.api[key], original.api[key]);
    }
});

test("an already-patched source still reconstructs the independent original oracle", () => {
    assert.deepEqual(loadSources(sources.optimized), sources);
});

for (const capacity of [0, 1, 2, 43, 256, 4096]) {
    test(`byte-identical mixed commands with initial capacity ${capacity}`, () => {
        for (const bindings of [0, 1, 7]) {
            const control = harness(original);
            const candidate = harness(optimized, capacity);
            for (const h of [control, candidate]) {
                for (const command of variedCommands(h.renderPass, bindings)) assert.equal(append(h, command), true);
                assert.equal(h.stream.flush(), true);
                assert.equal(h.stream.flush(), false);
                assert.equal(retained(h.stream).length, 0);
                assert.ok(h.stream._pendingResources.every(resource => resource === null));
            }
            assertRecords(candidate.records, control.records);
        }
    });
}

test("reserves once per accepted draw and reuses storage after warmup", () => {
    const h = harness(optimized, 0);
    const ensure = h.stream._ensureCapacity;
    let reserves = 0;
    h.stream._ensureCapacity = function (words) { reserves++; return ensure.call(this, words); };
    const command = fixtures.createCommand({ renderPass: h.renderPass });
    for (let i = 0; i < 50; i++) assert.equal(append(h, command), true);
    assert.equal(reserves, 50);
    const storage = h.stream._commandWords;
    h.stream.flush();
    for (let i = 0; i < 50; i++) assert.equal(append(h, command), true);
    assert.equal(h.stream._commandWords, storage);
    assert.equal(reserves, 100);
    h.stream.flush();
    assert.deepEqual(bytes(h.records[0].words), bytes(h.records[1].words));
});

test("safe high resource IDs have exact low and high words", () => {
    for (const id of [1, 2 ** 32 - 1, 2 ** 32, 2 ** 32 + 1, 2 ** 48 + 123, Number.MAX_SAFE_INTEGER]) {
        const h = harness(optimized, 1);
        assert.equal(append(h, fixtures.createCommand({ renderPass: h.renderPass, pipeline: fixtures.createResource(id) })), true);
        h.stream.flush();
        const words = h.records[0].words;
        assert.equal(words[1], Number(BigInt(id) & 0xffffffffn));
        assert.equal(words[2], Number(BigInt(id) >> 32n));
    }
});

test("offset normalization and u32 coercions match the original encoder", () => {
    for (const offset of [0, -0, -1, NaN, Infinity, -Infinity, 0.5, 2 ** 32 + 3.5, Number.MAX_SAFE_INTEGER, Number.MAX_VALUE]) {
        const pair = [harness(original), harness(optimized, 1)];
        for (const h of pair) {
            const command = fixtures.createCommand({
                renderPass: h.renderPass, vertexBuffers: [fixtures.createVertexBuffer(301, offset, false)],
                indirectDrawBuffer: fixtures.createResource(501), indirectDrawByteOffset: offset,
            });
            assert.equal(append(h, command), true);
            command.indirectDrawBuffer = null;
            command.count = 2 ** 32 + 9.75;
            command.start = -3.5;
            command.instancesCount = NaN;
            assert.equal(append(h, command), true);
            h.stream.flush();
        }
        assertRecords(pair[1].records, pair[0].records);
    }
});

test("invalid resource IDs never partially append or poison a later valid draw", () => {
    for (const id of [0, -1, 1.5, NaN, Infinity, Number.MAX_SAFE_INTEGER + 1]) {
        for (const resource of ["pipeline", "index", "vertex", "bindGroup", "indirect"]) {
            for (const encoder of [original, optimized]) {
                const h = harness(encoder);
                const command = fixtures.createCommand({ renderPass: h.renderPass, indirectDrawBuffer: fixtures.createResource(501) });
                assert.equal(append(h, command), true);
                const bad = fixtures.createCommand({ renderPass: h.renderPass, indirectDrawBuffer: fixtures.createResource(501) });
                const target = { pipeline: bad.pipeline, index: bad.indexBuffer.underlyingResource,
                    vertex: bad.vertexBuffers[1].effectiveBuffer.underlyingResource, bindGroup: bad.bindGroups[1], indirect: bad.indirectDrawBuffer }[resource];
                const before = Array.from(h.stream._commandWords.subarray(0, h.stream._commandWordCount));
                const references = retained(h.stream);
                fixtures.setBackendResourceId(target, id);
                assert.equal(append(h, bad), false);
                assert.deepEqual(Array.from(h.stream._commandWords.subarray(0, h.stream._commandWordCount)), before);
                assert.deepEqual(retained(h.stream), references);
                assert.equal(h.stream._drawCount, 1);
                assert.equal(h.records.length, 0);
                fixtures.setBackendResourceId(target, 900);
                assert.equal(append(h, bad), true);
                assert.equal(h.stream.flush(), true);
                assert.equal(h.records[0].drawCount, 2);
            }
        }
    }
});

for (const failure of ["rejected", "threw", "threw-falsy", "missing"]) {
    test(`atomic ${failure} batch replays identical commands and releases retained objects`, () => {
        const pair = [harness(original, 256, false), harness(optimized, 1, false)];
        for (const h of pair) {
            const commands = variedCommands(h.renderPass);
            for (const command of commands) assert.equal(append(h, command), true);
            const references = retained(h.stream);
            assert.ok(references.length > commands.length);
            const record = h.renderPass._recordCommands;
            h.renderPass._recordCommands = (...args) => {
                assert.deepEqual(retained(h.stream), references);
                record(...args);
                if (failure === "threw") throw new Error("atomic failure");
                if (failure === "threw-falsy") throw 0;
                return false;
            };
            if (failure === "missing") delete h.renderPass._recordCommands;
            assert.equal(h.stream.flush(), false);
            assert.equal(h.calls.filter(call => call[0].startsWith("draw")).length, commands.length);
            assert.equal(h.disabled.length, 1);
            assert.equal(h.disabled[0].replayedDrawCount, commands.length);
            assert.equal(retained(h.stream).length, 0);
            assert.ok(h.stream._pendingResources.every(resource => resource === null));
            assert.equal(append(h, commands[0]), false);
            assert.equal(h.stream.flush(), false);
        }
        assertRecords(pair[1].records, pair[0].records);
        same(pair[1].calls, pair[0].calls);
        same(pair[1].disabled, pair[0].disabled);
    });
}

test("successful recording holds exact resource objects until consumption", () => {
    const h = harness(optimized, 1);
    const command = fixtures.createCommand({ renderPass: h.renderPass, indirectDrawBuffer: fixtures.createResource(501) });
    assert.equal(append(h, command), true);
    assert.equal(append(h, command), true);
    const expected = [command.pipeline, command.indexBuffer.underlyingResource,
        ...command.vertexBuffers.map(vertex => vertex.effectiveBuffer.underlyingResource), ...command.bindGroups,
        command.indirectDrawBuffer, command.indirectDrawBuffer];
    const record = h.renderPass._recordCommands;
    h.renderPass._recordCommands = (...args) => {
        const actual = retained(h.stream);
        assert.equal(actual.length, expected.length);
        actual.forEach((resource, index) => assert.equal(resource, expected[index]));
        return record(...args);
    };
    assert.equal(h.stream.flush(), true);
    assert.ok(h.stream._pendingResources.every(resource => resource === null));
});

test("unsupported atomicity and protocol mismatches do not reach the recorder", () => {
    for (const override of [{ recordsAtomically: undefined }, { recordsAtomically: false }, { protocolVersion: -1 }]) {
        const h = harness(optimized, 0);
        Object.assign(h.provider, override);
        const command = fixtures.createCommand({ renderPass: h.renderPass });
        assert.equal(append(h, command), false);
        assert.equal(append(h, command), false);
        assert.equal(h.stream.flush(), false);
        assert.equal(h.records.length, 0);
        assert.equal(h.calls.length, 0);
        assert.equal(h.disabled.length, 1);
        assert.equal(retained(h.stream).length, 0);
        assert.equal(h.stream._commandWords.length, 0);
    }
});

for (const count of [1023, 1024, 1025, 2050]) {
    test(`batch boundary at ${count} draws is byte-identical and bounded`, () => {
        const pair = [harness(original), harness(optimized, 1)];
        for (const h of pair) {
            const command = fixtures.createCommand({ renderPass: h.renderPass });
            for (let i = 0; i < count; i++) {
                command.start = i;
                assert.equal(h.stream.tryAppend(command), true);
            }
            assert.equal(h.records.length, Math.floor((count - 1) / 1024));
            h.stream.flush();
            assert.equal(h.records.length, Math.ceil(count / 1024));
            assert.equal(h.records.reduce((sum, record) => sum + record.drawCount, 0), count);
            for (const record of h.records) {
                assert.ok(record.drawCount <= 1024);
                assert.equal(record.words[0], original.api.WebGPURenderPassCommandOpcode.setPipeline);
            }
        }
        assertRecords(pair[1].records, pair[0].records);
    });
}

test("pass switches and explicit resets re-emit state without stale references", () => {
    const outcomes = [];
    for (const encoder of [original, optimized]) {
        const h = harness(encoder, 1);
        const second = fixtures.createRecordingRenderPass();
        assert.equal(append(h, fixtures.createCommand({ renderPass: h.renderPass })), true);
        assert.equal(append(h, fixtures.createNonIndexedCommand({ renderPass: second.renderPass })), true);
        assert.equal(h.records.length, 1);
        h.stream.flush();
        assert.equal(append(h, fixtures.createCommand({ renderPass: h.renderPass })), true);
        h.stream.reset();
        assert.equal(retained(h.stream).length, 0);
        assert.ok(h.stream._pendingResources.every(resource => resource === null));
        assert.equal(append(h, fixtures.createCommand({ renderPass: second.renderPass })), true);
        h.stream.flush();
        outcomes.push([...h.records, ...second.records]);
    }
    assertRecords(outcomes[1], outcomes[0]);
});

test("a rejected automatic flush replays the full batch but does not append the next draw", () => {
    const outcomes = [];
    for (const encoder of [original, optimized]) {
        const h = harness(encoder, 1, false);
        const command = fixtures.createCommand({ renderPass: h.renderPass });
        for (let i = 0; i < 1024; i++) assert.equal(h.stream.tryAppend(command), true);
        assert.equal(h.stream.tryAppend(command), false);
        assert.equal(h.records[0].drawCount, 1024);
        assert.equal(h.calls.filter(call => call[0] === "drawIndexed").length, 1024);
        assert.equal(h.disabled[0].replayedDrawCount, 1024);
        assert.equal(retained(h.stream).length, 0);
        outcomes.push({ records: h.records, calls: h.calls, disabled: h.disabled });
    }
    same(outcomes[1], outcomes[0]);
});
