// Diagnostic only. Load after moving_benchmark, before the demo. Requires
// JSC_exposeProfilersOnGlobalObject=true; never use this run as an FPS result.
(function () {
    var required = ["__enableSamplingProfiler", "__disableSamplingProfiler", "__dumpAndClearSamplingProfilerSamples"];
    required.forEach(function (name) {
        if (typeof globalThis[name] !== "function") throw new Error("JSC sampling unavailable: missing " + name);
    });
    var previousStart = globalThis.__nativeArPortalStartProfile;
    if (typeof previousStart !== "function") throw new Error("JSC sampling must load after the moving-camera benchmark.");
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-jsc-diagnostic";
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        var timer;
        var started = false;
        var active = false;
        var disposed = false;
        var prefix = "bn-portal-jsc-" + Date.now();
        function stop(complete) {
            if (!active) return;
            active = false;
            globalThis.__disableSamplingProfiler();
            status("jsc-sampler:stop=" + JSON.stringify({ epochMs: Date.now(), complete: complete, prefix: prefix }));
            // JSC writes a temporary file in the app's writable temp directory.
            globalThis.__dumpAndClearSamplingProfilerSamples(prefix);
        }
        scene.onDisposeObservable.addOnce(function () {
            disposed = true;
            clearTimeout(timer);
            stop(false);
        });
        previousStart(scene, function (message) {
            status(message);
            if (!disposed && !started && message.indexOf("moving-benchmark:timing=") === 0) {
                started = true;
                timer = setTimeout(function () {
                    if (disposed) return;
                    globalThis.__enableSamplingProfiler();
                    active = true;
                    status("jsc-sampler:start=" + JSON.stringify({ epochMs: Date.now(), durationMs: 20000, prefix: prefix }));
                    timer = setTimeout(function () { stop(true); }, 20000);
                }, 5000);
            }
        });
    };
})();
