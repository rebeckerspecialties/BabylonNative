import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { join, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { createLoopInvariantTransformer } from "./loopInvariantTransformer.mjs";

const root = resolve(process.env.BABYLON_BUILD_SOURCE || fileURLToPath(new URL("../../build_babylon_bundle/source", import.meta.url)));
const require = createRequire(join(root, "package.json"));
const ts = require("typescript");

function compile(source) {
    const file = "/fixture.ts";
    const options = { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, alwaysStrict: true, noLib: true };
    const host = ts.createCompilerHost(options);
    host.getSourceFile = name => name === file ? ts.createSourceFile(file, source, options.target, true) : undefined;
    host.fileExists = name => name === file;
    host.readFile = name => name === file ? source : undefined;
    const program = ts.createProgram([file], options, host);
    const transform = createLoopInvariantTransformer(ts, program);
    let code;
    program.emit(undefined, (_name, output) => { code = output; }, undefined, false, transform);
    return { code, report: transform.report, original: ts.transpileModule(source, { compilerOptions: options }).outputText };
}

function equivalent(source, args = [[0, true], [1, false], [7, true]]) {
    const result = compile(source);
    for (const values of args) {
        const run = code => structuredClone(vm.runInNewContext(`${code}\nrun`, {})(...values));
        assert.deepEqual(run(result.code), run(result.original), JSON.stringify(values));
    }
    return result;
}

test("hoists stable lexical predicates once, with identical 0/1/n results", () => {
    const result = equivalent(`function run(n, flag) {
        let count = 0;
        for (let i = 0; i < n; i++) if (flag === true || flag === "enabled") count++;
        return count;
    }`);
    assert.equal(result.report.hoisted.length, 1);
    assert.match(result.code, /const _loopInvariant_\d+ = flag === true \|\| flag === "enabled"/);
});

test("does not treat types, private, readonly, getters, or Proxy paths as invariant", () => {
    const result = equivalent(`function run(n) {
        let reads = 0, sum = 0;
        const options = new Proxy({ get value() { return ++reads; } }, {
            get(target, key) { reads++; return target[key]; }
        });
        for (let i = 0; i < n; i++) sum += options.value;
        return [sum, reads];
    }`);
    assert.equal(result.report.hoisted.length, 0);
    assert.ok(result.report.rejectedProperties.some(entry => entry.expression === "options.value"));
    const typed = compile(`class C { constructor(private readonly value: number) {} run(n: number) {
        let count = 0; for (let i = 0; i < n; i++) if (this.value === 1) count++; return count;
    } }`);
    assert.equal(typed.report.hoisted.length, 0);
});

test("unknown calls may coexist with stable lexical values, but not closure mutations", () => {
    const stable = equivalent(`function run(n, flag) {
        const log = []; const callback = value => log.push(value);
        for (let i = 0; i < n; i++) { callback(i); if (flag === true) callback("yes"); }
        return log;
    }`);
    assert.equal(stable.report.hoisted.length, 1);
    const changing = equivalent(`function run(n, flag) {
        const log = []; const callback = () => { flag = !flag; };
        for (let i = 0; i < n; i++) { callback(); if (flag === true) log.push(i); }
        return log;
    }`);
    assert.equal(changing.report.hoisted.length, 0);
});

for (const assignment of ["flag = !flag", "[flag] = [!flag]", "({ flag } = { flag: !flag })",
    "({ value: flag } = { value: !flag })", "[flag = false] = [!flag]", "for (flag of [!flag]) {}",
    "flag &&= false", "flag ||= true", "flag ??= false", "flag++", "for (flag in { key: true }) {}"] ) {
    test(`rejects loop-varying bindings through ${assignment}`, () => {
        const result = equivalent(`function run(n, flag) {
            let count = 0; for (let i = 0; i < n; i++) { ${assignment}; if (flag === true) count++; } return count;
        }`);
        assert.equal(result.report.hoisted.length, 0);
    });
}

test("does not hoist coercion, allocation, throwing reads, or calls on zero-trip paths", () => {
    const result = equivalent(`function run(n) {
        let calls = 0; const input = { valueOf() { calls++; throw Error("coercion"); } };
        let error = ""; try { for (let i = 0; i < n; i++) if (input < 5) calls++; } catch (e) { error = e.message; }
        return [calls, error];
    }`);
    assert.equal(result.report.hoisted.length, 0);
    equivalent(`function run(n) {
        let reads = 0, error = "";
        const object = { get value() { reads++; throw Error("getter"); } };
        try { for (let i = 0; i < n; i++) if (false && object.value) reads++; } catch (e) { error = e.message; }
        return [reads, error];
    }`);
    assert.equal(compile("function run(n) { for (let i=0;i<n;i++) if ({} === {}) return true; return false; }").report.hoisted.length, 0);
});

test("leaves copy-on-write and aliased indexed getters observable", () => {
    const result = equivalent(`function run(n) {
        let current = [0,0,0,0,0,0,0]; const history = [];
        const input = { get 0() { history.push(current); current = current.slice(); return 3; } };
        for (let i=0;i<n;i++) current[i] = input[0];
        return [current, history.map(a => a.slice())];
    }`);
    assert.equal(result.report.hoisted.length, 0);
});

test("retains shadowed bindings and initialization order", () => {
    equivalent(`function run(n, flag) {
        let count = 0; const outer = flag === true;
        for (let i=0;i<n;i++) { const flag = i === 0; if (flag === true) count++; if (outer === true) count++; }
        return count;
    }`);
    const result = equivalent(`function run(n, flag) {
        let error = "";
        try { for (let i=0;i<n;i++) if (later === true) return 1; const later = flag; }
        catch (e) { error = e.name; } return error;
    }`);
    assert.equal(result.report.hoisted.length, 0);
});

test("labels, break, continue, and early returns keep their control flow", () => {
    equivalent(`function run(n, flag) {
        const log = [];
        outer: for (let i=0;i<n;i++) { for (let j=0;j<3;j++) {
            if (flag === true) { log.push(i); continue outer; }
            if (j === 1) break outer;
        } } return log;
    }`);
    equivalent(`function run(n, flag) {
        let count = 0; for (let i=0;i<n;i++) { if (flag === true) return i; if (i === 2) break; count++; } return count;
    }`);
});

test("direct eval and arguments aliases prevent transformation", () => {
    for (const mutation of ["eval('flag = !flag')", "arguments[1] = !flag"]) {
        const result = equivalent(`function run(n, flag) {
            let count=0; for(let i=0;i<n;i++) { ${mutation}; if(flag === true) count++; } return count;
        }`);
        assert.equal(result.report.hoisted.length, 0);
    }
});

test("global and captured mutable state cannot become a local snapshot", () => {
    const result = equivalent(`let flag = false; function run(n) {
        let count = 0; for(let i=0;i<n;i++) { flag = !flag; if(flag === true) count++; } return count;
    }`);
    assert.equal(result.report.hoisted.length, 0);
});

test("same-frame calls and independent views do not share cached values", () => {
    const result = compile(`function one(n, mode) {
        let count=0; for(let i=0;i<n;i++) if(mode === "left" || mode === "right") count++; return count;
    }
    function run() { return [one(3,"left"), one(2,"none"), one(7,"right"), one(0,"left")]; }`);
    assert.equal(result.report.hoisted.length, 1);
    assert.deepEqual(structuredClone(vm.runInNewContext(`${result.code}\nrun()`)), [3,0,7,0]);
});

test("transformation is idempotent and does not collapse distinct symbols", () => {
    const source = `function run(n, flag) { let result=0; for(let i=0;i<n;i++) if(flag === true) result++; return result; }`;
    const first = compile(source);
    const second = compile(first.code);
    assert.equal(second.report.hoisted.length, 0);
});

test("three Terser compression passes retain the lifted predicate", async () => {
    const { minify } = require("terser");
    const result = compile(`function run(n, flag) { let result=0; for(let i=0;i<n;i++) if(flag === true || flag === "enabled") result++; return result; }`);
    const minimized = await minify(result.code, { compress: { passes: 3 }, mangle: false });
    assert.match(minimized.code, /const _loopInvariant_/);
    for (const n of [0, 1, 7]) for (const flag of [false, true, "enabled", null]) {
        assert.equal(vm.runInNewContext(`${minimized.code};run`)(n, flag), vm.runInNewContext(`${result.original};run`)(n, flag));
    }
});

test("effectful policies and same-frame getter mutation are not memoized", () => {
    const result = equivalent(`function run(n) {
        let reads = 0, count = 0;
        const policy = { get flag() { return ++reads % 2 === 0; } };
        for(let i=0;i<n;i++) if(policy.flag === true) count++;
        return [reads, count];
    }`);
    assert.equal(result.report.hoisted.length, 0);
});

test("getter callbacks can mutate lexical parameters without an explicit loop-body assignment", () => {
    const result = equivalent(`function run(n, flag) {
        const source = { get value() { flag = !flag; return 1; } };
        let count = 0;
        for (let i = 0; i < n; i++) { count += source.value; if (flag === true) count++; }
        return count;
    }`);
    assert.equal(result.report.hoisted.length, 0);
});

test("nested-loop local initialization stays inside the outer iteration", () => {
    const result = equivalent(`function run(n, flag) {
        const log = [];
        for (let i = 0; i < n; i++) {
            const mode = i % 2 === 0;
            for (let j = 0; j < 3; j++) if (mode === true || flag === true) log.push([i, j]);
        }
        return log;
    }`);
    assert.ok(result.report.hoisted.every(entry => entry.expression !== "mode === true || flag === true"));
});

test("eligible boolean operations never coerce object parameters, even on a zero-trip loop", () => {
    const result = equivalent(`function inner(n, input) {
        let count = 0;
        for (let i = 0; i < n; i++) if (!input || typeof input === "object") count++;
        return count;
    }
    function run(n) {
        let coercions = 0;
        const input = { [Symbol.toPrimitive]() { coercions++; throw Error("unexpected coercion"); } };
        return [inner(n, input), coercions];
    }`);
    assert.equal(result.report.hoisted.length, 1);
});
