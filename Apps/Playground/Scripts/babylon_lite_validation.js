(async function () {
    "use strict";

    var READY_TIMEOUT_MS = 60000;
    var sceneName = String(globalThis.__babylonLiteValidationScene || "unknown-scene");
    var state = globalThis.__babylonLiteValidationState || { consoleErrors: [], startedAt: performance.now(), device: null };
    var canvas = document.getElementById("renderCanvas");

    function delay(milliseconds) {
        return new Promise(function (resolve) {
            setTimeout(resolve, milliseconds);
        });
    }

    async function waitForReady() {
        var deadline = performance.now() + READY_TIMEOUT_MS;
        while (performance.now() < deadline) {
            if (state.consoleErrors.length > 0) {
                throw new Error("Scene reported a console error before becoming ready.");
            }
            if (canvas && canvas.dataset && canvas.dataset.ready === "true") {
                return;
            }
            await delay(16);
        }
        throw new Error("Timed out waiting for canvas.dataset.ready after " + READY_TIMEOUT_MS + " ms.");
    }

    function waitForFrame() {
        return new Promise(function (resolve) {
            requestAnimationFrame(function () {
                resolve();
            });
        });
    }

    function getCaptureData() {
        if (canvas && typeof canvas.__babylonLiteReadValidationPixels === "function") {
            var canvasPixels = canvas.__babylonLiteReadValidationPixels();
            if (canvasPixels) {
                return Promise.resolve(canvasPixels);
            }
        }

        return new Promise(function (resolve) {
            TestUtils.getFrameBufferData(function (pixels, width, height) {
                resolve({ pixels: pixels, width: width, height: height, source: "framebuffer" });
            });
        });
    }

    function analyzeFrame(pixels) {
        var pixelCount = pixels.length / 4;
        var backgroundR = pixels.length >= 4 ? pixels[0] : 0;
        var backgroundG = pixels.length >= 4 ? pixels[1] : 0;
        var backgroundB = pixels.length >= 4 ? pixels[2] : 0;
        var nonBlack = 0;
        var nonBackground = 0;
        var minimum = 255;
        var maximum = 0;

        for (var index = 0; index < pixels.length; index += 4) {
            var r = pixels[index];
            var g = pixels[index + 1];
            var b = pixels[index + 2];
            minimum = Math.min(minimum, r, g, b);
            maximum = Math.max(maximum, r, g, b);
            if (r > 3 || g > 3 || b > 3) {
                nonBlack++;
            }
            if (Math.abs(r - backgroundR) > 2 || Math.abs(g - backgroundG) > 2 || Math.abs(b - backgroundB) > 2) {
                nonBackground++;
            }
        }

        return {
            pixelCount: pixelCount,
            nonBlackPixels: nonBlack,
            nonBackgroundPixels: nonBackground,
            minimumChannel: minimum,
            maximumChannel: maximum,
            uniform: nonBackground < Math.max(16, pixelCount * 0.00001)
        };
    }

    function finish(exitCode, result) {
        if (state.frameTimerIntervalId !== null) {
            clearInterval(state.frameTimerIntervalId);
            state.frameTimerIntervalId = null;
        }
        if (typeof globalThis.__nativeValidationSetFrameTimerEnabled === "function") {
            globalThis.__nativeValidationSetFrameTimerEnabled(false);
        }
        var message = "BABYLON_LITE_NATIVE_RESULT " + JSON.stringify(result);
        if (typeof globalThis.__nativeValidationReport === "function") {
            globalThis.__nativeValidationReport(message);
        } else {
            console.log(message);
        }
        TestUtils.exit(exitCode);
    }

    try {
        await waitForReady();
        await waitForFrame();
        await waitForFrame();

        if (state.device && state.device.queue && typeof state.device.queue.onSubmittedWorkDone === "function") {
            await state.device.queue.onSubmittedWorkDone();
        }

        var frame = await getCaptureData();
        var analysis = analyzeFrame(frame.pixels);
        var outputPath = TestUtils.getOutputDirectory() + "/BabylonLiteResults/" + sceneName + ".png";
        TestUtils.writePNG(frame.pixels, frame.width, frame.height, outputPath);

        var status = state.consoleErrors.length > 0
            ? "console-error"
            : (analysis.uniform ? "uniform-frame" : "passed");
        var result = {
            scene: sceneName,
            status: status,
            width: frame.width,
            height: frame.height,
            captureSource: frame.source,
            drawCalls: Number(canvas.dataset.drawCalls || 0),
            initMs: Number(canvas.dataset.initMs || 0),
            elapsedMs: performance.now() - state.startedAt,
            consoleErrors: state.consoleErrors,
            outputPath: outputPath,
            frame: analysis
        };
        finish(status === "passed" ? 0 : (status === "console-error" ? 10 : 11), result);
    } catch (error) {
        var status = state.consoleErrors.length > 0 ? "console-error" : "failed";
        finish(status === "console-error" ? 10 : 12, {
            scene: sceneName,
            status: status,
            elapsedMs: performance.now() - state.startedAt,
            consoleErrors: state.consoleErrors,
            error: error && error.stack ? String(error.stack) : String(error)
        });
    }
})();
