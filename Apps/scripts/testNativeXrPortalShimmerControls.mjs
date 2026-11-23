import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const read = name => readFileSync(new URL(`../Playground/Scripts/native_xr_portal_${name}_validation.js`, import.meta.url), "utf8");
test("depth comparison changes only the non-XR near plane and chains the prior setup", () => {
    const camera = { mode: 0, minZ: 0.05, maxZ: 1000 };
    let calls = 0;
    const context = vm.createContext({ BABYLON: { Camera: { PERSPECTIVE_CAMERA: 0 } },
        __nativeArPortalForceNonXrStress: true, __nativeArPortalStartProfile() { calls++; } });
    vm.runInContext(read("depth_precision"), context);
    context.__nativeArPortalStartProfile({ activeCamera: camera }, () => {});
    assert.deepEqual(camera, { mode: 0, minZ: 0.5, maxZ: 1000 });
    assert.equal(calls, 1);
    assert.throws(() => context.__nativeArPortalStartProfile({ activeCamera: camera }, () => {}), /unmodified non-XR/);
});

for (const [xr, camera] of [[true, { mode: 0, minZ: 0.05 }], [false, null], [false, { mode: 1, minZ: 0.05 }]]) {
    test(`depth control rejects XR/missing/orthographic camera ${JSON.stringify([xr, camera])}`, () => {
        const context = vm.createContext({ BABYLON: { Camera: { PERSPECTIVE_CAMERA: 0 } }, __nativeArPortalForceNonXrStress: !xr });
        vm.runInContext(read("depth_precision"), context);
        assert.throws(() => context.__nativeArPortalStartProfile({ activeCamera: camera }, () => {}), /unmodified non-XR/);
        if (camera) assert.equal(camera.minZ, 0.05);
    });
}

test("AO quality control changes only both loop bounds, once, and chains setup", () => {
    const source = "prefix\nconst PORTAL_SLICE_COUNT: u32 = 2u;\nconst PORTAL_STEPS_PER_SIDE: u32 = 2u;\nsuffix";
    const store = { ssao2PixelShader: source };
    let calls = 0;
    const context = vm.createContext({ BABYLON: { ShaderStore: { ShadersStoreWGSL: store } },
        __nativeArPortalStartProfile() { calls++; } });
    vm.runInContext(read("ao_quality"), context);
    assert.equal(store.ssao2PixelShader, source.replaceAll("= 2u", "= 4u"));
    context.__nativeArPortalStartProfile({}, () => {});
    assert.equal(calls, 1);
    assert.throws(() => vm.runInContext(read("ao_quality"), context), /unmodified two-slice/);
});

test("AO quality control fails atomically for absent, partial, or duplicate bounds", () => {
    for (const source of [undefined, "", "const PORTAL_SLICE_COUNT: u32 = 2u;",
        "const PORTAL_SLICE_COUNT: u32 = 2u; const PORTAL_SLICE_COUNT: u32 = 2u; const PORTAL_STEPS_PER_SIDE: u32 = 2u;"]) {
        const store = { ssao2PixelShader: source };
        const context = vm.createContext({ BABYLON: { ShaderStore: { ShadersStoreWGSL: store } } });
        assert.throws(() => vm.runInContext(read("ao_quality"), context), /unmodified two-slice/);
        assert.equal(store.ssao2PixelShader, source);
    }
});

test("reversed depth chains configuration and rejects XR and non-WebGPU engines", () => {
    for (const [nonXr, webgpu] of [[true, true], [false, true], [true, false]]) {
        let calls = 0;
        const context = vm.createContext({ __nativeArPortalForceNonXrStress: nonXr,
            __nativePlaygroundConfigureEngine() { calls++; }, console: { log() {} } });
        vm.runInContext(read("reverse_depth"), context);
        const engine = { isWebGPU: webgpu, useReverseDepthBuffer: false };
        if (nonXr && webgpu) {
            context.__nativePlaygroundConfigureEngine(engine);
            assert.equal(engine.useReverseDepthBuffer, true);
        } else {
            assert.throws(() => context.__nativePlaygroundConfigureEngine(engine), /non-XR WebGPU/);
            assert.equal(engine.useReverseDepthBuffer, false);
        }
        assert.equal(calls, 1);
    }
});

const foliageNames = ["plan_tree_05", "plan_tree_02", "arbres_plan_04", "plan_tree_03", "buisson", "arbuste",
    "gd_arbre_01_SycamoreLeaves5", "pt_arbre_01_AlderBranches", "pt_arbre_branche_02", "gd_arbre_02_SycamoreLeaves5", "brousaille"];
function foliageFixture() {
    const materials = foliageNames.map(name => ({ name, alpha: 1, albedoTexture: { hasAlpha: true }, transparencyMode: 2 }));
    materials.push({ name: "vitre_trans_n", alpha: 0.4, transparencyMode: 2 });
    const context = vm.createContext({ BABYLON: { Material: { MATERIAL_ALPHATEST: 1, MATERIAL_ALPHABLEND: 2 } } });
    vm.runInContext(read("foliage_alpha"), context);
    return { materials, run: () => context.__nativeArPortalStartProfile({ materials }, () => {}) };
}
test("foliage control restores eleven cutout materials while preserving glass and textures", () => {
    const { materials, run } = foliageFixture();
    const before = structuredClone(materials);
    run();
    for (let i = 0; i < foliageNames.length; i++) {
        assert.deepEqual(materials[i], { ...before[i], transparencyMode: 1, alphaCutOff: 0.4 });
    }
    assert.deepEqual(materials.at(-1), before.at(-1));
});
test("missing or changed foliage fails before any material is modified", () => {
    for (const mutate of [materials => materials.splice(5, 1), materials => { materials[5].alpha = 0.5; },
        materials => { materials[5].albedoTexture = null; }, materials => { materials[5].transparencyMode = 1; },
        materials => materials.push({ ...materials[5] })]) {
        const { materials, run } = foliageFixture();
        mutate(materials);
        const before = structuredClone(materials);
        assert.throws(run, /original blended GLB material/);
        assert.deepEqual(materials, before);
    }
});
