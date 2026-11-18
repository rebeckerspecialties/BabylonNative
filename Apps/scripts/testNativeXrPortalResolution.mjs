import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../Playground/Scripts/native_xr_portal_resolution_validation.js", import.meta.url), "utf8");
function fixture(value, limit = 8192, applies = true) {
    const calls = [];
    let width = 1170;
    let height = 2532;
    const configure = () => calls.push("previous");
    const context = vm.createContext({ _playgroundOptions: { profileRenderSize: value }, console: { log() {} },
        __nativePlaygroundConfigureEngine: configure, __nativeArPortalBenchmarkSuffix: "-ao" });
    const engine = { getCaps: () => ({ maxTextureSize: limit }),
        setSize(w, h, force) { calls.push([w, h, force]); if (applies) { width = w; height = h; } },
        getRenderWidth: () => width, getRenderHeight: () => height };
    return { context, calls, configure, load: () => vm.runInContext(source, context),
        apply: () => context.__nativePlaygroundConfigureEngine(engine) };
}

test("absent resolution preserves the existing configuration hook", () => {
    const f = fixture(undefined);
    f.load();
    assert.equal(f.context.__nativePlaygroundConfigureEngine, f.configure);
    assert.equal(f.context.__nativeArPortalBenchmarkExpectedSize, undefined);
});

for (const [width, height] of [[1920, 1080], [2560, 1440], [3840, 2160]]) {
    test(`physical backing resolution ${width}x${height} is set once after prior configuration`, () => {
        const f = fixture(`${width}x${height}`);
        f.load(); f.apply();
        assert.deepEqual(f.calls, ["previous", [width, height, true]]);
        assert.deepEqual(Array.from(f.context.__nativeArPortalBenchmarkExpectedSize), [width, height]);
        assert.equal(f.context.__nativeArPortalBenchmarkSuffix, `-ao-${width}x${height}`);
    });
}

test("invalid or unsupported sizes fail rather than silently lowering resolution", () => {
    for (const value of [null, 1080, "", "1080p", "0x1080", "-1x1", "1.5x2", "1920x1080 ", "9007199254740992x1"]) {
        assert.throws(() => fixture(value).load(), /PROFILE_RENDER_SIZE/);
    }
    const limited = fixture("3840x2160", 2048);
    limited.load();
    assert.throws(limited.apply, /exceeds maxTextureSize 2048/);
    assert.deepEqual(limited.calls, ["previous"]);
    const ignored = fixture("1920x1080", 8192, false);
    ignored.load();
    assert.throws(ignored.apply, /not applied exactly/);
    assert.equal(ignored.context.__nativeArPortalBenchmarkExpectedSize, undefined);
});
