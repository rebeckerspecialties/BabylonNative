import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import test from "node:test";
import vm from "node:vm";

const root = resolve(
  process.env.BABYLON_BUILD_SOURCE ||
    new URL("../../build_babylon_bundle/source", import.meta.url).pathname,
);
const require = createRequire(join(root, "package.json"));
const ts = require("typescript");
const { createAccessorInlineTransformers, getAccessorInlineOptionsFromEnv } =
  await import(
    pathToFileURL(join(root, "packages/public/accessorInlineTransformer.mjs"))
  );

function compile(source, options = {}) {
  const directory = mkdtempSync(join(tmpdir(), "babylon-accessor-test-"));
  try {
    const file = join(directory, "fixture.ts");
    writeFileSync(file, source);
    const program = ts.createProgram([file], {
      target: ts.ScriptTarget.ES2022,
      module: ts.ModuleKind.None,
      noLib: true,
    });
    const transformer = createAccessorInlineTransformers(program, {
      mode: "all",
      allowMapped: true,
      downcastPolicy: "site",
      siblingRule: true,
      scanExternalPackages: false,
      ...options,
    });
    let code;
    program.emit(
      undefined,
      (name, text) => {
        if (name.endsWith(".js")) code = text;
      },
      undefined,
      undefined,
      transformer,
    );
    const context = vm.createContext({});
    vm.runInContext(code, context);
    return {
      code,
      result: JSON.parse(JSON.stringify(context.result)),
      report: transformer.buildReport(),
    };
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
}

test("inlines trivial reads and leaves setters, compound assignments and deletes intact", () => {
  const f =
    compile(`class Value { _value = 3; writes = 0; get value() { return this._value; } set value(n: number) { this.writes++; this._value = n; } }
        const v = new Value(); v.value = 4; v.value += 2; const read = v.value; delete v.value;
        globalThis.result = [read, v.writes, v._value];`);
  assert.deepEqual(f.result, [6, 2, 6]);
  assert.ok(f.report.total > 0);
  assert.match(f.code, /const read = v\._value/);
  assert.match(f.code, /v\.value \+= 2/);
  assert.match(f.code, /delete v\.value/);
});

test("zero candidates remains a correct no-op", () => {
  const f = compile(
    "const value = { plain: 9 }; globalThis.result = value.plain;",
  );
  assert.equal(f.result, 9);
  assert.equal(f.report.total, 0);
});

test("known subclass overrides are not bypassed through a base-typed argument", () => {
  const f =
    compile(`class Base { _value = 3; get value() { return this._value; } }
        class Child extends Base { get value() { return 17; } }
        function read(value: Base) { return value.value; }
        globalThis.result = [read(new Base()), read(new Child())];`);
  assert.deepEqual(f.result, [3, 17]);
  assert.equal(f.report.total, 0);
});

test("runtime property definitions and side-effecting getters keep normal dispatch", () => {
  const f =
    compile(`class Value { _value = 3; calls = 0; get value() { return this._value; } get counted() { return ++this.calls; } }
        const v = new Value(); Object.defineProperty(v, "value", { get() { return 12; } });
        globalThis.result = [v.value, v.counted, v.counted];`);
  assert.deepEqual(f.result, [12, 1, 2]);
  assert.equal(f.report.total, 0);
});

test("optional reads and effectful receivers are evaluated exactly once", () => {
  const f =
    compile(`class Value { _value = 4; get value() { return this._value; } }
        let count = 0; function getValue(): Value { count++; return new Value(); }
        function read(value: Value | null) { return value?.value; }
        globalThis.result = [getValue().value, count, read(null) ?? -1, read(new Value())];`);
  assert.deepEqual(f.result, [4, 1, -1, 4]);
  assert.ok(f.report.total > 0);
});

test("narrowing assertions and sibling class probes keep their original behavior", () => {
  const f =
    compile(`class Base {} class Mesh extends Base { _value = 5; get material() { return this._value; } }
        class Sibling extends Base { material = 19; }
        function read(value: Base) { return (value as Mesh).material; }
        globalThis.result = [read(new Mesh()), read(new Sibling())];`);
  assert.deepEqual(f.result, [5, 19]);
});

test("structural interface arguments remain public-property reads", () => {
  const f =
    compile(`class Value { _value = 3; get value() { return this._value; } }
        interface Shape { value: number; } function read(value: Shape) { return value.value; }
        globalThis.result = [read(new Value()), read({ value: 25 })];`);
  assert.deepEqual(f.result, [3, 25]);
  assert.equal(f.report.total, 0);
});

test("off is opt-in safe and an unknown mode fails rather than silently disabling", () => {
  const previous = process.env.BABYLON_ACCESSOR_INLINE;
  try {
    process.env.BABYLON_ACCESSOR_INLINE = "off";
    assert.equal(getAccessorInlineOptionsFromEnv(), undefined);
    process.env.BABYLON_ACCESSOR_INLINE = "typo";
    assert.throws(getAccessorInlineOptionsFromEnv, /unknown mode/);
  } finally {
    if (previous === undefined) delete process.env.BABYLON_ACCESSOR_INLINE;
    else process.env.BABYLON_ACCESSOR_INLINE = previous;
  }
});

test("expression getters observe same-frame uniform state and independent views", () => {
  const f = compile(
    `class Uniforms {
        _noUBO=false; _needSync=false;
        get useUbo(){return !this._noUBO;} get isSync(){return !this._needSync;}
    }
    const left=new Uniforms(), right=new Uniforms();
    const read=(u:Uniforms)=>[u.useUbo,u.isSync];
    const result=[read(left),read(right)]; left._needSync=true;result.push(read(left),read(right));
    left._needSync=false;right._noUBO=true;result.push(read(left),read(right));
    globalThis.result=result;`,
    { mode: "all+expr" },
  );
  assert.deepEqual(f.result, [
    [true, true],
    [true, true],
    [true, false],
    [true, true],
    [true, true],
    [false, true],
  ]);
  assert.equal(f.report.sitesByForm.expr, 2);
});

test("expression getter evaluates an effectful receiver exactly once for 0/1/n reads", () => {
  const f = compile(
    `class Uniforms { _needSync=false;get isSync(){return !this._needSync;} }
    let calls=0;const u=new Uniforms();function acquire():Uniforms{calls++;u._needSync=!u._needSync;return u;}
    const result=[];for(const count of [0,1,7]){const values=[];for(let i=0;i<count;i++)values.push(acquire().isSync);result.push([calls,values]);}
    globalThis.result=result;`,
    { mode: "all+expr" },
  );
  assert.deepEqual(f.result, [
    [0, []],
    [1, [false]],
    [8, [true, false, true, false, true, false, true]],
  ]);
  assert.equal(f.report.sitesByForm.expr, 1);
});

test("multi-read getter cannot observe reassignment of its caller's receiver binding", () => {
  const f = compile(
    `let receiver:State;class State {
        constructor(public value:number){} get first(){receiver=new State(10);return 1;}
        get total(){return this.first+this.value;}
    }
    receiver=new State(2);globalThis.result=receiver.total;`,
    { mode: "all+expr" },
  );
  assert.equal(f.result, 3);
  assert.ok(f.report.skippedSites["expr:repeatedOrLazyReceiver"] > 0);
});

test("lazy and constant getters retain receiver errors and optional-chain behavior", () => {
  const f = compile(
    `class State { _value=1;get lazy(){return false && this._value;} get constant(){return true;} get unary(){return !this._value;} }
    const result=[];for(const name of ["lazy","constant"]){try{let s:State=null;if(name==="lazy")result.push(s.lazy);else result.push(s.constant);}catch(e){result.push(e.name);}}
    let s:State=null;result.push(s?.unary??"absent");globalThis.result=result;`,
    { mode: "all+expr" },
  );
  assert.deepEqual(f.result, ["TypeError", "TypeError", "absent"]);
});

test("static material flags retain setters, dirty notifications and same-frame changes", () => {
  const f = compile(
    `const dirty=[];class Flags {
        static _enabled=true;
        static get enabled(){return this._enabled;}
        static set enabled(value:boolean){if(this._enabled!==value){this._enabled=value;dirty.push(value);}}
    }
    function render(view:string){return [view,Flags.enabled];}
    const result=[render("left")];Flags.enabled=false;result.push(render("right"),render("left"));
    Flags.enabled=true;result.push(render("left"),render("right"));globalThis.result=[result,dirty];`,
    { includeStatic: true },
  );
  assert.deepEqual(f.result, [
    [
      ["left", true],
      ["right", false],
      ["left", false],
      ["left", true],
      ["right", true],
    ],
    [false, true],
  ]);
  assert.equal(f.report.sitesByReceiverKind.staticClass, 1);
  assert.match(f.code, /Flags\._enabled/);
  assert.match(f.code, /Flags\.enabled = false/);
});

test("static inlining leaves subclass and constructor-variable dispatch intact", () => {
  const f = compile(
    `class Flags {static _enabled=1;static get enabled(){return this._enabled;} static read(){return this.enabled;} }
    class Child extends Flags {static get enabled(){return 7;}}
    function read(C:typeof Flags){return C.enabled;}
    const alias=Flags;globalThis.result=[Flags.enabled,Child.enabled,Flags.read(),Child.read(),read(Child),alias.enabled];`,
    { includeStatic: true },
  );
  assert.deepEqual(f.result, [1, 7, 1, 7, 7, 1]);
  assert.match(f.code, /return C\.enabled/);
  assert.match(f.code, /return this\.enabled/);
  assert.match(f.code, /alias\.enabled/);
});

test("static runtime redefinition is excluded and static mode defaults off", () => {
  const source = `class Flags {static _enabled=1;static get enabled(){return this._enabled;}}
        Object.defineProperty(Flags,"enabled",{get(){return 9;}});globalThis.result=Flags.enabled;`;
  const f = compile(source, { includeStatic: true });
  assert.equal(f.result, 9);
  assert.equal(f.report.total, 0);
  const off = compile(
    `class Flags{static _enabled=1;static get enabled(){return this._enabled;}}globalThis.result=Flags.enabled;`,
  );
  assert.equal(off.report.total, 0);
});

test("deleting a static getter cannot leave an inlined stale backing-field read", () => {
  for (const remove of [
    "delete Flags.enabled",
    "Reflect.deleteProperty(Flags, 'enabled')",
    "const name='enabled';delete Flags[name]",
  ]) {
    const f = compile(
      `class Flags{static _enabled=1;static get enabled(){return this._enabled;}}
            const first=Flags.enabled;${remove};globalThis.result=[first,Flags.enabled??"absent"];`,
      { includeStatic: true },
    );
    assert.deepEqual(f.result, [1, "absent"]);
    assert.equal(f.report.total, 0);
  }
});

test("reassigned class bindings do not become static backing-field reads", () => {
  for (const assignment of [
    "Flags=Other",
    "({value:Flags}={value:Other})",
    "for(Flags of [Other]){}",
  ]) {
    const f = compile(
      `class Flags{static _enabled=1;static get enabled(){return this._enabled;}}
            class Other{static _enabled=2;static get enabled(){return 9;}}
            const first=Flags.enabled;${assignment};globalThis.result=[first,Flags.enabled];`,
      { includeStatic: true },
    );
    assert.deepEqual(f.result, [1, 9]);
  }
});

test("receiver-only getters retain null errors and free bindings retain their scope", () => {
  const f = compile(
    `class State { _value=1;get self(){return this;}get truth(){return !this;}get missing(){return this._value===undefined;} }
        function read(undefined){const s=new State();return s.missing;}
        const result=[read(1)];for(const name of ["self","truth"]){try{const s:State=null;result.push(name==="self"?s.self:s.truth);}catch(e){result.push(e.name);}}
        globalThis.result=result;`,
    { mode: "all+expr" },
  );
  assert.deepEqual(f.result, [false, "TypeError", "TypeError"]);
  assert.equal(f.report.total, 0);
});

test("prototype deletion through Reflect or a computed key prevents stale getter substitution", () => {
  for (const remove of [
    "Reflect.deleteProperty(State.prototype, 'value')",
    "const key='value';Reflect.deleteProperty(State.prototype,key)",
    "const key='value';delete State.prototype[key]",
  ]) {
    const f =
      compile(`class State{_value=1;get value(){return this._value;}}const s=new State();
            const first=s.value;${remove};globalThis.result=[first,s.value??"absent"];`);
    assert.deepEqual(f.result, [1, "absent"]);
    assert.equal(f.report.total, 0);
  }
});
