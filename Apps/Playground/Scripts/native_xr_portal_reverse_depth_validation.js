// Validation only: preserve the original near/far visibility while improving
// floating-point depth precision. The XR projection contract is not changed.
(function () {
    var configure = globalThis.__nativePlaygroundConfigureEngine;
    globalThis.__nativePlaygroundConfigureEngine = function (engine) {
        if (configure) configure(engine);
        if (globalThis.__nativeArPortalForceNonXrStress !== true || !engine.isWebGPU) {
            throw new Error("Reversed-depth comparison requires non-XR WebGPU.");
        }
        engine.useReverseDepthBuffer = true;
        console.log("shimmer-diagnostic:reverse-depth=true:near-far-unchanged");
    };
})();
