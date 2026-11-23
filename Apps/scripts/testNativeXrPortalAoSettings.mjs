import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(new URL("../package.json", import.meta.url));
const ts = require("typescript");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_babylon_portal_demo.js", import.meta.url), "utf8");
const ast = ts.createSourceFile("portal.js", source, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
let configure;
function visit(node) {
    if (ts.isFunctionDeclaration(node) && node.name?.text === "enablePortalSSAO2") {
        configure = node.getText(ast);
    }
    ts.forEachChild(node, visit);
}
visit(ast);
assert.ok(configure, "Portal AO configuration function must exist");

function fixture(overrides = {}, { existingGeometry = true, availableGeometry = true, pipelineAvailable = true, existingPrePass = false, availablePrePass = true } = {}) {
    const messages = [];
    const calls = [];
    const normalSpacesAtConstruction = [];
    const geometry = {};
    const prePass = {};
    const camera = {};
    const scene = {
        geometryBufferRenderer: existingGeometry ? geometry : null,
        prePassRenderer: existingPrePass ? prePass : null,
        enablePrePassRenderer() {
            calls.push("enablePrePass");
            return availablePrePass ? prePass : null;
        },
        enableGeometryBufferRenderer() {
            calls.push("enableGeometry");
            return availableGeometry ? geometry : null;
        },
    };
    class Pipeline {
        epsilon = 0;
        bilateralSoften = 0;
        bilateralTolerance = 0;
        constructor(...args) {
            calls.push(args);
            normalSpacesAtConstruction.push((args[4] === false ? prePass : geometry).generateNormalsInWorldSpace);
        }
    }
    const context = vm.createContext({
        ...overrides,
        BABYLON: { SSAO2RenderingPipeline: pipelineAvailable ? Pipeline : undefined },
        reportStatus: message => messages.push(message),
    });
    vm.runInContext(source, context);
    // Evaluate the real configuration routine without creating an XR session.
    vm.runInContext(configure, context);
    return { context, calls, messages, scene, camera, geometry, prePass, normalSpacesAtConstruction, pipeline: context.enablePortalSSAO2(scene, camera) };
}

function aoFactor(pipeline, occlusion) {
    return Math.max(0, Math.min(1, 1 - pipeline.totalStrength * occlusion + pipeline.base));
}

test("default AO preserves unoccluded color and darkens partial and full occlusion", () => {
    const { pipeline, messages } = fixture();
    assert.equal(pipeline.base, 0);
    assert.equal(aoFactor(pipeline, 0), 1);
    assert.equal(aoFactor(pipeline, 0.5), 0.79);
    assert.ok(Math.abs(aoFactor(pipeline, 1) - 0.58) < 1e-12);
    assert.match(messages[0], /strength=0.42:base=0.00/);
});

test("explicit zero base and strength are not replaced by fallback values", () => {
    const { pipeline } = fixture({ __nativeArPortalSSAOBase: 0, __nativeArPortalSSAOStrength: 0 });
    assert.equal(pipeline.base, 0);
    assert.equal(pipeline.totalStrength, 0);
    assert.equal(aoFactor(pipeline, 1), 1);
});

test("nonzero overrides remain configurable and values are bounded", () => {
    const custom = fixture({ __nativeArPortalSSAOBase: 0.1, __nativeArPortalSSAOStrength: 0.8 }).pipeline;
    assert.equal(custom.base, 0.1);
    assert.equal(custom.totalStrength, 0.8);
    assert.equal(fixture({ __nativeArPortalSSAOBase: 2 }).pipeline.base, 1);
    const negative = fixture({ __nativeArPortalSSAOBase: -1, __nativeArPortalSSAOStrength: -1 }).pipeline;
    assert.equal(negative.base, 0);
    assert.equal(negative.totalStrength, 0);
});

test("preserves the existing camera, geometry renderer, resolution and sample budget", () => {
    const { pipeline, calls, scene, camera, geometry } = fixture();
    assert.equal(calls.length, 1);
    assert.equal(calls[0][1], scene);
    assert.equal(calls[0][2].ssaoRatio, 0.30);
    assert.equal(calls[0][2].blurRatio, 0.45);
    assert.equal(calls[0][3][0], camera);
    assert.equal(calls[0][4], geometry);
    assert.equal(pipeline.samples, 8);
    assert.equal(pipeline.textureSamples, 1);
});

test("creates geometry only when absent", () => {
    const { pipeline, calls, geometry } = fixture({}, { existingGeometry: false });
    assert.ok(pipeline);
    assert.equal(calls[0], "enableGeometry");
    assert.equal(calls[1][4], geometry);
});

test("disabled or unavailable AO does not create a geometry pass", () => {
    for (const f of [fixture({ __nativeArPortalUseSSAO2: false }), fixture({}, { pipelineAvailable: false })]) {
        assert.equal(f.pipeline, null);
        assert.deepEqual(f.calls, []);
    }
});

test("missing geometry reports the reason without creating a pipeline", () => {
    const f = fixture({}, { existingGeometry: false, availableGeometry: false });
    assert.equal(f.pipeline, null);
    assert.deepEqual(f.calls, ["enableGeometry"]);
    assert.deepEqual(f.messages, ["native-xr-portal:ssao2=disabled:no-geometry-buffer"]);
});

test("opt-in pre-pass uses the public pipeline path without creating geometry", () => {
    for (const existingPrePass of [false, true]) {
        const f = fixture({ __nativeArPortalSSAOUsePrePass: true }, { existingGeometry: false, existingPrePass });
        assert.ok(f.pipeline);
        assert.equal(f.calls.includes("enableGeometry"), false);
        assert.equal(f.calls.includes("enablePrePass"), !existingPrePass);
        const args = f.calls.at(-1);
        assert.equal(args[1], f.scene);
        assert.equal(args[3][0], f.camera);
        assert.equal(args[4], false);
        assert.equal(args[2].ssaoRatio, 0.30);
        assert.equal(args[2].blurRatio, 0.45);
        assert.equal(f.pipeline.samples, 8);
        assert.equal(f.pipeline.base, 0);
        assert.equal(f.messages.at(-1), "native-xr-portal:ssao2-renderer=pre-pass");
    }
});

test("unavailable pre-pass is reported without silently changing the workload", () => {
    const f = fixture({ __nativeArPortalSSAOUsePrePass: true }, { availablePrePass: false });
    assert.equal(f.pipeline, null);
    assert.deepEqual(f.calls, ["enablePrePass"]);
    assert.deepEqual(f.messages, ["native-xr-portal:ssao2=disabled:no-pre-pass"]);
});

test("world normals are selected before AO compilation for either renderer", () => {
    for (const usePrePass of [false, true]) {
        const f = fixture({ __nativeArPortalWorldNormals: true, __nativeArPortalSSAOUsePrePass: usePrePass });
        assert.deepEqual(f.normalSpacesAtConstruction, [true]);
        assert.equal((usePrePass ? f.prePass : f.geometry).generateNormalsInWorldSpace, true);
        assert.equal((usePrePass ? f.geometry : f.prePass).generateNormalsInWorldSpace, undefined);
        assert.equal(f.pipeline.samples, 8);
        assert.equal(f.calls.at(-1)[2].ssaoRatio, 0.30);
        assert.equal(f.calls.at(-1)[2].blurRatio, 0.45);
    }
});

test("world-normal experiment remains opt-in and disabled AO has no side effects", () => {
    for (const value of [undefined, false, 0, "true"]) {
        const f = fixture({ __nativeArPortalWorldNormals: value });
        assert.deepEqual(f.normalSpacesAtConstruction, [undefined]);
    }
    const f = fixture({ __nativeArPortalWorldNormals: true, __nativeArPortalUseSSAO2: false });
    assert.deepEqual(f.normalSpacesAtConstruction, []);
    assert.equal(f.geometry.generateNormalsInWorldSpace, undefined);
});
