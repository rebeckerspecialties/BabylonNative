// Validation only: vary scene backing size while leaving the display mode unchanged.
(function () {
    var text = globalThis._playgroundOptions && globalThis._playgroundOptions.profileRenderSize;
    if (text === undefined) return;
    var match = typeof text === "string" && /^([1-9][0-9]*)x([1-9][0-9]*)$/.exec(text);
    if (!match) throw new Error("PROFILE_RENDER_SIZE must be WIDTHxHEIGHT in physical pixels.");
    var width = Number(match[1]);
    var height = Number(match[2]);
    if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height)) {
        throw new Error("PROFILE_RENDER_SIZE dimensions must be safe positive integers.");
    }
    var configure = globalThis.__nativePlaygroundConfigureEngine;
    globalThis.__nativePlaygroundConfigureEngine = function (engine) {
        if (configure) configure(engine);
        var limit = engine.getCaps().maxTextureSize;
        if (!(width <= limit && height <= limit)) {
            throw new Error("PROFILE_RENDER_SIZE " + text + " exceeds maxTextureSize " + limit);
        }
        engine.setSize(width, height, true);
        if (engine.getRenderWidth() !== width || engine.getRenderHeight() !== height) {
            throw new Error("PROFILE_RENDER_SIZE was not applied exactly: " + text);
        }
        globalThis.__nativeArPortalBenchmarkExpectedSize = [width, height];
        globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-" + text;
        console.log("moving-benchmark:resolution=" + JSON.stringify({ width: width, height: height, maxTextureSize: limit,
            windowWidth: globalThis.innerWidth, windowHeight: globalThis.innerHeight, devicePixelRatio: globalThis.devicePixelRatio }));
    };
})();
