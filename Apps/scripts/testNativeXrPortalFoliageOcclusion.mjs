import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(import.meta.url);
const B = require("babylonjs/babylon.max.js");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_foliage_occlusion.js", import.meta.url), "utf8");

function load(globals = {}) {
    const c = vm.createContext({ BABYLON: B, ...globals });
    vm.runInContext(source, c);
    return c;
}

// Small horizontal cards on a regular lattice filling a cube: a stand-in for a dense bush.
function bush(half, spacing) {
    const positions = [];
    const indices = [];
    for (let x = -half; x <= half; x += spacing) {
        for (let y = -half; y <= half; y += spacing) {
            for (let z = -half; z <= half; z += spacing) {
                const first = positions.length / 3;
                positions.push(x, y, z, x + 0.1, y, z, x, y, z + 0.1);
                indices.push(first, first + 1, first + 2);
            }
        }
    }
    return { positions: new Float32Array(positions), indices };
}

test("vertices deep inside foliage are darker than those open to the sky", () => {
    const c = load();
    const { positions, indices } = bush(1.5, 0.25);
    const visibility = c.__nativeArPortalBakeFoliageOcclusion(positions, indices);
    const at = (x, y, z) => {
        for (let i = 0; i < positions.length; i += 9) {
            if (positions[i] === x && positions[i + 1] === y && positions[i + 2] === z) return visibility[i / 3];
        }
        throw new Error("no card");
    };
    assert.equal(at(0, 1.5, 0), 1);
    assert.ok(at(0, 0, 0) < 0.5, String(at(0, 0, 0)));
    assert.ok(at(0, 0, 0) >= 0.35);
    // The underside is shaded by everything above it; a side face is open to half the sky.
    assert.ok(at(0, -1.5, 0) < at(1.5, 0, 0));
    assert.ok(at(1.5, 0, 0) < at(0, 1.5, 0));
    assert.ok(at(0, 0, 0) <= at(0, 0.75, 0));
});

test("a level card is fully open and unused vertices stay white", () => {
    const c = load();
    const positions = new Float32Array([0, 0, 0, 2, 0, 0, 0, 0, 2, 2, 0, 2, 9, 9, 9]);
    const visibility = c.__nativeArPortalBakeFoliageOcclusion(positions, [0, 1, 2, 1, 3, 2]);
    assert.deepEqual(Array.from(visibility), [1, 1, 1, 1, 1]);
});

test("a mesh too large to voxelize is left alone", () => {
    const c = load();
    const positions = new Float32Array([0, 0, 0, 100, 0, 0, 0, 100, 100]);
    assert.equal(c.__nativeArPortalBakeFoliageOcclusion(positions, [0, 1, 2]), null);
});

test("replaces the fixed cutout visibility unless one was chosen, and validates settings", () => {
    assert.equal(load().__nativeArPortalCutoutAoVisibility, 1);
    assert.equal(load({ __nativeArPortalCutoutAoVisibility: 0.9 }).__nativeArPortalCutoutAoVisibility, 0.9);
    for (const settings of [{ cellSize: 0 }, { steps: 0 }, { cellTransmittance: 1 }, { minimumVisibility: 0 }]) {
        assert.throws(() => load({ __nativeArPortalFoliageOcclusion: settings }), /Invalid portal foliage occlusion settings/);
    }
});

test("bakes double-sided alpha-textured meshes once and shades them through a material plugin", () => {
    const c = load();
    const engine = new B.NullEngine();
    const scene = new B.Scene(engine);
    const { positions, indices } = bush(1, 0.25);
    const make = (name, configure) => {
        const mesh = new B.Mesh(name, scene);
        mesh.setVerticesData(B.VertexBuffer.PositionKind, Array.from(positions));
        mesh.setIndices(indices);
        mesh.position.x = 5;
        const material = new B.PBRMaterial(name, scene);
        material.albedoTexture = new B.Texture(null, scene);
        material.albedoTexture.hasAlpha = true;
        material.transparencyMode = B.PBRMaterial.PBRMATERIAL_ALPHABLEND;
        material.backFaceCulling = false;
        // The null engine builds GLSL materials; the plugin is written for the WebGPU engine's WGSL ones.
        material._shaderLanguage = B.ShaderLanguage.WGSL;
        configure(material);
        mesh.material = material;
        return mesh;
    };
    const foliage = make("foliage", () => {});
    const sign = make("sign", (material) => { material.backFaceCulling = true; });
    const opaque = make("opaque", (material) => { material.transparencyMode = B.PBRMaterial.PBRMATERIAL_OPAQUE; });
    const lines = [];
    c.__nativeArPortalStartProfile(scene, (line) => lines.push(line));
    const baked = foliage.getVerticesData("portalFoliageVisibility");
    assert.equal(baked.length, positions.length / 3);
    assert.ok(Math.min(...baked) < 0.6 && Math.max(...baked) === 1);
    assert.equal(foliage.material.pluginManager.getPlugin("PortalFoliageOcclusion").getClassName(), "PortalFoliageOcclusionPlugin");
    for (const mesh of [sign, opaque]) {
        assert.equal(mesh.isVerticesDataPresent("portalFoliageVisibility"), false);
        assert.equal(mesh.material.pluginManager?.getPlugin("PortalFoliageOcclusion") ?? null, null);
    }
    assert.match(lines[0], /^native-xr-portal:foliage-occlusion=baked:meshes=1:vertices=\d+:darkest=0\.\d\d:ms=\d+$/);
    // A second start leaves the baked mesh as it is.
    c.__nativeArPortalStartProfile(scene, (line) => lines.push(line));
    assert.match(lines[1], /meshes=0:/);
    const plugin = foliage.material.pluginManager.getPlugin("PortalFoliageOcclusion");
    assert.equal(plugin.isCompatible(B.ShaderLanguage.WGSL), true);
    assert.equal(plugin.isCompatible(B.ShaderLanguage.GLSL), false);
    assert.match(plugin.getCustomCode("fragment").CUSTOM_FRAGMENT_BEFORE_FRAGCOLOR, /finalColor\.rgb \* fragmentInputs\.vPortalFoliageVisibility/);
    assert.match(plugin.getCustomCode("vertex").CUSTOM_VERTEX_DEFINITIONS, /attribute portalFoliageVisibility: f32;/);
    engine.dispose();
});
