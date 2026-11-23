import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_ao_diagnostics.js", import.meta.url), "utf8");
function fixture({ available = true } = {}) {
    let now = 0;
    const messages = [];
    const calls = [];
    const observable = () => ({
        observers: [],
        add(callback) { this.observers.push(callback); return callback; },
        remove(callback) { this.observers = this.observers.filter(value => value !== callback); },
        notify(value) { this.observers.forEach(callback => callback(value)); },
    });
    const combine = { updateEffect: (...args) => calls.push(args), onApplyObservable: observable() };
    const pipeline = { name: "native-xr-portal-ssao2", _ssaoCombinePostProcess: combine, _ssaoPostProcess: {} };
    const textures = [{ name: "depth" }, { name: "normal" }];
    const scene = {
        activeCamera: { name: "camera", viewport: {}, getProjectionMatrix: () => ({ m: [1, 2, 3] }) },
        postProcessRenderPipelineManager: { supportedPipelines: available ? [pipeline] : [] },
        geometryBufferRenderer: { getGBuffer: () => ({ textures, getSize: () => ({ width: 100, height: 200 }) }) },
        onDisposeObservable: observable(),
    };
    let profileCalls = 0;
    const context = vm.createContext({
        performance: { now: () => now },
        BABYLON: { ShaderStore: { ShadersStoreWGSL: {} } },
        __nativeArPortalStartProfile() { profileCalls++; },
    });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { scene, combine, pipeline, messages, calls, profileCalls, textures, frame(time) {
        now = time;
        const values = {};
        combine.onApplyObservable.notify({
            setFloat(name, value) { values[name] = value; },
            setTexture(name, value) { values[name] = value; },
            setTextureFromPostProcessOutput(name, value) { values[name] = value; },
        });
        return values;
    } };
}

test("absent AO leaves the scene and shaders alone", () => {
    const f = fixture({ available: false });
    assert.equal(f.profileCalls, 1);
    assert.deepEqual(f.calls, []);
    assert.deepEqual(f.messages, ["native-xr-portal:ao-diagnostic=unavailable"]);
});

test("diagnoses real attachments in order and restores combined output", () => {
    const f = fixture();
    assert.equal(f.profileCalls, 1);
    assert.equal(f.calls.length, 1);
    assert.ok(f.calls[0][1].includes("scale"));
    assert.ok(f.calls[0][2].includes("textureSampler"));
    assert.equal(f.calls[0][7], "nativePortalAoDiagnostic");
    assert.equal(f.frame(0).diagnosticMode, 0);
    assert.equal(f.frame(14999).diagnosticMode, 0);
    for (const [time, mode] of [[15000, 2], [30000, 3], [45000, 5], [60000, 1], [75000, 4], [90000, 0], [120000, 0]]) {
        const values = f.frame(time);
        assert.equal(values.diagnosticMode, mode);
        assert.equal(values.depthSampler, f.textures[0]);
        assert.equal(values.normalSampler, f.textures[1]);
        assert.equal(values.rawAoSampler, f.pipeline._ssaoPostProcess);
    }
    assert.equal(f.messages.length, 7);
    assert.match(f.messages.at(-1), /combined-restored/);
});

test("disposal removes the diagnostic binding observer", () => {
    const f = fixture();
    f.scene.onDisposeObservable.notify();
    assert.equal(f.combine.onApplyObservable.observers.length, 0);
    assert.deepEqual(f.frame(60000), {});
});
