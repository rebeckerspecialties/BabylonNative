import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(new URL("../package.json", import.meta.url));
const ts = require("typescript");
const BABYLON = require("babylonjs");
const assetRoot = new URL("../Playground/Scripts/native_xr_portal_assets/", import.meta.url);
const glb = readFileSync(new URL("native_xr_portal_hillvalley.astc.glb", assetRoot));
const jsonLength = glb.readUInt32LE(12);
const asset = JSON.parse(glb.subarray(20, 20 + jsonLength).toString("utf8"));
const binary = glb.subarray(28 + jsonLength);
const targets = [
    { mesh: "Road1", material: "New road", texture: "native_xr_portal_road_01.ktx", roughness: 0.95 },
    { mesh: "Trottoir", material: "Trottoir", texture: "native_xr_portal_sol_trotoir_.ktx", roughness: 0.9 },
    { mesh: "Trottoir3", material: "Trottoir3", texture: "native_xr_portal_trotoir_.ktx", roughness: 0.9 },
];

function extractFunctions(url, names) {
    const ast = ts.createSourceFile(url.pathname, readFileSync(url, "utf8"), ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
    const functions = new Map();
    function visit(node) {
        if (ts.isFunctionDeclaration(node) && names.includes(node.name?.text)) {
            functions.set(node.name.text, node.getText(ast));
        }
        ts.forEachChild(node, visit);
    }
    visit(ast);
    assert.equal(functions.size, names.length, "All tested production functions must exist");
    return names.map(name => functions.get(name)).join("\n");
}

const tuningCode = extractFunctions(new URL("tuneHillValleyGltfMaterials.js", import.meta.url), [
    "clamp01", "cloneJson", "pushExtensionUsed", "isBackdropMaterialName", "hintMaterial",
    "ensureHillValleyPunctualLights", "tune", "alignTo", "writeGlb",
]);
const runtimeCode = extractFunctions(new URL("../Playground/Scripts/native_xr_babylon_portal_demo.js", import.meta.url), [
    "materialNameMatches", "isPortalBackdropMaterial", "isPortalReflectiveMaterial", "applyRealtimeMaterialOverrides",
]);

function tuner() {
    const output = [];
    const context = vm.createContext({
        Buffer,
        GLB_MAGIC: 0x46546c67,
        JSON_CHUNK: 0x4e4f534a,
        BIN_CHUNK: 0x004e4942,
        fs: { writeFileSync: (_path, bytes) => output.push(bytes) },
    });
    vm.runInContext(tuningCode, context);
    return { context, output };
}

function materialByName(json, name) {
    const material = json.materials.find(value => value.name === name);
    assert.ok(material, `Missing material ${name}`);
    return material;
}

function runtimeMaterial(name) {
    const material = materialByName(asset, name);
    const pbr = material.pbrMetallicRoughness;
    return {
        name,
        metallic: pbr.metallicFactor,
        roughness: pbr.roughnessFactor,
        alpha: pbr.baseColorFactor[3],
        albedoTexture: pbr.baseColorTexture,
        emissiveTexture: material.emissiveTexture || null,
        emissiveColor: BABYLON.Color3.FromArray(material.emissiveFactor || [0, 0, 0]),
        emissiveIntensity: 1,
        unlit: Boolean(material.extensions?.KHR_materials_unlit),
        environmentIntensity: 1,
        directIntensity: 1,
        reflectionTexture: null,
    };
}

function applyRuntime(materials, overrides = {}) {
    const scene = { materials, environmentTexture: { name: "authored-environment" } };
    const context = vm.createContext({ BABYLON, reportStatus() {}, __nativeArPortalUseEnvironmentReflectionOverride: true, ...overrides });
    vm.runInContext(runtimeCode, context);
    context.applyRealtimeMaterialOverrides(scene);
    return scene;
}

test("ground materials retain their authored mesh assignments, texture files and UV transforms", () => {
    for (const target of targets) {
        const mesh = asset.meshes.find(value => value.name === target.mesh);
        assert.equal(mesh.primitives.length, 1);
        const primitive = mesh.primitives[0];
        const material = asset.materials[primitive.material];
        assert.equal(material.name, target.material);
        assert.equal(asset.meshes.filter(value => value.primitives.some(p => p.material === primitive.material)).length, 1);
        assert.ok(primitive.attributes.TEXCOORD_0 !== undefined);
        const info = material.pbrMetallicRoughness.baseColorTexture;
        const texture = asset.textures[info.index];
        assert.equal(asset.images[texture.source].uri, target.texture);
        assert.deepEqual(asset.samplers[texture.sampler], { wrapS: 10497, wrapT: 10497 });
        assert.deepEqual(info, target.material === "New road"
            ? { index: 88, extensions: { KHR_texture_transform: { offset: [0, 0], scale: [70, 70] } } }
            : { index: target.material === "Trottoir" ? 89 : 91 });
        assert.ok(readFileSync(new URL(target.texture, assetRoot)).length > 64);
    }
});

test("packaged asphalt and concrete are rough, opaque, lit dielectrics without emission", () => {
    for (const target of targets) {
        const material = materialByName(asset, target.material);
        assert.equal(material.pbrMetallicRoughness.metallicFactor, 0);
        assert.equal(material.pbrMetallicRoughness.roughnessFactor, target.roughness);
        assert.deepEqual(material.pbrMetallicRoughness.baseColorFactor, [1, 1, 1, 1]);
        assert.equal(material.alphaMode ?? "OPAQUE", "OPAQUE");
        assert.equal(material.extensions?.KHR_materials_unlit, undefined);
        assert.equal(material.emissiveFactor, undefined);
        assert.equal(material.emissiveTexture, undefined);
        assert.equal(material.normalTexture, undefined);
        assert.equal(material.pbrMetallicRoughness.metallicRoughnessTexture, undefined);
    }
});

test("tuning the previous ground values changes only three roughness factors and preserves the binary chunk", () => {
    const previous = structuredClone(asset);
    for (const target of targets) {
        materialByName(previous, target.material).pbrMetallicRoughness.roughnessFactor = 0.6156187236129782;
    }
    const { context, output } = tuner();
    context.tune(previous);
    assert.deepEqual(structuredClone(previous), asset);
    // Exercise the real writer in memory, without modifying the asset on disk.
    context.writeGlb("memory.glb", previous, binary);
    assert.equal(output.length, 1);
    const written = output[0];
    const length = written.readUInt32LE(12);
    assert.equal(written.readUInt32LE(8), written.length);
    assert.deepEqual(JSON.parse(written.subarray(20, 20 + length).toString("utf8")), asset);
    assert.deepEqual(written.subarray(28 + length), binary);
});

test("tuning is idempotent for the entire packaged scene", () => {
    const json = structuredClone(asset);
    const { context } = tuner();
    context.tune(json);
    context.tune(json);
    assert.deepEqual(structuredClone(json), asset);
});

test("ground tuning corrects stale lighting hints without replacing authored color or texture", () => {
    const { context } = tuner();
    for (const target of targets) {
        const material = structuredClone(materialByName(asset, target.material));
        material.pbrMetallicRoughness.metallicFactor = 0.8;
        material.pbrMetallicRoughness.roughnessFactor = 0.2;
        material.extensions = { KHR_materials_unlit: {} };
        material.emissiveFactor = [0.16, 0.16, 0.16];
        material.emissiveTexture = material.pbrMetallicRoughness.baseColorTexture;
        context.hintMaterial(material);
        assert.deepEqual(structuredClone(material), materialByName(asset, target.material));
    }
});

test("runtime preserves ground roughness and removes ambient emission on repeated application", () => {
    for (const target of targets) {
        for (const roughness of [undefined, 0.6156187236129782, 0.82, 0.9, 0.95, 1]) {
            const material = runtimeMaterial(target.material);
            const texture = material.albedoTexture;
            Object.assign(material, { roughness, metallic: 0.8, unlit: true, emissiveTexture: texture, emissiveColor: new BABYLON.Color3(0.16, 0.16, 0.16) });
            applyRuntime([material]);
            applyRuntime([material]);
            assert.equal(material.roughness, target.roughness);
            assert.equal(material.metallic, 0);
            assert.equal(material.unlit, false);
            assert.equal(material.alpha, 1);
            assert.equal(material.albedoTexture, texture);
            assert.equal(material.emissiveTexture, null);
            assert.deepEqual(material.emissiveColor.asArray(), [0, 0, 0]);
            assert.equal(material.environmentIntensity, 1.08);
            assert.equal(material.directIntensity, 0.65);
        }
    }
});

test("painted markings, grass and similarly named materials retain the existing runtime policy", () => {
    for (const name of ["Parking lot", "Trottoir2", "Herbe5", "Herbes11", "Gravier11", "BordureRue2", "new road", "New road copy", "Trottoir4"]) {
        const material = { ...runtimeMaterial("New road"), name, roughness: 0.6156187236129782 };
        applyRuntime([material]);
        assert.equal(material.roughness, 0.6156187236129782, name);
        assert.equal(material.emissiveTexture, material.albedoTexture, name);
        assert.deepEqual(material.emissiveColor.asArray(), [0.16, 0.16, 0.16], name);
    }
});

test("all seven authored backdrop materials keep unlit textured emission", () => {
    const materials = asset.materials.filter(material => material.extensions?.KHR_materials_unlit).map(material => runtimeMaterial(material.name));
    assert.equal(materials.length, 7);
    applyRuntime(materials);
    for (const material of materials) {
        assert.equal(material.unlit, true, material.name);
        assert.equal(material.metallic, 0);
        assert.equal(material.roughness, 1);
        assert.deepEqual(material.emissiveTexture, material.albedoTexture);
        assert.deepEqual(material.emissiveColor.asArray(), [1, 1, 1]);
        assert.equal(material.emissiveIntensity, 1);
    }
});

test("car body, plate, doors and wheels retain their existing runtime materials", () => {
    for (const [name, metallic, roughness, emission, environment] of [
        ["Ext Carrosserie", 0.8, 0.2, 0, 1.42],
        ["D1_ext_carrosserie_plate", 0.8, 0.2, 0, 1.42],
        ["DLporte2", 0, 0.48, 0.16, 1.08],
        ["D1porte022", 0, 0.6156187236129782, 0.16, 1.08],
        ["08 - Default", 0, 0.48, 0.16, 1.08],
    ]) {
        const material = runtimeMaterial(name);
        const texture = material.albedoTexture;
        const scene = applyRuntime([material]);
        assert.equal(material.metallic, metallic, name);
        assert.equal(material.roughness, roughness, name);
        assert.equal(material.albedoTexture, texture, name);
        assert.deepEqual(material.emissiveColor.asArray(), [emission, emission, emission], name);
        assert.equal(material.emissiveTexture, emission ? texture : null, name);
        assert.equal(material.environmentIntensity, environment, name);
        assert.equal(material.reflectionTexture, environment === 1.42 ? scene.environmentTexture : null, name);
    }
});

test("disabled realtime lighting leaves every material unchanged", () => {
    const materials = asset.materials.map(material => runtimeMaterial(material.name));
    const before = structuredClone(materials);
    applyRuntime(materials, { __nativeArPortalUseRealtimeLighting: false });
    assert.deepEqual(structuredClone(materials), before);
});
