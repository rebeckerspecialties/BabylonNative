import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const BABYLON = createRequire(import.meta.url)("babylonjs");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_registration_validation.js", import.meta.url), "utf8");
function observable() {
    const callbacks = new Set();
    return { add(fn) { callbacks.add(fn); return fn; }, remove(fn) { callbacks.delete(fn); }, notify(value) { callbacks.forEach(fn => fn(value)); } };
}
function fixture(rightHanded = false, offset = BABYLON.Matrix.Identity()) {
    const messages = [];
    const spaces = [];
    let priorCalls = 0;
    // Reuse the same pose/matrix object, as NativeXR currently does.
    const pose = { transform: { matrix: new Float32Array(16) } };
    const base = BABYLON.Matrix.Translation(1, 2, 3);
    const reference = base.multiply(offset);
    const manager = { baseReferenceSpace: "base", referenceSpace: "offset", currentFrame: { getViewerPose(space) {
        spaces.push(space);
        pose.transform.matrix.set((space === "base" ? base : reference).m);
        return pose;
    } } };
    const scene = { useRightHandedSystem: rightHanded, activeCamera: { _xrSessionManager: manager },
        onBeforeCameraRenderObservable: observable(), onDisposeObservable: observable() };
    const view = offset.clone().invert();
    if (!rightHanded) view.toggleModelMatrixHandInPlace();
    const camera = { outputRenderTarget: { getSize: () => ({ width: 200, height: 100 }) }, globalPosition: { x: 1, y: 2, z: 3 },
        getViewMatrix: () => view, getProjectionMatrix: () => BABYLON.Matrix.Identity() };
    const context = vm.createContext({ BABYLON, __nativeArPortalStartProfile: () => priorCalls++ });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile(scene, message => messages.push(message));
    return { scene, camera, manager, spaces, messages, priorCalls };
}
test("chains profiling and ignores absent or non-XR cameras", () => {
    const f = fixture();
    for (const camera of [null, {}]) f.scene.onBeforeCameraRenderObservable.notify(camera);
    assert.equal(f.priorCalls, 1);
    assert.deepEqual(f.messages, []);
    assert.deepEqual(f.spaces, []);
});
test("compares native coordinates in either scene handedness", () => {
    for (const rightHanded of [false, true]) {
        const f = fixture(rightHanded);
        f.scene.onBeforeCameraRenderObservable.notify(f.camera);
        assert.equal(f.messages[0], "native-xr-portal:registration:position=1.0000,2.0000," + (rightHanded ? "3.0000" : "-3.0000") + ":pixel=100.0,100.0");
        assert.deepEqual(f.spaces, ["base", "offset"]);
    }
});
test("offset translation and rotation do not change the compared world point", () => {
    const offset = BABYLON.Matrix.RotationY(0.7).multiply(BABYLON.Matrix.Translation(0.02, 0.03, -0.04));
    for (const rightHanded of [false, true]) {
        const f = fixture(rightHanded, offset);
        f.scene.onBeforeCameraRenderObservable.notify(f.camera);
        assert.ok(f.messages[0].endsWith(":pixel=100.0,100.0"));
    }
});
test("missing tracking does not fabricate a registration sample", () => {
    const f = fixture();
    f.manager.currentFrame = { getViewerPose: () => null };
    f.scene.onBeforeCameraRenderObservable.notify(f.camera);
    assert.deepEqual(f.messages, []);
});
test("records successive frames and stops on disposal", () => {
    const f = fixture();
    f.scene.onBeforeCameraRenderObservable.notify(f.camera);
    f.scene.onBeforeCameraRenderObservable.notify(f.camera);
    f.scene.onDisposeObservable.notify();
    f.scene.onBeforeCameraRenderObservable.notify(f.camera);
    assert.equal(f.messages.length, 2);
});
