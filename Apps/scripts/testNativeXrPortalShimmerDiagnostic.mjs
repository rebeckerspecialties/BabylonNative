import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_shimmer_diagnostic.js", import.meta.url), "utf8");

test("diagnostic removes only AO strength and restores the exact original value", () => {
    const pipeline = { name: "native-xr-portal-ssao2", totalStrength: 1.6 };
    const log = [];
    let tick;
    let previous = 0;
    const context = vm.createContext({ __nativeArPortalStartProfile() { previous++; } });
    vm.runInContext(source, context);
    context.__nativeArPortalStartProfile({
        postProcessRenderPipelineManager: { supportedPipelines: [pipeline] }, textures: [],
        onBeforeRenderObservable: { add(callback) { tick = callback; } },
        getEngine() { return { getDeltaTime() { return 1000; } }; },
    }, message => log.push(message));
    for (let i = 0; i < 59; i++) tick();
    assert.equal(pipeline.totalStrength, 1.6);
    tick();
    assert.equal(pipeline.totalStrength, 0);
    for (let i = 0; i < 60; i++) tick();
    assert.equal(pipeline.totalStrength, 1.6);
    assert.equal(previous, 1);
    assert.equal(log.length, 3);
    assert.match(log[1], /ao=off/);
    assert.match(log[2], /ao=on/);
});

test("missing pipeline fails instead of producing a misleading AO-off result", () => {
    const context = vm.createContext({});
    vm.runInContext(source, context);
    assert.throws(() => context.__nativeArPortalStartProfile({ postProcessRenderPipelineManager: { supportedPipelines: [] } }, () => {}), /requires the AO comparison pipeline/);
});
