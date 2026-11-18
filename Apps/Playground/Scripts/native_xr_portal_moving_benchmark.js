// Non-AR validation only. Load after the AO workload prelude and before the demo.
globalThis.__nativeArPortalForceNonXrStress = true;
globalThis.__nativeArPortalSSAOUsePrePass = true;
globalThis.__nativeArPortalExitDelayMs = (globalThis.__nativeArPortalBenchmarkDurationMs || 45000) + 45000;
globalThis.__nativeArPortalLogMeshDiagnostics = false;
globalThis.__nativePlaygroundStatus = function (message) { console.log(message); };
if (typeof __nativeValidationSetFrameTimerEnabled === "function") {
    __nativeValidationSetFrameTimerEnabled(true);
}
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var minimum = new BABYLON.Vector3(Infinity, Infinity, Infinity);
        var maximum = new BABYLON.Vector3(-Infinity, -Infinity, -Infinity);
        scene.meshes.forEach(function (mesh) {
            if (!/d1_|delorean/i.test(mesh.name) || !mesh.getTotalVertices()) return;
            mesh.computeWorldMatrix(true);
            var box = mesh.getBoundingInfo().boundingBox;
            minimum.minimizeInPlace(box.minimumWorld);
            maximum.maximizeInPlace(box.maximumWorld);
        });
        var camera = scene.activeCamera;
        camera.detachControl();
        camera.inputs.clear();
        camera.setTarget(minimum.add(maximum).scale(0.5));
        camera.unfreezeProjectionMatrix();
        var variant = globalThis.__nativeArPortalBenchmarkLabel || (/\._x/.test(BABYLON.Plane.prototype.dotCoordinate.toString()) ? "optimized" : "control");
        variant += globalThis.__nativeArPortalBenchmarkSuffix || "";
        var instrumentation = new BABYLON.SceneInstrumentation(scene);
        instrumentation.captureFrameTime = true;
        instrumentation.captureActiveMeshesEvaluationTime = true;
        instrumentation.captureRenderTime = true;
        instrumentation.captureRenderTargetsRenderTime = true;
        var poses = [[-Math.PI / 2, 1.3, 7], [-0.77, 1.1, 8.5], [-2.37, 1.5, 5.5], [-1.57, 0.9, 7]];
        var frame = 0;
        var pending = false;
        var capture = 0;
        var started = 0;
        var previous = 0;
        var times = [];
        var cpu = 0;
        var activeCpu = 0;
        var renderCpu = 0;
        var targetsCpu = 0;
        var activeMin = Infinity;
        var activeMax = 0;
        var done = false;
        var duration = globalThis.__nativeArPortalBenchmarkDurationMs || 45000;
        var motion = [];
        var nextMotionSample = 0;
        var windowStarted = 0;
        var windowFrames = 0;
        var windowCpu = 0;
        var threadCpuStart;
        function threadCpuTime() {
            return typeof TestUtils.getCurrentThreadCpuTime === "function" ? TestUtils.getCurrentThreadCpuTime() : undefined;
        }
        var windowMaxMs = 0;
        var initialStats;
        var gpu = typeof navigator !== "undefined" && navigator.gpu;
        function stats() { return gpu && typeof gpu._backendStats === "function" ? gpu._backendStats() : null; }
        status("moving-benchmark:start=" + JSON.stringify({ variant: variant, width: scene.getEngine().getRenderWidth(), height: scene.getEngine().getRenderHeight(), far: camera.maxZ, ao: true }));
        scene.onBeforeRenderObservable.add(function () {
            if (pending) return;
            var phase = started ? (performance.now() - started) / 12000 * Math.PI * 2 : 0;
            var pose = poses[Math.min(capture, poses.length - 1)];
            camera.alpha = started ? -Math.PI / 2 + Math.sin(phase) * 0.8 : pose[0];
            camera.beta = started ? 1.3 + Math.sin(phase * 2) * 0.25 : pose[1];
            camera.radius = started ? 7 + Math.sin(phase) * 1.5 : pose[2];
            frame++;
        });
        scene.onAfterRenderObservable.add(function () {
            if (pending || done) return;
            var now = performance.now();
            if (capture < poses.length) {
                if (frame < 180 + capture * 60 || !scene.isReady()) return;
                var expectedSize = globalThis.__nativeArPortalBenchmarkExpectedSize;
                if (expectedSize) {
                    var texture = scene.getEngine().getRenderingCanvas().getContext("webgpu").getCurrentTexture();
                    if (texture.width !== expectedSize[0] || texture.height !== expectedSize[1]) {
                        throw new Error("Moving-camera canvas texture does not match the requested render size.");
                    }
                    status("moving-benchmark:backing=" + JSON.stringify({ width: texture.width, height: texture.height }));
                }
                pending = true;
                var path = TestUtils.getOutputDirectory() + "/moving-" + variant + "-" + capture + ".png";
                TestUtils.getFrameBufferData(function (data, width, height) {
                    if (!(width > 0 && height > 0) || data.length !== width * height * 4) {
                        throw new Error("Moving-camera framebuffer capture dimensions do not match the RGBA byte length.");
                    }
                    // Native screenshots use the presentation size, which can differ from the canvas backing.
                    TestUtils.writePNG(data, width, height, path);
                    status("moving-benchmark:capture=" + JSON.stringify({ variant: variant, index: capture, path: path, width: width, height: height, active: scene.getActiveMeshes().length }));
                    capture++;
                    pending = false;
                    if (capture === poses.length && globalThis.__nativeArPortalBenchmarkCaptureOnly) {
                        done = true;
                        status("moving-benchmark:captures-complete");
                        TestUtils.exit(0);
                    }
                });
                return;
            }
            // Keep readback and pipeline warm-up outside the timed interval.
            if (!started) {
                if (frame < 480) return;
                started = previous = windowStarted = now;
                initialStats = stats();
                threadCpuStart = threadCpuTime();
                status("moving-benchmark:timing=" + JSON.stringify({ epochMs: Date.now(), durationMs: duration }));
                return;
            }
            times.push(now - previous);
            windowMaxMs = Math.max(windowMaxMs, now - previous);
            windowFrames++;
            windowCpu += instrumentation.frameTimeCounter.current;
            previous = now;
            cpu += instrumentation.frameTimeCounter.current;
            activeCpu += instrumentation.activeMeshesEvaluationTimeCounter.current;
            renderCpu += instrumentation.renderTimeCounter.current;
            targetsCpu += instrumentation.renderTargetsRenderTimeCounter.current;
            activeMin = Math.min(activeMin, scene.getActiveMeshes().length);
            activeMax = Math.max(activeMax, scene.getActiveMeshes().length);
            if (now - started >= nextMotionSample) {
                var view = camera.getViewMatrix().m;
                var sample = { elapsedMs: now - started, frames: times.length, threadCpuMs: threadCpuStart === undefined ? undefined : threadCpuTime() - threadCpuStart,
                    alpha: camera.alpha, beta: camera.beta, radius: camera.radius,
                    view: [view[0], view[2], view[5], view[12], view[13], view[14]], active: scene.getActiveMeshes().length };
                motion.push(sample);
                // Full motion samples are streamed separately. Bound the final
                // receipt so long runs do not overflow device console chunks.
                if (motion.length > 12) motion.shift();
                status("moving-benchmark:motion=" + JSON.stringify(sample));
                nextMotionSample += 5000;
            }
            if (now - windowStarted >= 30000 || now - started >= duration) {
                status("moving-benchmark:window=" + JSON.stringify({ elapsedMs: now - started,
                    durationMs: now - windowStarted, frames: windowFrames,
                    fps: windowFrames * 1000 / (now - windowStarted), cpuMs: windowCpu / windowFrames,
                    maxMs: windowMaxMs }));
                windowStarted = now;
                windowFrames = windowCpu = windowMaxMs = 0;
            }
            if (now - started < duration) return;
            done = true;
            times.sort(function (a, b) { return a - b; });
            status("moving-benchmark:result=" + JSON.stringify({ variant: variant, frames: times.length, durationMs: now - started,
                threadCpuMs: threadCpuStart === undefined ? undefined : threadCpuTime() - threadCpuStart,
                width: scene.getEngine().getRenderWidth(), height: scene.getEngine().getRenderHeight(),
                fps: times.length * 1000 / (now - started), cpuMs: cpu / times.length,
                activeCpuMs: activeCpu / times.length, renderCpuMs: renderCpu / times.length, targetsCpuMs: targetsCpu / times.length,
                medianMs: times[Math.floor(times.length * 0.5)], p95Ms: times[Math.floor(times.length * 0.95)],
                activeMin: activeMin, activeMax: activeMax, motion: motion, initialStats: initialStats, finalStats: stats() }));
            status("moving-benchmark:timing-complete:camera-keeps-moving");
            if (globalThis.__nativeArPortalBenchmarkExitOnComplete) TestUtils.exit(0);
        });
        scene.onDisposeObservable.add(function () { instrumentation.dispose(); });
    };
})();
