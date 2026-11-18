import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const publishedShader = require("babylonjs/babylon.max.js").ShaderStore.ShadersStoreWGSL.ssao2PixelShader;

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_ao_projection_validation.js", import.meta.url), "utf8");
function observable() {
    const observers = new Set();
    return { get size() { return observers.size; }, add(fn) { observers.add(fn); return fn; }, remove(fn) { observers.delete(fn); },
        notify(value) { observers.forEach(fn => fn(value)); } };
}
function fixture({ available = true, prior = true, coordinates = true } = {}) {
    const projection = [4, 0, 0, 0, 0, 2, 0, 0, 0.02, -0.01, 1, 1, 0, 0, -0.1, 0];
    const ssao = { onBeforeRenderObservable: observable(), onAfterRenderObservable: observable() };
    const gbuffer = { onAfterRenderObservable: observable() };
    const messages = [];
    let profileCalls = 0;
    const scene = {
        activeCamera: { mode: 0, getProjectionMatrix: () => ({ m: projection }) },
        geometryBufferRenderer: { getGBuffer: () => gbuffer },
        getEngine: () => ({ getAspectRatio: () => 2 }),
        postProcessRenderPipelineManager: { supportedPipelines: available ? [{ name: "native-xr-portal-ssao2", _ssaoPostProcess: ssao }] : [] },
        onDisposeObservable: observable(),
    };
    const store = { ssao2PixelShader: publishedShader };
    const context = vm.createContext({ BABYLON: { Camera: { PERSPECTIVE_CAMERA: 0 }, ShaderStore: { ShadersStoreWGSL: store } },
        __nativeArPortalLogAoCoordinates: coordinates,
        __nativeArPortalStartProfile: prior ? () => profileCalls++ : undefined });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { scene, projection, messages, profileCalls, store, ssao, gbuffer, frame() {
        const values = {};
        ssao.onBeforeRenderObservable.notify({ setFloat(name, value) { values[name] = value; } });
        return values;
    } };
}

test("no AO does not install a correction, with and without an earlier prelude", () => {
    for (const prior of [false, true]) {
        const f = fixture({ available: false, prior });
        assert.equal(f.profileCalls, Number(prior));
        assert.deepEqual(f.frame(), {});
        assert.deepEqual(f.messages, ["native-xr-portal:ao-projection-validation=unavailable"]);
    }
});

test("validation bounds the sample array while retaining depth rejection and normal conversion", () => {
    const shader = fixture().store.ssao2PixelShader;
    assert.match(shader, /uniforms\.sampleSphere\[\(i \+ i32\(random.y \* f32\(SAMPLES\)\)\) % SAMPLES\]/);
    assert.match(shader, /projection\[2\]\.x \* depthSign/);
    assert.match(shader, /projection\[2\]\.y \* depthSign/);
    assert.match(shader, /if \(depth==0.0\)/);
    assert.match(shader, /normal=normalize\(uniforms.normalWorldToView\*normal\)/);
    assert.match(shader, /scales\[\(i\+ i32\(random.x\*16.0\)\) % 16\]/);
});

test("shader drift fails instead of silently leaving half a correction applied", () => {
    const store = { ssao2PixelShader: publishedShader.replace("uniforms.xViewport", "changedViewport") };
    assert.throws(() => vm.runInNewContext(source, { BABYLON: { ShaderStore: { ShadersStoreWGSL: store } } }), /no longer matches/);
    assert.equal(store.ssao2PixelShader.includes("% SAMPLES"), false);
});

test("one perspective frame uses projection scales after normal effect binding", () => {
    const f = fixture();
    assert.deepEqual(f.messages, []);
    assert.deepEqual(f.frame(), { xViewport: 0.25, yViewport: 0.5 });
    assert.equal(f.profileCalls, 1);
    assert.match(f.messages[0], /screenAspect=2:projectionAspect=0.5:principalPoint=0.02,-0.01/);
});

test("later frames use the live projection, including a flipped vertical projection", () => {
    const f = fixture();
    f.frame();
    f.projection[0] = 2;
    f.projection[5] = -4;
    assert.deepEqual(f.frame(), { xViewport: 0.5, yViewport: -0.25 });
    assert.equal(f.messages.length, 1);
});

test("orthographic and absent cameras retain their existing bindings", () => {
    const f = fixture();
    f.scene.activeCamera.mode = 1;
    assert.deepEqual(f.frame(), {});
    f.scene.activeCamera = null;
    assert.deepEqual(f.frame(), {});
    assert.deepEqual(f.messages, []);
});

test("invalid projection scales fail with a useful diagnostic", () => {
    for (const value of [0, NaN, Infinity, -Infinity]) {
        const f = fixture();
        f.projection[0] = value;
        assert.throws(f.frame, /finite, nonzero projection scales/);
    }
});

test("scene disposal stops the correction", () => {
    const f = fixture();
    f.scene.onDisposeObservable.notify();
    assert.deepEqual(f.frame(), {});
    assert.deepEqual(f.messages, []);
});

test("profiling can omit coordinate snapshots without losing the shader correction", () => {
    for (const coordinates of [false, true]) {
        const f = fixture({ coordinates });
        assert.equal(f.gbuffer.onAfterRenderObservable.size, Number(coordinates));
        assert.equal(f.ssao.onAfterRenderObservable.size, Number(coordinates));
        assert.deepEqual(f.frame(), { xViewport: 0.25, yViewport: 0.5 });
        f.scene.onDisposeObservable.notify();
        assert.equal(f.gbuffer.onAfterRenderObservable.size, 0);
        assert.equal(f.ssao.onAfterRenderObservable.size, 0);
    }
});
