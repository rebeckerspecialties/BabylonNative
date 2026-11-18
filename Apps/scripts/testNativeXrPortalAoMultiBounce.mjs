import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const B = createRequire(import.meta.url)("babylonjs/babylon.max.js");
const read = (name) => readFileSync(new URL("../Playground/Scripts/" + name, import.meta.url), "utf8");
const combined = "vec4f(sceneColor.rgb * ssaoColor.r, sceneColor.a)";

function fixture(colorReuseShader) {
    const compiled = [];
    const pipeline = { name: "native-xr-portal-ssao2", _ssaoCombinePostProcess: { updateEffect: (...args) => compiled.push(args[7]) } };
    const store = { portalSsaoColorReusePixelShader: colorReuseShader };
    const context = vm.createContext({ BABYLON: { ShaderStore: { ShadersStoreWGSL: store } } });
    vm.runInContext(read("native_xr_portal_ao_multibounce.js"), context);
    const statuses = [];
    const start = () => context.__nativeArPortalStartProfile(
        { postProcessRenderPipelineManager: { supportedPipelines: [pipeline] } }, (message) => statuses.push(message));
    return { store, compiled, statuses, start, context };
}

// The combine shader as the R8 and color-reuse preludes leave it.
function colorReuseShader() {
    const store = { ...B.ShaderStore.ShadersStoreWGSL };
    vm.runInContext(read("native_xr_portal_ao_r8_validation.js"), vm.createContext({ BABYLON: { ShaderStore: { ShadersStoreWGSL: store } } }));
    assert.equal(store.ssaoCombinePixelShader.split(combined).length, 2);
    return store.ssaoCombinePixelShader;
}

test("replaces only the final product and compiles under its own key", () => {
    const source = colorReuseShader();
    const f = fixture(source);
    f.start();
    assert.deepEqual(f.compiled, ["portalSsaoMultiBounce"]);
    assert.equal(f.store.portalSsaoColorReusePixelShader, source);
    const shader = f.store.portalSsaoMultiBouncePixelShader;
    assert.equal(shader.split(combined).length, 1);
    assert.equal(shader.match(/@fragment/g).length, 1);
    assert.match(shader, /sceneColor\.rgb \* portalMultiBounce\(ssaoColor\.r, clamp\(sceneColor\.rgb, vec3f\(0\.0\), vec3f\(1\.0\)\)\), sceneColor\.a\)/);
    assert.equal(f.context.__nativeArPortalBenchmarkSuffix, "-multi-bounce");
    assert.equal(f.statuses.length, 1);
});

test("fails closed without the R8 and color-reuse combine contract", () => {
    const source = colorReuseShader();
    for (const drifted of [undefined, source.replace(combined, "sceneColor * ssaoColor"), source + "\n@fragment\n"]) {
        const f = fixture(drifted);
        assert.throws(f.start, /Multi-bounce AO requires/);
        assert.deepEqual(f.compiled, []);
        assert.equal(f.store.portalSsaoMultiBouncePixelShader, undefined);
    }
});

test("the fit never darkens, keeps the end points and brightens with reflectance", () => {
    const f = fixture(colorReuseShader());
    f.start();
    const body = /fn portalMultiBounce\(visibility: f32, reflectance: vec3f\) -> vec3f \{([^}]*)\}/.exec(f.store.portalSsaoMultiBouncePixelShader)[1];
    const scalar = new Function("visibility", "reflectance", "const vec3f = (x) => x, max = Math.max;" + body.replace(/\blet\b/g, "const"));
    for (const reflectance of [0, 0.2, 0.5, 0.8, 1]) {
        let previous = 0;
        for (let visibility = 0; visibility <= 1.0001; visibility += 0.05) {
            const value = scalar(visibility, reflectance);
            assert.ok(value >= visibility && value <= 1.002, `${reflectance} ${visibility}: ${value}`);
            assert.ok(value >= previous);
            assert.ok(value >= scalar(visibility, Math.max(0, reflectance - 0.2)) - 1e-9);
            previous = value;
        }
        assert.equal(scalar(0, reflectance), 0);
        assert.ok(Math.abs(scalar(1, reflectance) - 1) < 2e-3);
    }
    // A black surface returns no light.
    assert.ok(Math.abs(scalar(0.5, 0) - 0.5) < 1e-9);
    assert.ok(scalar(0.5, 0.8) > 0.8);
});
