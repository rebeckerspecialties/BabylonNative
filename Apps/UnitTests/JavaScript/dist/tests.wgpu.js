(function () {
    function fail(message) {
        throw new Error(message);
    }

    function expect(condition, message) {
        if (!condition) {
            fail(message);
        }
    }

    function expectEqual(actual, expected, message) {
        if (actual !== expected) {
            fail(message + ": expected " + String(expected) + ", got " + String(actual));
        }
    }

    var tests = [
        ["Canvas.parseColor accepts supported CSS color forms", function () {
            expect(typeof _native === "object", "_native was not installed");
            expect(_native.Canvas && typeof _native.Canvas.parseColor === "function", "_native.Canvas.parseColor missing");
            expectEqual(_native.Canvas.parseColor(""), 0, "empty color");
            expectEqual(_native.Canvas.parseColor("transparent"), 0, "transparent color");
            expectEqual(_native.Canvas.parseColor("#123"), 0xff332211, "short hex color");
            expectEqual(_native.Canvas.parseColor("#1234"), 0x44332211, "short hex alpha color");
            expectEqual(_native.Canvas.parseColor("#123456"), 0xff563412, "long hex color");
            expectEqual(_native.Canvas.parseColor("#12345678"), 0x78563412, "long hex alpha color");
            expectEqual(_native.Canvas.parseColor("snow"), 0xfffafaff, "named color");
            expectEqual(_native.Canvas.parseColor("rgb(16,32,48)"), 0xff302010, "rgb color");
            expectEqual(_native.Canvas.parseColor("rgba(16,32,48,64)"), 0x40302010, "rgba color");
        }],
        ["Canvas.parseColor rejects malformed colors", function () {
            [
                "unknownColor",
                "#",
                "#12345",
                "rgb(11)",
                "rgb(11,22,33",
                "rgb(11,22,33,",
                "rgba(11,   22, 33,  )",
                "rgba(11,   22, 33, 44,   55,   66 )",
                "rgb",
                "rgba"
            ].forEach(function (value) {
                var didThrow = false;
                try {
                    _native.Canvas.parseColor(value);
                } catch (_) {
                    didThrow = true;
                }
                expect(didThrow, "Expected parseColor to reject " + value);
            });
        }],
        ["navigator.gpu exposes the WebGPU adapter/device promise surface", async function () {
            expect(typeof navigator === "object", "navigator missing");
            expect(navigator.gpu === navigator.gpu, "navigator.gpu should be SameObject-like");
            expect(typeof navigator.gpu.requestAdapter === "function", "requestAdapter missing");
            expectEqual(navigator.gpu.getPreferredCanvasFormat(), "bgra8unorm", "preferred canvas format");

            var adapter = await navigator.gpu.requestAdapter();
            expect(adapter, "requestAdapter returned null");
            expect(adapter.info && adapter.info.architecture === "wgpu", "adapter info architecture should be wgpu");
            expect(typeof adapter.requestDevice === "function", "requestDevice missing");

            var device = await adapter.requestDevice();
            expect(device, "requestDevice returned null");
            expect(device.queue && typeof device.queue.submit === "function", "device.queue.submit missing");
            expect(typeof device.createCommandEncoder === "function", "createCommandEncoder missing");
            expect(typeof device.createRenderPipeline === "function", "createRenderPipeline missing");
        }],
        ["GPUCanvasContext shim can produce a current texture view", async function () {
            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var context = navigator.gpu._createCanvasContext();
            expect(context && typeof context.configure === "function", "canvas context configure missing");
            context.configure({
                device: device,
                format: navigator.gpu.getPreferredCanvasFormat(),
                width: 64,
                height: 64
            });
            var texture = context.getCurrentTexture();
            expect(texture && typeof texture.createView === "function", "current texture view factory missing");
            expect(texture.createView(), "createView returned null");
            context.unconfigure();
        }]
    ];

    (async function () {
        for (var i = 0; i < tests.length; i++) {
            var name = tests[i][0];
            var body = tests[i][1];
            try {
                await body();
                console.log("[wgpu-unit] PASS " + name);
            } catch (error) {
                console.error("[wgpu-unit] FAIL " + name + ": " + (error && error.stack ? error.stack : String(error)));
                setExitCode(1);
                return;
            }
        }
        setExitCode(0);
    })().catch(function (error) {
        console.error("[wgpu-unit] Unhandled failure: " + (error && error.stack ? error.stack : String(error)));
        setExitCode(1);
    });
})();
