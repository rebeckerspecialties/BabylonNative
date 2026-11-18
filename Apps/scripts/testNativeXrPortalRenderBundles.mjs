import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import test from "node:test";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_render_bundle_validation.js", import.meta.url), "utf8");
test("bundle comparison opts into the existing engine path and preserves setup ordering", () => {
    const events = [];
    const engine = { isWebGPU: true, compatibilityMode: false };
    const scene = { getEngine: () => engine };
    const status = message => events.push(message);
    const c = vm.createContext({ __nativeArPortalStartProfile: (s, callback) => {
        assert.equal(s, scene);
        assert.equal(callback, status);
        events.push("setup");
    } });
    vm.runInContext(source, c);
    c.__nativeArPortalStartProfile(scene, status);
    assert.equal(c.__nativeArPortalUseWebGPUFastPath, true);
    assert.deepEqual(events, ["setup", "native-xr-portal:render-bundle-comparison=dynamic"]);
    assert.equal(c.__nativeArPortalBenchmarkSuffix, "-render-bundles");
});
test("bundle comparison rejects fallback engines, snapshots and freezing", () => {
    for (const engine of [{}, { isWebGPU: true, compatibilityMode: true }, { isWebGPU: true, compatibilityMode: false, snapshotRendering: true }]) {
        const c = vm.createContext({});
        vm.runInContext(source, c);
        assert.throws(() => c.__nativeArPortalStartProfile({ getEngine: () => engine }, () => {}), /dynamic WebGPU rendering/);
    }
    for (const flag of ["__nativeArPortalFreezeMaterials", "__nativeArPortalUseSceneStaticFastPath"]) {
        const c = vm.createContext({ [flag]: true });
        vm.runInContext(source, c);
        assert.throws(() => c.__nativeArPortalStartProfile({ getEngine: () => ({ isWebGPU: true, compatibilityMode: false }) }, () => {}), /without snapshots or freezing/);
    }
});
