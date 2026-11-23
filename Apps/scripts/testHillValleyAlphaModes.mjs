import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(new URL("../package.json", import.meta.url));
const BABYLON = require("babylonjs");
const ts = require("typescript");
const source = readFileSync(new URL("transcodeHillValleyToGltf.js", import.meta.url), "utf8");
const ast = ts.createSourceFile("transcode.js", source, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
const helper = ast.statements.find(node => ts.isFunctionDeclaration(node) && node.name?.text === "applyMaterialAlphaMode");
assert.ok(helper);
const context = vm.createContext({});
vm.runInContext(helper.getText(ast), context);

function convert(setup) {
    const engine = new BABYLON.NullEngine();
    try {
        const scene = new BABYLON.Scene(engine);
        const material = new BABYLON.StandardMaterial("fixture", scene);
        const texture = new BABYLON.BaseTexture(scene);
        texture.hasAlpha = true;
        setup(material, texture);
        const result = {};
        context.applyMaterialAlphaMode(result, material);
        return result;
    } finally { engine.dispose(); }
}

test("an opaque material without an alpha texture stays opaque", () => {
    assert.deepEqual(convert(() => {}), {});
});
test("a diffuse alpha channel without alpha blending is a cutout at the source threshold", () => {
    for (const cutoff of [0, 0.4, 0.7, 1]) {
        assert.deepEqual(convert((m, t) => { m.diffuseTexture = t; m.alphaCutOff = cutoff; }), { alphaMode: "MASK", alphaCutoff: cutoff });
    }
});
for (const [name, setup] of [
    ["diffuse alpha explicitly used for blending", (m, t) => { m.diffuseTexture = t; m.useAlphaFromDiffuseTexture = true; }],
    ["opacity texture", (m, t) => { m.opacityTexture = t; }],
    ["fractional material alpha", m => { m.alpha = 0.4; }],
    ["explicit blend mode", m => { m.transparencyMode = BABYLON.Material.MATERIAL_ALPHABLEND; }],
    ["combined alpha-test-and-blend mode", (m, t) => { m.diffuseTexture = t; m.transparencyMode = BABYLON.Material.MATERIAL_ALPHATESTANDBLEND; }],
]) {
    test(`${name} remains blended, not a cutout`, () => assert.deepEqual(convert(setup), { alphaMode: "BLEND" }));
}
test("explicit opaque mode overrides an alpha texture and fractional alpha", () => {
    assert.deepEqual(convert((m, t) => { m.diffuseTexture = t; m.alpha = 0.5; m.transparencyMode = BABYLON.Material.MATERIAL_OPAQUE; }), {});
});
test("explicit alpha-test mode takes priority over automatic blend inputs", () => {
    assert.deepEqual(convert((m, t) => { m.diffuseTexture = t; m.alpha = 0.5; m.transparencyMode = BABYLON.Material.MATERIAL_ALPHATEST; }), { alphaMode: "MASK", alphaCutoff: 0.4 });
});
test("exporter applies source semantics before intentional realtime glass hints", () => {
    const body = ast.statements.find(node => ts.isFunctionDeclaration(node) && node.name?.text === "addMaterial").getText(ast);
    assert.ok(body.indexOf("applyMaterialAlphaMode(") < body.indexOf("applyRealtimeMaterialHints("));
    assert.doesNotMatch(body, /usesAlphaTexture/);
});
