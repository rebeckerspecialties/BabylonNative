import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(new URL("../package.json", import.meta.url));
const BABYLON = require("babylonjs");
const ts = require("typescript");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_babylon_portal_demo.js", import.meta.url), "utf8");
const ast = ts.createSourceFile("portal.js", source, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
let createStressScene;
function visit(node) {
    if (ts.isFunctionDeclaration(node) && node.name?.text === "createPortalStressScene") {
        createStressScene = node.getText(ast);
    }
    ts.forEachChild(node, visit);
}
visit(ast);
assert.ok(createStressScene, "Portal stress scene must exist");

const glb = readFileSync(new URL("../Playground/Scripts/native_xr_portal_assets/native_xr_portal_hillvalley.astc.glb", import.meta.url));
const asset = JSON.parse(glb.subarray(20, 20 + glb.readUInt32LE(12)).toString("utf8"));
const sky = asset.nodes.find(node => node.name === "Ciel");
assert.ok(sky, "The authored sky dome must exist");
const primitive = asset.meshes[sky.mesh].primitives[0];

test("authored sky stays unlit and emissive, independently of environment reflections", () => {
    const material = asset.materials[primitive.material];
    assert.ok(material.extensions.KHR_materials_unlit);
    assert.deepEqual(material.emissiveFactor, [1, 1, 1]);
    assert.deepEqual(material.emissiveTexture, material.pbrMetallicRoughness.baseColorTexture);
    assert.equal(material.pbrMetallicRoughness.metallicFactor, 0);
});

for (const webgpu of [false, true]) {
    test(`stress camera preserves near/sky visibility and selects reversed depth only for WebGPU (${webgpu})`, async () => {
        const engine = new BABYLON.NullEngine();
        Object.defineProperty(engine, "isWebGPU", { value: webgpu });
        const stopBeforeLoading = new Error("Camera configuration complete");
        const context = vm.createContext({
            BABYLON: {
                Scene: BABYLON.Scene,
                Color4: BABYLON.Color4,
                ArcRotateCamera: BABYLON.ArcRotateCamera,
                Vector3: BABYLON.Vector3,
                SceneLoader: { ImportMeshAsync: () => Promise.reject(stopBeforeLoading) },
            },
            ASSET_ROOT_URL: "",
            HILL_VALLEY_FILE: "",
            reportStatus() {},
            ensureBrowserLocationForLoaders() {},
            installNativePortalDocumentShim() {},
            installNativeWebGPUImageLoadingShim() {},
            installNativePortalAssetUrlShim() {},
            createPortalEnvironmentTexture() {},
        });
        try {
            vm.runInContext(createStressScene, context);
            // Stop at asset loading so the real camera setup runs without a GPU or network.
            await assert.rejects(context.createPortalStressScene(engine), error => error === stopBeforeLoading);
            const camera = engine.scenes[0].activeCamera;
            assert.equal(engine.useReverseDepthBuffer, webgpu);
            camera.getViewMatrix(true);
            const position = asset.accessors[primitive.attributes.POSITION];
            assert.equal(position.componentType, 5122, "Update bounds decoding if the sky stops using normalized shorts");
            assert.equal(position.normalized, true);
            assert.equal(asset.nodes.some(node => node.children?.includes(asset.nodes.indexOf(sky))), false);
            const world = BABYLON.Matrix.FromArray(sky.matrix)
                .multiply(BABYLON.Matrix.Scaling(1, 1, -1))
                .multiply(BABYLON.Matrix.Translation(29, -1, -11));
            let farthest = 0;
            for (let corner = 0; corner < 8; corner++) {
                const point = BABYLON.Vector3.FromArray([0, 1, 2].map(axis =>
                    Math.max(-1, ((corner & (1 << axis)) ? position.max : position.min)[axis] / 32767)));
                const worldPoint = BABYLON.Vector3.TransformCoordinates(point, world);
                farthest = Math.max(farthest, BABYLON.Vector3.Distance(camera.position, worldPoint));
            }
            assert.ok(farthest > 500, "Fixture must exercise the large authored dome");
            assert.ok(camera.maxZ > farthest, `Far clip ${camera.maxZ} must include sky bounds at ${farthest}`);
            assert.equal(camera.minZ, 0.05, "Do not sacrifice nearby doorway geometry to fix the distant sky");
        } finally {
            engine.dispose();
        }
    });
}

test("the portal demo never overrides the AR camera with reversed depth", () => {
    assert.equal((source.match(/useReverseDepthBuffer\s*=/g) || []).length, 1);
    assert.match(createStressScene, /engine\.useReverseDepthBuffer = true/);
});
