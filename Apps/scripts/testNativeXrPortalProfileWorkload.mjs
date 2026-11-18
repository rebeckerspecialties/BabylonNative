import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_profile_workload.js", import.meta.url), "utf8");
test("estimated-placement prelude is bounded and labels non-floor evidence", () => {
    const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_profile_estimated_placement.js", import.meta.url), "utf8");
    const messages = [];
    const context = vm.createContext({ console: { log: message => messages.push(message) } });
    vm.runInContext(source, context);
    assert.equal(context.__nativeArPortalAllowFallbackPlacement, true);
    assert.equal(context.__nativeArPortalExitDelayMs, 180000);
    assert.match(messages[0], /profile-placement=estimated:not-floor-validation/);
    assert.deepEqual(Object.keys(context).sort(), ["__nativeArPortalAllowFallbackPlacement", "__nativeArPortalExitDelayMs", "console"]);
});

for (const present of [false, true]) {
    test(`fixed workload keeps resolution and sample budget with AO ${present ? "present" : "absent"}`, () => {
        let calls = 0;
        const pipeline = { name: "native-xr-portal-ssao2", samples: 8, totalStrength: 0.42, epsilon: 0.04 };
        const canvas = { width: 4239, height: 2384, clientWidth: 1280, clientHeight: 720 };
        const engine = { getRenderingCanvas: () => canvas, getHardwareScalingLevel: () => 1 / 3 };
        const scene = { postProcessRenderPipelineManager: { supportedPipelines: present ? [pipeline] : [] }, getEngine: () => engine };
        const messages = [];
        const context = vm.createContext({ __nativeArPortalStartProfile: () => calls++, devicePixelRatio: 3 });
        vm.runInContext(source, context);
        assert.equal(context.__nativeArPortalSSAOStrength, 1.2);
        assert.equal(context.__nativeArPortalLogMeshDiagnostics, false);
        assert.equal(context.__nativeArPortalLogAoCoordinates, false);
        context.__nativeArPortalStartProfile(scene, message => messages.push(message));
        assert.equal(calls, 1);
        assert.equal(pipeline.totalStrength, present ? 1.2 : 0.42);
        assert.equal(pipeline.epsilon, present ? 0.01 : 0.04);
        assert.equal(pipeline.samples, 8);
        assert.deepEqual(canvas, { width: 4239, height: 2384, clientWidth: 1280, clientHeight: 720 });
        assert.match(messages[0], /profile-workload=/);
    });
}
