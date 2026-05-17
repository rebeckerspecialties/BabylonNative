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

    function expectNear(actual, expected, tolerance, message) {
        if (Math.abs(Number(actual) - expected) > tolerance) {
            fail(message + ": expected " + String(expected) + " +/- " + String(tolerance) + ", got " + String(actual));
        }
    }

    function expectPixel(actual, expected, message) {
        expect(actual && actual.length === 4, message + ": pixel readback did not return four channels");
        for (var i = 0; i < 4; i++) {
            if (Math.abs(Number(actual[i]) - expected[i]) > 2) {
                console.error(message + ": expected [" + expected.join(",") + "], got [" + Array.prototype.join.call(actual, ",") + "]");
                fail(message + ": expected [" + expected.join(",") + "], got [" + Array.prototype.join.call(actual, ",") + "]");
            }
        }
    }

    function textureId(texture) {
        return Number(texture && texture.__babylonNativeWebGPUHandleId || 0);
    }

    function makeCanvasContext(device, width, height) {
        var context = navigator.gpu._createCanvasContext();
        context.canvas = { width: width, height: height };
        context.configure({
            device: device,
            format: navigator.gpu.getPreferredCanvasFormat(),
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
        });
        return context;
    }

    function makeBabylonWebGPUCanvas(width, height) {
        var context = navigator.gpu._createCanvasContext();
        var canvas = {
            width: width,
            height: height,
            clientWidth: width,
            clientHeight: height,
            style: { width: width + "px", height: height + "px" },
            addEventListener: function () { },
            removeEventListener: function () { },
            setAttribute: function () { },
            focus: function () { },
            getBoundingClientRect: function () {
                return {
                    x: 0,
                    y: 0,
                    top: 0,
                    left: 0,
                    right: width,
                    bottom: height,
                    width: width,
                    height: height
                };
            },
            getContext: function (contextName) {
                if (contextName === "webgpu") {
                    context.canvas = canvas;
                    return context;
                }

                return null;
            }
        };
        return canvas;
    }

    async function readCanvasPixel(source, width, height, x, y) {
        return readCanvasPixelWithDestination(source, width, height, x, y, {});
    }

    async function readCanvasPixelWithDestination(source, width, height, x, y, destinationOptions) {
        expect(typeof navigator.gpu._testReadTexturePixel === "function", "texture readback test hook missing");

        var adapter = await navigator.gpu.requestAdapter();
        var device = await adapter.requestDevice();
        var texture = device.createTexture({
            size: [width, height, 1],
            format: "rgba8unorm",
            usage: GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
        });
        var destination = destinationOptions || {};
        destination.texture = texture;
        device.queue.copyExternalImageToTexture({ source: source }, destination, { width: width, height: height });
        await device.queue.onSubmittedWorkDone();
        return navigator.gpu._testReadTexturePixel(texture, x, y);
    }

    function getUnsupportedNativeWebGPUEffectMessage(effect, context) {
        if (!effect || !effect._engine || !effect._engine.isWebGPU || effect._shaderLanguage !== 0) {
            return "";
        }

        var parts = [];
        if (context) {
            parts.push(context);
        }
        if (effect.name) {
            parts.push("effect=" + String(effect.name));
        }
        if (effect._key) {
            parts.push("key=" + String(effect._key));
        }

        return "BabylonJS requested a GLSL WebGPU effect without a compiled pipeline" +
            (parts.length > 0 ? " (" + parts.join(",") + ")" : "") +
            ". BabylonNative WebGPU does not ship the glslang/twgsl fallback. This BabylonJS path needs a WGSL implementation, a shaderLanguage=WGSL createEffect path, or a deliberate NativeWebGPU exclusion.";
    }

    function installUnsupportedGlslCreateEffectGuard(engine) {
        expect(engine && engine.isWebGPU, "GLSL createEffect guard requires a WebGPU engine");
        var originalCreateEffect = engine.createEffect;
        expect(typeof originalCreateEffect === "function", "engine.createEffect missing");

        engine.createEffect = function (baseName) {
            var effect = originalCreateEffect.apply(this, arguments);
            var shaderName = typeof baseName === "string" ? baseName : JSON.stringify(baseName);
            var unsupportedMessage = getUnsupportedNativeWebGPUEffectMessage(effect, "createEffect=" + shaderName);
            if (unsupportedMessage) {
                throw new Error("NATIVE_WEBGPU_UNSUPPORTED_EFFECT: " + unsupportedMessage);
            }
            return effect;
        };
    }

    function clearTexture(device, texture, color) {
        var encoder = device.createCommandEncoder();
        var pass = encoder.beginRenderPass({
            colorAttachments: [{
                view: texture.createView(),
                loadOp: "clear",
                storeOp: "store",
                clearValue: color
            }]
        });
        pass.end();
        device.queue.submit([encoder.finish()]);
    }

    function createFloatBuffer(device, values, usage) {
        var data = new Float32Array(values);
        var buffer = device.createBuffer({
            size: data.byteLength,
            usage: usage,
            mappedAtCreation: true
        });
        new Float32Array(buffer.getMappedRange()).set(data);
        buffer.unmap();
        return buffer;
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
            expectEqual(_native.Canvas.parseColor("hsl(120, 100%, 50%)"), 0xff00ff00, "hsl color");
            expectEqual(_native.Canvas.parseColor("hsla(0, 100%, 50%, 0.5)"), 0x800000ff, "hsla color");
        }],
        ["Canvas 2D globalAlpha follows browser preservation semantics", function () {
            var canvas = new _native.Canvas();
            canvas.width = 4;
            canvas.height = 4;
            var context = canvas.getContext("2d");

            expectEqual(context.globalAlpha, 1, "default globalAlpha");
            context.globalAlpha *= 0.5;
            expectEqual(context.globalAlpha, 0.5, "globalAlpha should be readable for multiplicative updates");
            context.save();
            context.globalAlpha = 0.25;
            expectEqual(context.globalAlpha, 0.25, "globalAlpha setter should update valid alpha");
            context.restore();
            expectEqual(context.globalAlpha, 0.5, "save/restore should preserve globalAlpha");

            context.globalAlpha = -1;
            expectEqual(context.globalAlpha, 0.5, "negative globalAlpha should be ignored");
            context.globalAlpha = 2;
            expectEqual(context.globalAlpha, 0.5, "out-of-range globalAlpha should be ignored");
            context.globalAlpha = Number.NaN;
            expectEqual(context.globalAlpha, 0.5, "NaN globalAlpha should be ignored");
            context.globalAlpha = Number.POSITIVE_INFINITY;
            expectEqual(context.globalAlpha, 0.5, "infinite globalAlpha should be ignored");
        }],
        ["Canvas 2D fillStyle and strokeStyle ignore invalid strings", async function () {
            var originalWarn = console.warn;
            var warnings = [];
            console.warn = function (message) {
                warnings.push(String(message));
                originalWarn.apply(console, arguments);
            };

            var fillCanvas = new _native.Canvas();
            fillCanvas.width = 4;
            fillCanvas.height = 4;
            var fillContext = fillCanvas.getContext("2d");

            var strokeCanvas = new _native.Canvas();
            strokeCanvas.width = 8;
            strokeCanvas.height = 5;
            var strokeContext = strokeCanvas.getContext("2d");

            try {
                fillContext.fillStyle = "rgb(0, 255, 0)";
                fillContext.fillStyle = "";
                fillContext.fillStyle = "unknownColor";
                fillContext.fillRect(0, 0, 4, 4);

                strokeContext.strokeStyle = "rgb(255, 0, 0)";
                strokeContext.strokeStyle = "";
                strokeContext.strokeStyle = "unknownColor";
                strokeContext.lineWidth = 2;
                strokeContext.beginPath();
                strokeContext.moveTo(0, 2);
                strokeContext.lineTo(8, 2);
                strokeContext.stroke();
            } finally {
                console.warn = originalWarn;
            }

            expectPixel(await readCanvasPixel(fillCanvas, 4, 4, 1, 1), [0, 255, 0, 255], "invalid fillStyle should preserve the previous valid color");
            expectPixel(await readCanvasPixel(strokeCanvas, 8, 5, 3, 2), [255, 0, 0, 255], "invalid strokeStyle should preserve the previous valid color");
            expect(warnings.some(function (message) { return message.indexOf("fillStyle color value \"\"") !== -1; }), "empty fillStyle warning should include the ignored value");
            expect(warnings.some(function (message) { return message.indexOf("fillStyle color parse failed:") !== -1 && message.indexOf("Unable to parse color") !== -1; }), "fillStyle parser warning should include Napi::Error::what()");
            expect(warnings.some(function (message) { return message.indexOf("strokeStyle color value \"\"") !== -1; }), "empty strokeStyle warning should include the ignored value");
            expect(warnings.some(function (message) { return message.indexOf("strokeStyle color parse failed:") !== -1 && message.indexOf("Unable to parse color") !== -1; }), "strokeStyle parser warning should include Napi::Error::what()");
        }],
        ["Canvas 2D measureText exposes browser-style bounding metrics", function () {
            var canvas = new _native.Canvas();
            canvas.width = 64;
            canvas.height = 64;
            var context = canvas.getContext("2d");
            context.font = "20px DefinitelyMissingFont";

            var metrics = context.measureText("Hi");
            expectNear(metrics.width, 22, 0.001, "unavailable font family should use stable fallback width");
            expectNear(metrics.height, 20, 0.001, "unavailable font family should use stable fallback height");
            expectNear(metrics.actualBoundingBoxLeft, 0, 0.001, "fallback actualBoundingBoxLeft");
            expectNear(metrics.actualBoundingBoxRight, 22, 0.001, "fallback actualBoundingBoxRight");
            expectNear(metrics.actualBoundingBoxAscent, 15, 0.001, "fallback actualBoundingBoxAscent");
            expectNear(metrics.actualBoundingBoxDescent, 5, 0.001, "fallback actualBoundingBoxDescent");
            expectNear(metrics.fontBoundingBoxAscent, 15, 0.001, "fallback fontBoundingBoxAscent");
            expectNear(metrics.fontBoundingBoxDescent, 5, 0.001, "fallback fontBoundingBoxDescent");
        }],
        ["Canvas 2D Arial metrics stay close to browser GUI wrapping", function () {
            var canvas = new _native.Canvas();
            canvas.width = 64;
            canvas.height = 64;
            var context = canvas.getContext("2d");
            context.font = "18px Arial";

            var clickMetrics = context.measureText("Click");
            var meMetrics = context.measureText("Me");
            expectNear(clickMetrics.width, 38.9970703125, 0.001, "Arial Click width");
            expectNear(meMetrics.width, 25.0048828125, 0.001, "Arial Me width");
            expectNear(clickMetrics.fontBoundingBoxAscent, 16, 0.5, "Arial Click fontBoundingBoxAscent");
            expectNear(clickMetrics.fontBoundingBoxDescent, 4, 0.5, "Arial Click fontBoundingBoxDescent");
        }],
        ["BabylonJS native font offset approximates browser DOM line boxes", function () {
            var engineLike = {
                createCanvas: function (width, height) {
                    var canvas = new _native.Canvas();
                    canvas.width = width;
                    canvas.height = height;
                    return canvas;
                }
            };
            expect(BABYLON && BABYLON.Engine && BABYLON.Engine.prototype.getFontOffset, "BabylonJS Engine.getFontOffset missing");
            var offset = BABYLON.Engine.prototype.getFontOffset.call(engineLike, "18px Arial");
            expectEqual(offset.ascent, 16, "18px Arial font offset ascent");
            expectEqual(offset.height, 21, "18px Arial font offset height");
            expectEqual(offset.descent, 5, "18px Arial font offset descent");
        }],
        ["Canvas 2D narrow rounded paths fill", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 32;
            canvas.height = 32;
            var context = canvas.getContext("2d");
            var x = 10.5;
            var y = 2.5;
            var width = 11;
            var height = 21;
            var radius = 5.5;

            context.fillStyle = "rgb(255, 0, 0)";
            context.beginPath();
            context.moveTo(x + radius, y);
            context.lineTo(x + width - radius, y);
            context.arc(x + width - radius, y + radius, radius, (3 * Math.PI) / 2, Math.PI * 2);
            context.lineTo(x + width, y + height - radius);
            context.arc(x + width - radius, y + height - radius, radius, 0, Math.PI / 2);
            context.lineTo(x + radius, y + height);
            context.arc(x + radius, y + height - radius, radius, Math.PI / 2, Math.PI);
            context.lineTo(x, y + radius);
            context.arc(x + radius, y + radius, radius, Math.PI, (3 * Math.PI) / 2);
            context.closePath();
            context.fill();

            expectPixel(await readCanvasPixel(canvas, 32, 32, 16, 12), [255, 0, 0, 255], "narrow rounded path center");
        }],
        ["Canvas 2D negative-width roundRect strokes match clipped browser geometry", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 64;
            canvas.height = 48;
            var context = canvas.getContext("2d");

            var measureLeft = 35;
            var measureTop = 8;
            var measureWidth = -20;
            var measureHeight = 30;
            var x = measureLeft + 0.5;
            var y = measureTop + 0.5;
            var width = measureWidth - 1;
            var height = measureHeight - 1;
            var radius = Math.abs(Math.min(height / 2, Math.min(width / 2, 20)));

            context.beginPath();
            context.rect(measureLeft, measureTop, measureWidth, measureHeight);
            context.clip();
            context.strokeStyle = "rgb(255, 255, 255)";
            context.lineWidth = 1;
            context.beginPath();
            context.roundRect(x, y, width, height, radius);
            context.stroke();

            var readSourcePixel = function (x, y) {
                return readCanvasPixel(canvas, 64, 48, x, 47 - y);
            };
            var topArcA = await readSourcePixel(24, 8);
            var topArcB = await readSourcePixel(24, 9);
            var bottomArcA = await readSourcePixel(24, 37);
            var bottomArcB = await readSourcePixel(24, 38);
            var hasVisibleWhite = function (pixel) {
                return pixel[0] > 200 && pixel[1] > 200 && pixel[2] > 200 && pixel[3] > 16;
            };
            expect(
                hasVisibleWhite(topArcA) || hasVisibleWhite(topArcB),
                "negative-width roundRect should keep the clipped top arc segment: got " + Array.prototype.join.call(topArcA, ",") + " / " + Array.prototype.join.call(topArcB, ",")
            );
            expect(
                hasVisibleWhite(bottomArcA) || hasVisibleWhite(bottomArcB),
                "negative-width roundRect should keep the clipped bottom arc segment: got " + Array.prototype.join.call(bottomArcA, ",") + " / " + Array.prototype.join.call(bottomArcB, ",")
            );
            expectPixel(await readSourcePixel(16, 37), [0, 0, 0, 0], "negative-width roundRect should not draw a solid crossing bottom edge");
            expectPixel(await readSourcePixel(34, 37), [0, 0, 0, 0], "negative-width roundRect should not draw past the clipped bottom arc segment");
        }],
        ["Canvas 2D rounded clips reject transformed top-corner overflow", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 80;
            canvas.height = 60;
            var context = canvas.getContext("2d");

            context.translate(40, 30);
            context.rotate(-Math.PI / 10);
            context.beginPath();
            context.roundRect(-22, -18, 44, 36, 18);
            context.clip();
            context.fillStyle = "rgb(255, 255, 255)";
            context.fillRect(-80, -80, 160, 160);

            var transformPoint = function (x, y) {
                var angle = -Math.PI / 10;
                return {
                    x: Math.round(40 + x * Math.cos(angle) - y * Math.sin(angle)),
                    y: Math.round(30 + x * Math.sin(angle) + y * Math.cos(angle))
                };
            };
            var readSourcePixel = function (point) {
                return readCanvasPixel(canvas, 80, 60, point.x, 59 - point.y);
            };

            var topCornerOutside = transformPoint(-21, -17);
            var topCenterInside = transformPoint(0, -15);
            expectPixel(await readSourcePixel(topCornerOutside), [0, 0, 0, 0], "rounded clip should reject transformed top-corner overflow");
            expectPixel(await readSourcePixel(topCenterInside), [255, 255, 255, 255], "rounded clip should keep transformed top-center content");
        }],
        ["Canvas 2D nested rounded clips preserve transformed top corners", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 80;
            canvas.height = 60;
            var context = canvas.getContext("2d");

            context.beginPath();
            context.rect(0, 0, 80, 60);
            context.clip();
            context.translate(40, 30);
            context.rotate(-Math.PI / 10);
            context.beginPath();
            context.roundRect(-22, -18, 44, 36, 18);
            context.clip();
            context.fillStyle = "rgb(255, 255, 255)";
            context.fillRect(-80, -80, 160, 160);

            var transformPoint = function (x, y) {
                var angle = -Math.PI / 10;
                return {
                    x: Math.round(40 + x * Math.cos(angle) - y * Math.sin(angle)),
                    y: Math.round(30 + x * Math.sin(angle) + y * Math.cos(angle))
                };
            };
            var readSourcePixel = function (point) {
                return readCanvasPixel(canvas, 80, 60, point.x, 59 - point.y);
            };

            var topCornerOutside = transformPoint(-21, -17);
            var topCenterInside = transformPoint(0, -15);
            expectPixel(await readSourcePixel(topCornerOutside), [0, 0, 0, 0], "nested rounded clip should reject transformed top-corner overflow");
            expectPixel(await readSourcePixel(topCenterInside), [255, 255, 255, 255], "nested rounded clip should keep transformed top-center content");
        }],
        ["Canvas 2D nested rounded clips reject rotated capsule top-side overflow", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 96;
            canvas.height = 96;
            var context = canvas.getContext("2d");

            context.translate(48, 48);
            context.rotate(0.2);
            context.beginPath();
            context.rect(-48, -48, 96, 96);
            context.clip();
            context.beginPath();
            context.roundRect(-14, -19, 28, 38, 14);
            context.clip();
            context.fillStyle = "rgb(255, 255, 255)";
            context.fillRect(-96, -96, 192, 192);

            var transformPoint = function (x, y) {
                var angle = 0.2;
                return {
                    x: Math.round(48 + x * Math.cos(angle) - y * Math.sin(angle)),
                    y: Math.round(48 + x * Math.sin(angle) + y * Math.cos(angle))
                };
            };
            var readSourcePixel = function (point) {
                return readCanvasPixel(canvas, 96, 96, point.x, 95 - point.y);
            };

            expectPixel(await readSourcePixel(transformPoint(-13, -16)), [0, 0, 0, 0], "rotated capsule clip should reject top-left text overflow");
            expectPixel(await readSourcePixel(transformPoint(0, -16)), [255, 255, 255, 255], "rotated capsule clip should keep top-center content");
        }],
        ["Canvas 2D matching rectangular content clips preserve rounded parent corners", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 96;
            canvas.height = 96;
            var context = canvas.getContext("2d");

            context.translate(48, 48);
            context.rotate(0.2);
            context.beginPath();
            context.roundRect(-14, -19, 28, 38, 14);
            context.clip();
            context.beginPath();
            context.rect(-14, -19, 28, 38);
            context.clip();
            context.fillStyle = "rgb(255, 255, 255)";
            context.fillRect(-96, -96, 192, 192);

            var transformPoint = function (x, y) {
                var angle = 0.2;
                return {
                    x: Math.round(48 + x * Math.cos(angle) - y * Math.sin(angle)),
                    y: Math.round(48 + x * Math.sin(angle) + y * Math.cos(angle))
                };
            };
            var readSourcePixel = function (point) {
                return readCanvasPixel(canvas, 96, 96, point.x, 95 - point.y);
            };

            expectPixel(await readSourcePixel(transformPoint(-13, -16)), [0, 0, 0, 0], "matching rectangular child clip should not erase rounded parent top-left corner");
            expectPixel(await readSourcePixel(transformPoint(0, -16)), [255, 255, 255, 255], "matching rectangular child clip should preserve rounded parent top-center content");
        }],
        ["Canvas 2D rotated rounded GUI clip keeps top corners inside the capsule", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 96;
            canvas.height = 96;
            var context = canvas.getContext("2d");

            function roundedRectPath(x, y, width, height, radius) {
                context.beginPath();
                context.moveTo(x + radius, y);
                context.lineTo(x + width - radius, y);
                context.arc(x + width - radius, y + radius, radius, (3 * Math.PI) / 2, Math.PI * 2);
                context.lineTo(x + width, y + height - radius);
                context.arc(x + width - radius, y + height - radius, radius, 0, Math.PI / 2);
                context.lineTo(x + radius, y + height);
                context.arc(x + radius, y + height - radius, radius, Math.PI / 2, Math.PI);
                context.lineTo(x, y + radius);
                context.arc(x + radius, y + radius, radius, Math.PI, (3 * Math.PI) / 2);
                context.closePath();
            }

            context.fillStyle = "rgb(51, 51, 76)";
            context.fillRect(0, 0, 96, 96);
            context.translate(48, 48);
            context.rotate(0.2);
            context.translate(-48, -48);

            var left = 33;
            var top = 29;
            var width = 30;
            var height = 40;

            context.beginPath();
            context.rect(left, top, width, height);
            context.clip();

            context.fillStyle = "green";
            roundedRectPath(left + 0.5, top + 0.5, width - 1, height - 1, 14.5);
            context.fill();
            context.strokeStyle = "white";
            context.lineWidth = 1;
            roundedRectPath(left + 0.5, top + 0.5, width - 1, height - 1, 14.5);
            context.stroke();

            context.beginPath();
            context.roundRect(left + 1, top + 1, width - 2, height - 2, 14);
            context.clip();
            context.font = "18px Arial";
            context.fillStyle = "white";

            var lines = ["Click", "Me"];
            var fontOffsetHeight = 21;
            var rootY = top + 1 + 16 + ((height - 2) - fontOffsetHeight * lines.length) / 2;
            for (var i = 0; i < lines.length; i++) {
                var textWidth = context.measureText(lines[i]).width;
                var x = left + 1 + ((width - 2) - textWidth) / 2;
                context.fillText(lines[i], x, rootY);
                rootY += fontOffsetHeight;
            }

            var readSourcePixel = function (x, y) {
                return readCanvasPixel(canvas, 96, 96, x, 95 - y);
            };
            expectPixel(await readSourcePixel(35, 35), [51, 51, 76, 255], "rotated capsule fill should not bleed past the top-left rounded edge");
            expectPixel(await readSourcePixel(63, 35), [51, 51, 76, 255], "rotated capsule fill should not bleed past the top-right rounded edge");
            expectPixel(await readSourcePixel(64, 35), [51, 51, 76, 255], "rotated capsule fill should keep rejecting far top-right overflow");
        }],
        ["Canvas 2D inset rectangular content clips preserve rounded parent corners", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 96;
            canvas.height = 96;
            var context = canvas.getContext("2d");

            context.translate(48, 48);
            context.rotate(0.2);
            context.beginPath();
            context.roundRect(-14, -19, 28, 38, 14);
            context.clip();
            context.beginPath();
            context.rect(-10, -19, 20, 38);
            context.clip();
            context.fillStyle = "rgb(255, 255, 255)";
            context.fillRect(-96, -96, 192, 192);

            var transformPoint = function (x, y) {
                var angle = 0.2;
                return {
                    x: Math.round(48 + x * Math.cos(angle) - y * Math.sin(angle)),
                    y: Math.round(48 + x * Math.sin(angle) + y * Math.cos(angle))
                };
            };
            var readSourcePixel = function (point) {
                return readCanvasPixel(canvas, 96, 96, point.x, 95 - point.y);
            };

            expectPixel(await readSourcePixel(transformPoint(-9, -18)), [0, 0, 0, 0], "inset rectangular child clip should keep rejecting rounded parent top-left overflow");
            expectPixel(await readSourcePixel(transformPoint(0, -16)), [255, 255, 255, 255], "inset rectangular child clip should preserve rounded parent top-center content");
        }],
        ["Canvas 2D clips use current path bounds and intersect nested clips", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 18;
            canvas.height = 8;
            var context = canvas.getContext("2d");

            context.save();
            context.beginPath();
            context.moveTo(4, 1);
            context.lineTo(10, 1);
            context.lineTo(10, 7);
            context.lineTo(4, 7);
            context.closePath();
            context.clip();
            context.beginPath();
            context.rect(8, 0, 8, 8);
            context.clip();

            context.fillStyle = "rgb(255, 0, 0)";
            context.fillRect(0, 0, 18, 8);
            context.restore();

            expectPixel(await readCanvasPixel(canvas, 18, 8, 8, 4), [255, 0, 0, 255], "nested clip intersection visible pixel");
            expectPixel(await readCanvasPixel(canvas, 18, 8, 1, 4), [0, 0, 0, 0], "path clip should not fall back to the full canvas");
            expectPixel(await readCanvasPixel(canvas, 18, 8, 14, 4), [0, 0, 0, 0], "nested clip should intersect the previous clip");
        }],
        ["Canvas 2D drawImage accepts another native canvas source", function () {
            var source = new _native.Canvas();
            source.width = 4;
            source.height = 4;
            var sourceContext = source.getContext("2d");
            sourceContext.fillStyle = "hsl(120, 100%, 50%)";
            sourceContext.fillRect(0, 0, 4, 4);
            sourceContext.flush();

            var destination = new _native.Canvas();
            destination.width = 4;
            destination.height = 4;
            var destinationContext = destination.getContext("2d");
            destinationContext.drawImage(source, 0, 0);
            destinationContext.flush();

            var payload = destination.getCanvasTexture();
            expect(payload && payload.nativeTexture, "drawImage canvas source did not produce a native texture payload");
        }],
        ["Canvas 2D putImageData survives drawImage canvas copies", async function () {
            var source = new _native.Canvas();
            source.width = 4;
            source.height = 4;
            var sourceContext = source.getContext("2d");
            var imageData = sourceContext.getImageData(0, 0, 4, 4);
            var pixels = imageData.data;
            for (var y = 0; y < 4; y++) {
                for (var x = 1; x <= 2; x++) {
                    var redPixel = (x + y * 4) * 4;
                    pixels[redPixel] = 255;
                    pixels[redPixel + 1] = 0;
                    pixels[redPixel + 2] = 0;
                    pixels[redPixel + 3] = 255;
                }
            }
            sourceContext.putImageData(imageData, 0, 0);
            expectPixel(await readCanvasPixel(source, 4, 4, 1, 1), [255, 0, 0, 255], "putImageData source canvas pixel");
            expectPixel(await readCanvasPixel(source, 4, 4, 0, 0), [0, 0, 0, 0], "putImageData source transparent pixel");

            var destination = new _native.Canvas();
            destination.width = 8;
            destination.height = 8;
            var destinationContext = destination.getContext("2d");
            destinationContext.drawImage(source, 2, 3);
            expectPixel(await readCanvasPixel(destination, 8, 8, 3, 4), [255, 0, 0, 255], "drawImage copied putImageData canvas pixel");
            expectPixel(await readCanvasPixel(destination, 8, 8, 1, 1), [0, 0, 0, 0], "drawImage offset left destination transparent");
        }],
        ["Canvas 2D drawImage preserves semi-transparent CanvasWgpu sources", async function () {
            var source = new _native.Canvas();
            source.width = 2;
            source.height = 2;
            var sourceContext = source.getContext("2d");
            var imageData = sourceContext.getImageData(0, 0, 2, 2);
            for (var i = 0; i < imageData.data.length; i += 4) {
                imageData.data[i + 0] = 255;
                imageData.data[i + 1] = 0;
                imageData.data[i + 2] = 0;
                imageData.data[i + 3] = 128;
            }
            sourceContext.putImageData(imageData, 0, 0);

            var destination = new _native.Canvas();
            destination.width = 2;
            destination.height = 2;
            var destinationContext = destination.getContext("2d");
            destinationContext.fillStyle = "rgb(0, 0, 0)";
            destinationContext.fillRect(0, 0, 2, 2);
            destinationContext.drawImage(source, 0, 0);

            expectPixel(
                await readCanvasPixel(destination, 2, 2, 0, 0),
                [128, 0, 0, 255],
                "semi-transparent canvas drawImage should not double-premultiply source RGB"
            );
        }],
        ["GPUQueue.copyExternalImageToTexture uploads CanvasWgpu 2D contents", async function () {
            expect(typeof navigator.gpu._testReadTexturePixel === "function", "texture readback test hook missing");

            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();

            var source = new _native.Canvas();
            source.width = 4;
            source.height = 4;
            var context = source.getContext("2d");
            context.fillStyle = "rgb(0, 255, 0)";
            context.fillRect(0, 0, 4, 4);

            var texture = device.createTexture({
                size: [4, 4, 1],
                format: "rgba8unorm",
                usage: GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
            });
            device.queue.copyExternalImageToTexture({ source: source }, { texture: texture }, { width: 4, height: 4 });
            await device.queue.onSubmittedWorkDone();

            expectPixel(navigator.gpu._testReadTexturePixel(texture, 1, 1), [0, 255, 0, 255], "CanvasWgpu 2D upload should include pre-flush draw commands");
        }],
        ["GPUQueue.copyExternalImageToTexture converts CanvasWgpu alpha mode", async function () {
            var source = new _native.Canvas();
            source.width = 2;
            source.height = 2;
            var context = source.getContext("2d");
            context.globalAlpha = 0.5;
            context.fillStyle = "rgb(255, 255, 255)";
            context.fillRect(0, 0, 2, 2);

            expectPixel(
                await readCanvasPixel(source, 2, 2, 0, 0),
                [255, 255, 255, 128],
                "CanvasWgpu default external upload should provide straight alpha"
            );
            expectPixel(
                await readCanvasPixelWithDestination(source, 2, 2, 0, 0, { premultipliedAlpha: true }),
                [128, 128, 128, 128],
                "CanvasWgpu premultiplied external upload should preserve premultiplied alpha"
            );
        }],
        ["CanvasGradient renders into CanvasWgpu WebGPU uploads", async function () {
            var linear = new _native.Canvas();
            linear.width = 8;
            linear.height = 4;
            var linearContext = linear.getContext("2d");
            var linearGradient = linearContext.createLinearGradient(0, 0, 8, 0);
            linearGradient.addColorStop(0, "hsl(0, 100%, 50%)");
            linearGradient.addColorStop(0.49, "hsl(0, 100%, 50%)");
            linearGradient.addColorStop(0.51, "hsl(240, 100%, 50%)");
            linearGradient.addColorStop(1, "hsl(240, 100%, 50%)");
            linearContext.fillStyle = linearGradient;
            linearContext.fillRect(0, 0, 8, 4);

            expectPixel(await readCanvasPixel(linear, 8, 4, 1, 2), [255, 0, 0, 255], "linear gradient left stop");
            expectPixel(await readCanvasPixel(linear, 8, 4, 6, 2), [0, 0, 255, 255], "linear gradient right stop");

            var radial = new _native.Canvas();
            radial.width = 8;
            radial.height = 8;
            var radialContext = radial.getContext("2d");
            var radialGradient = radialContext.createRadialGradient(4, 4, 0, 4, 4, 4);
            radialGradient.addColorStop(0, "hsla(120, 100%, 50%, 1)");
            radialGradient.addColorStop(0.49, "hsla(120, 100%, 50%, 1)");
            radialGradient.addColorStop(0.51, "rgba(0, 0, 0, 255)");
            radialGradient.addColorStop(1, "rgba(0, 0, 0, 255)");
            radialContext.fillStyle = radialGradient;
            radialContext.fillRect(0, 0, 8, 8);

            expectPixel(await readCanvasPixel(radial, 8, 8, 4, 4), [0, 255, 0, 255], "radial gradient center stop");
            expectPixel(await readCanvasPixel(radial, 8, 8, 0, 0), [0, 0, 0, 255], "radial gradient outer stop");
        }],
        ["Canvas 2D dashed strokes render and preserve drawing state", async function () {
            var canvas = new _native.Canvas();
            canvas.width = 12;
            canvas.height = 5;
            var context = canvas.getContext("2d");

            context.setLineDash([4, 2, 1]);
            expectEqual(context.getLineDash().join(","), "4,2,1,4,2,1", "odd dash pattern should repeat");
            context.lineDashOffset = 2;
            context.save();
            context.setLineDash([]);
            context.lineDashOffset = 0;
            context.restore();
            expectEqual(context.getLineDash().join(","), "4,2,1,4,2,1", "save/restore should preserve dash pattern");
            expectEqual(context.lineDashOffset, 2, "save/restore should preserve dash offset");

            context.strokeStyle = "rgb(255, 0, 0)";
            context.lineWidth = 2;
            context.setLineDash([4, 4]);
            context.lineDashOffset = 0;
            context.beginPath();
            context.moveTo(0, 2);
            context.lineTo(12, 2);
            context.stroke();

            expectPixel(await readCanvasPixel(canvas, 12, 5, 1, 2), [255, 0, 0, 255], "dashed stroke first visible interval");
            expectPixel(await readCanvasPixel(canvas, 12, 5, 6, 2), [0, 0, 0, 0], "dashed stroke hidden interval");
            expectPixel(await readCanvasPixel(canvas, 12, 5, 9, 2), [255, 0, 0, 255], "dashed stroke second visible interval");
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
        ["Canvas Image decodes PNG data URLs", async function () {
            var ImageCtor = _native.Image || (typeof Image !== "undefined" ? Image : null);
            expect(!!ImageCtor, "Image constructor missing; _native keys: " + Object.getOwnPropertyNames(_native).join(","));

            var image = new ImageCtor();
            var loaded = await new Promise(function (resolve) {
                image.onload = function () { resolve(true); };
                image.onerror = function () { resolve(false); };
                image.src = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAAEUlEQVR4nGP4z8Dwn6HhfwMAEPoD/o3Nc3sAAAAASUVORK5CYII=";
            });

            expect(loaded, "PNG data URL did not fire onload");
            expectEqual(image.width, 2, "decoded image width");
            expectEqual(image.height, 1, "decoded image height");
            expectEqual(image.naturalWidth, 2, "decoded naturalWidth");
            expectEqual(image.naturalHeight, 1, "decoded naturalHeight");
        }],
        ["Canvas 2D drawImage prefers native image data over canvas texture hooks", async function () {
            var ImageCtor = _native.Image || (typeof Image !== "undefined" ? Image : null);
            expect(!!ImageCtor, "Image constructor missing");

            var image = new ImageCtor();
            var loaded = await new Promise(function (resolve) {
                image.onload = function () { resolve(true); };
                image.onerror = function () { resolve(false); };
                image.src = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAAEUlEQVR4nGP4z8Dwn6HhfwMAEPoD/o3Nc3sAAAAASUVORK5CYII=";
            });
            expect(loaded, "PNG data URL did not fire onload");

            image.getCanvasTexture = function () {
                throw new Error("drawImage incorrectly treated an image as a canvas texture source");
            };

            var destination = new _native.Canvas();
            destination.width = 4;
            destination.height = 4;
            var context = destination.getContext("2d");
            context.drawImage(image, 0, 0, 4, 2);
            context.flush();
        }],
        ["Canvas Image rejects invalid data URLs", async function () {
            var ImageCtor = _native.Image || Image;
            var image = new ImageCtor();
            var errored = await new Promise(function (resolve) {
                image.onload = function () { resolve(false); };
                image.onerror = function () { resolve(true); };
                image.src = "data:image/png;base64,AAAA";
            });

            expect(errored, "invalid PNG data URL did not fire onerror");
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
        }],
        ["WebGPU render pipeline honors color target alpha blending", async function () {
            expect(typeof navigator.gpu._testReadTexturePixel === "function", "texture readback test hook missing");

            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var texture = device.createTexture({
                size: [8, 8, 1],
                format: "rgba8unorm",
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
            });
            var shader = device.createShaderModule({
                code:
                    "@vertex fn vsMain(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {\n" +
                    "  var positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n" +
                    "  return vec4f(positions[vertexIndex], 0.0, 1.0);\n" +
                    "}\n" +
                    "@fragment fn fsMain() -> @location(0) vec4f {\n" +
                    "  return vec4f(1.0, 0.0, 0.0, 0.0);\n" +
                    "}\n"
            });
            var pipeline = device.createRenderPipeline({
                layout: "auto",
                vertex: {
                    module: shader,
                    entryPoint: "vsMain"
                },
                fragment: {
                    module: shader,
                    entryPoint: "fsMain",
                    targets: [{
                        format: "rgba8unorm",
                        blend: {
                            color: {
                                srcFactor: "src-alpha",
                                dstFactor: "one-minus-src-alpha",
                                operation: "add"
                            },
                            alpha: {
                                srcFactor: "one",
                                dstFactor: "one-minus-src-alpha",
                                operation: "add"
                            }
                        }
                    }]
                },
                primitive: { topology: "triangle-list" }
            });

            var encoder = device.createCommandEncoder();
            var pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: texture.createView(),
                    loadOp: "clear",
                    storeOp: "store",
                    clearValue: { r: 0, g: 0, b: 1, a: 1 }
                }]
            });
            pass.setPipeline(pipeline);
            pass.draw(3);
            pass.end();
            device.queue.submit([encoder.finish()]);
            await device.queue.onSubmittedWorkDone();

            expectPixel(navigator.gpu._testReadTexturePixel(texture, 4, 4), [0, 0, 255, 255], "transparent red fragment should blend over blue without writing red");
        }],
        ["WebGPU depth-only render pass preserves null color attachments", async function () {
            expect(typeof navigator.gpu._testResetDebugStats === "function", "debug stat reset hook missing");
            expect(typeof navigator.gpu._backendStats === "function", "backend stats hook missing");

            navigator.gpu._testResetDebugStats();
            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var depth = device.createTexture({
                size: [8, 8, 1],
                format: "depth32float",
                usage: GPUTextureUsage.RENDER_ATTACHMENT
            });
            var shader = device.createShaderModule({
                code:
                    "@vertex fn vsMain(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {\n" +
                    "  var positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n" +
                    "  return vec4f(positions[vertexIndex], 0.0, 1.0);\n" +
                    "}\n" +
                    "@fragment fn fsMain() {}\n"
            });
            var pipeline = device.createRenderPipeline({
                layout: "auto",
                vertex: {
                    module: shader,
                    entryPoint: "vsMain"
                },
                fragment: {
                    module: shader,
                    entryPoint: "fsMain",
                    targets: [null]
                },
                primitive: { topology: "triangle-list" },
                depthStencil: {
                    format: "depth32float",
                    depthWriteEnabled: true,
                    depthCompare: "less"
                }
            });

            var encoder = device.createCommandEncoder();
            var pass = encoder.beginRenderPass({
                colorAttachments: [null],
                depthStencilAttachment: {
                    view: depth.createView(),
                    depthLoadOp: "clear",
                    depthClearValue: 1,
                    depthStoreOp: "store"
                }
            });
            pass.setPipeline(pipeline);
            pass.draw(3);
            pass.end();
            device.queue.submit([encoder.finish()]);
            await device.queue.onSubmittedWorkDone();

            var lastError = navigator.gpu._backendStats().lastError;
            expect(!lastError, "depth-only null color attachment pass produced backend error: " + lastError);
        }],
        ["WebGPU render pipeline preserves null vertex buffer layout slots", async function () {
            expect(typeof navigator.gpu._testResetDebugStats === "function", "debug stat reset hook missing");
            expect(typeof navigator.gpu._backendStats === "function", "backend stats hook missing");

            navigator.gpu._testResetDebugStats();
            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var texture = device.createTexture({
                size: [8, 8, 1],
                format: "rgba8unorm",
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
            });
            var vertexBuffer = createFloatBuffer(device, [
                -1, -1,
                3, -1,
                -1, 3
            ], GPUBufferUsage.VERTEX);
            var shader = device.createShaderModule({
                code:
                    "@vertex fn vsMain(@location(0) position : vec2f) -> @builtin(position) vec4f {\n" +
                    "  return vec4f(position, 0.0, 1.0);\n" +
                    "}\n" +
                    "@fragment fn fsMain() -> @location(0) vec4f {\n" +
                    "  return vec4f(1.0, 0.0, 0.0, 1.0);\n" +
                    "}\n"
            });
            var pipeline = device.createRenderPipeline({
                layout: "auto",
                vertex: {
                    module: shader,
                    entryPoint: "vsMain",
                    buffers: [
                        null,
                        {
                            arrayStride: 8,
                            attributes: [{ shaderLocation: 0, offset: 0, format: "float32x2" }]
                        }
                    ]
                },
                fragment: {
                    module: shader,
                    entryPoint: "fsMain",
                    targets: [{ format: "rgba8unorm" }]
                },
                primitive: { topology: "triangle-list" }
            });

            var encoder = device.createCommandEncoder();
            var pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: texture.createView(),
                    loadOp: "clear",
                    clearValue: [0, 0, 0, 1],
                    storeOp: "store"
                }]
            });
            pass.setPipeline(pipeline);
            pass.setVertexBuffer(1, vertexBuffer);
            pass.draw(3);
            pass.end();
            device.queue.submit([encoder.finish()]);
            await device.queue.onSubmittedWorkDone();

            var lastError = navigator.gpu._backendStats().lastError;
            expect(!lastError, "null vertex buffer layout slot produced backend error: " + lastError);
            expectPixel(navigator.gpu._testReadTexturePixel(texture, 4, 4), [255, 0, 0, 255], "null vertex buffer slot should not shift slot 1 attributes into slot 0");
        }],
        ["WebGPU descriptors preserve explicit null optional pipeline and pass states", async function () {
            expect(typeof navigator.gpu._testResetDebugStats === "function", "debug stat reset hook missing");
            expect(typeof navigator.gpu._backendStats === "function", "backend stats hook missing");

            navigator.gpu._testResetDebugStats();
            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var color = device.createTexture({
                size: [8, 8, 1],
                format: "rgba8unorm",
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
            });
            var depth = device.createTexture({
                size: [8, 8, 1],
                format: "depth32float",
                usage: GPUTextureUsage.RENDER_ATTACHMENT
            });
            var colorShader = device.createShaderModule({
                code:
                    "@vertex fn vsMain(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {\n" +
                    "  var positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n" +
                    "  return vec4f(positions[vertexIndex], 0.0, 1.0);\n" +
                    "}\n" +
                    "@fragment fn fsMain() -> @location(0) vec4f {\n" +
                    "  return vec4f(0.0, 1.0, 0.0, 1.0);\n" +
                    "}\n"
            });
            var colorPipeline = device.createRenderPipeline({
                layout: "auto",
                vertex: {
                    module: colorShader,
                    entryPoint: "vsMain"
                },
                fragment: {
                    module: colorShader,
                    entryPoint: "fsMain",
                    targets: [{ format: "rgba8unorm" }]
                },
                depthStencil: null
            });

            var colorEncoder = device.createCommandEncoder();
            var colorPass = colorEncoder.beginRenderPass({
                colorAttachments: [{
                    view: color.createView(),
                    loadOp: "clear",
                    clearValue: [0, 0, 0, 1],
                    storeOp: "store"
                }],
                depthStencilAttachment: null
            });
            colorPass.setPipeline(colorPipeline);
            colorPass.draw(3);
            colorPass.end();
            device.queue.submit([colorEncoder.finish()]);
            await device.queue.onSubmittedWorkDone();
            expectPixel(navigator.gpu._testReadTexturePixel(color, 4, 4), [0, 255, 0, 255], "null depthStencil/depthStencilAttachment should behave as omitted");

            var depthShader = device.createShaderModule({
                code:
                    "@vertex fn vsMain(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {\n" +
                    "  var positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n" +
                    "  return vec4f(positions[vertexIndex], 0.0, 1.0);\n" +
                    "}\n"
            });
            var depthPipeline = device.createRenderPipeline({
                layout: "auto",
                vertex: {
                    module: depthShader,
                    entryPoint: "vsMain"
                },
                fragment: null,
                depthStencil: {
                    format: "depth32float",
                    depthWriteEnabled: true,
                    depthCompare: "less"
                }
            });
            var depthEncoder = device.createCommandEncoder();
            var depthPass = depthEncoder.beginRenderPass({
                colorAttachments: [null],
                depthStencilAttachment: {
                    view: depth.createView(),
                    depthLoadOp: "clear",
                    depthClearValue: 1,
                    depthStoreOp: "store"
                }
            });
            depthPass.setPipeline(depthPipeline);
            depthPass.draw(3);
            depthPass.end();
            device.queue.submit([depthEncoder.finish()]);
            await device.queue.onSubmittedWorkDone();

            var lastError = navigator.gpu._backendStats().lastError;
            expect(!lastError, "explicit null optional descriptor states produced backend error: " + lastError);
        }],
        ["WebGPU pass encoders preserve null bind group unbinds", async function () {
            expect(typeof navigator.gpu._testResetDebugStats === "function", "debug stat reset hook missing");
            expect(typeof navigator.gpu._backendStats === "function", "backend stats hook missing");

            navigator.gpu._testResetDebugStats();
            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var texture = device.createTexture({
                size: [4, 4, 1],
                format: "rgba8unorm",
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
            });
            var layout = device.createBindGroupLayout({ entries: [] });
            var bindGroup = device.createBindGroup({ layout: layout, entries: [] });
            var encoder = device.createCommandEncoder();
            var renderPass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: texture.createView(),
                    loadOp: "clear",
                    clearValue: [0, 0, 0, 1],
                    storeOp: "store"
                }]
            });
            renderPass.setBindGroup(0, bindGroup);
            renderPass.setBindGroup(0, null);
            renderPass.end();

            var computePass = encoder.beginComputePass();
            computePass.setBindGroup(0, bindGroup);
            computePass.setBindGroup(0, null);
            computePass.end();
            device.queue.submit([encoder.finish()]);
            await device.queue.onSubmittedWorkDone();

            var lastError = navigator.gpu._backendStats().lastError;
            expect(!lastError, "null bind group unbind produced backend error: " + lastError);
        }],
        ["BabylonJS WebGPU createEffect reports GLSL shader paths actionably", async function () {
            expect(typeof BABYLON === "object", "BABYLON missing");
            expect(typeof BABYLON.WebGPUEngine === "function", "BABYLON.WebGPUEngine missing");
            expect(BABYLON.Effect && BABYLON.Effect.ShadersStore, "BABYLON.Effect shader store missing");

            var engine = new BABYLON.WebGPUEngine(makeBabylonWebGPUCanvas(16, 16), {
                antialias: false,
                adaptToDeviceRatio: false
            });
            await engine.initAsync();
            installUnsupportedGlslCreateEffectGuard(engine);

            var shaderName = "nativeWebGPUUnsupportedGlslUnit";
            BABYLON.Effect.ShadersStore[shaderName + "VertexShader"] =
                "precision highp float;attribute vec3 position;uniform mat4 worldViewProjection;void main(void){gl_Position=worldViewProjection*vec4(position,1.0);}";
            BABYLON.Effect.ShadersStore[shaderName + "PixelShader"] =
                "precision highp float;void main(void){gl_FragColor=vec4(1.0,0.0,0.0,1.0);}";

            var error = null;
            try {
                engine.createEffect(shaderName, {
                    attributes: ["position"],
                    uniformsNames: ["worldViewProjection"],
                    samplers: [],
                    defines: ""
                });
            } catch (e) {
                error = e;
            } finally {
                engine.dispose();
            }

            expect(error, "GLSL createEffect on NativeWebGPU should throw an actionable JavaScript exception");
            var message = String(error && error.message || error);
            expect(message.indexOf("NATIVE_WEBGPU_UNSUPPORTED_EFFECT") !== -1, "missing NativeWebGPU unsupported-effect marker: " + message);
            expect(message.indexOf("BabylonJS requested a GLSL WebGPU effect") !== -1, "missing GLSL WebGPU explanation: " + message);
            expect(message.indexOf("createEffect=" + shaderName) !== -1, "missing createEffect shader name: " + message);
            expect(message.indexOf("effect=" + shaderName) !== -1, "missing effect name: " + message);
            expect(message.indexOf("does not ship the glslang/twgsl fallback") !== -1, "missing fallback explanation: " + message);
            expect(message.indexOf("WGSL implementation") !== -1, "missing WGSL remediation: " + message);
            expect(message.indexOf("shaderLanguage=WGSL") !== -1, "missing shaderLanguage remediation: " + message);
            expect(message.indexOf("NativeWebGPU exclusion") !== -1, "missing exclusion remediation: " + message);
        }],
        ["BabylonJS WebGPU createEffect surfaces async preparation rejection actionably", async function () {
            expect(typeof BABYLON === "object", "BABYLON missing");
            expect(typeof BABYLON.WebGPUEngine === "function", "BABYLON.WebGPUEngine missing");
            expect(BABYLON.ShaderLanguage && BABYLON.ShaderLanguage.WGSL !== undefined, "BABYLON.ShaderLanguage.WGSL missing");

            var engine = new BABYLON.WebGPUEngine(makeBabylonWebGPUCanvas(16, 16), {
                antialias: false,
                adaptToDeviceRatio: false
            });
            await engine.initAsync();

            var marker = "native async shader preparation sentinel";
            var callbackError = "";
            var observableError = null;
            engine.onEffectErrorObservable.add(function (event) {
                observableError = event;
            });

            var effect = engine.createEffect("nativeAsyncPreparationRejectUnit", {
                attributes: [],
                uniformsNames: [],
                samplers: [],
                defines: "",
                shaderLanguage: BABYLON.ShaderLanguage.WGSL,
                extraInitializationsAsync: function nativeAsyncPreparationRejectUnit() {
                    return Promise.reject(new Error(marker));
                },
                onError: function (_effect, errors) {
                    callbackError = String(errors || "");
                }
            });

            await Promise.resolve();
            await Promise.resolve();
            await Promise.resolve();

            var observableMessage = String(observableError && observableError.errors || "");
            var compilationMessage = String(effect && effect.getCompilationError && effect.getCompilationError() || "");
            var message = callbackError || observableMessage || compilationMessage;
            engine.dispose();

            expect(message.indexOf(marker) !== -1, "missing original async rejection reason: " + message);
            expect(message.indexOf("Effect async shader preparation failed") !== -1, "missing async preparation context: " + message);
            expect(message.indexOf("nativeAsyncPreparationRejectUnit") !== -1, "missing effect or stack context: " + message);
            expect(callbackError.indexOf(marker) !== -1, "effect onError did not receive async rejection: " + callbackError);
            expect(observableMessage.indexOf(marker) !== -1, "engine effect-error observable did not receive async rejection: " + observableMessage);
        }],
        ["GPUCanvasContext renders to distinct offscreen canvases with predictable lifetimes", async function () {
            expect(typeof navigator.gpu._testReadTexturePixel === "function", "texture readback test hook missing");

            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var contextA = makeCanvasContext(device, 8, 8);
            var contextB = makeCanvasContext(device, 8, 8);

            var textureA = contextA.getCurrentTexture();
            var textureB = contextB.getCurrentTexture();
            expect(textureId(textureA) !== 0, "canvas A texture id missing");
            expect(textureId(textureB) !== 0, "canvas B texture id missing");
            expect(textureId(textureA) !== textureId(textureB), "offscreen canvas textures should be distinct");

            clearTexture(device, textureA, { r: 1, g: 0, b: 0, a: 1 });
            clearTexture(device, textureB, { r: 0, g: 1, b: 0, a: 1 });
            await device.queue.onSubmittedWorkDone();

            expectPixel(navigator.gpu._testReadTexturePixel(textureA, 0, 0), [255, 0, 0, 255], "canvas A retained its red clear");
            expectPixel(navigator.gpu._testReadTexturePixel(textureB, 0, 0), [0, 255, 0, 255], "canvas B retained its green clear");

            contextA.canvas.width = 16;
            var textureAResized = contextA.getCurrentTexture();
            expect(textureId(textureAResized) !== textureId(textureA), "resizing canvas A should acquire a new texture");
            expectEqual(textureId(contextB.getCurrentTexture()), textureId(textureB), "resizing canvas A should not rotate canvas B");

            clearTexture(device, textureAResized, { r: 0, g: 0, b: 1, a: 1 });
            await device.queue.onSubmittedWorkDone();
            expectPixel(navigator.gpu._testReadTexturePixel(textureAResized, 0, 0), [0, 0, 255, 255], "resized canvas A retained its blue clear");
            expectPixel(navigator.gpu._testReadTexturePixel(textureB, 0, 0), [0, 255, 0, 255], "canvas B survived canvas A resize");

            contextA.unconfigure();
            var destroyedTextureRejected = false;
            try {
                textureAResized.createView({ label: "after-unconfigure" });
            } catch (error) {
                destroyedTextureRejected = String(error && error.message || error).indexOf("GPUTexture") !== -1;
            }
            expect(destroyedTextureRejected, "unconfiguring canvas A should invalidate its current texture predictably");
            expectPixel(navigator.gpu._testReadTexturePixel(textureB, 0, 0), [0, 255, 0, 255], "canvas B survived canvas A unconfigure");

            contextB.unconfigure();
        }],
        ["WebGPU instanced matrix and color vertex attributes render distinct instances", async function () {
            expect(typeof navigator.gpu._testReadTexturePixel === "function", "texture readback test hook missing");

            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var texture = device.createTexture({
                size: [32, 16, 1],
                format: "rgba8unorm",
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
            });

            var vertices = createFloatBuffer(device, [
                -1, -1,
                 1, -1,
                -1,  1,
                -1,  1,
                 1, -1,
                 1,  1
            ], GPUBufferUsage.VERTEX);

            var matrices = createFloatBuffer(device, [
                0.35, 0, 0, 0,  0, 0.7, 0, 0,  0, 0, 1, 0,  -0.45, 0, 0, 1,
                0.35, 0, 0, 0,  0, 0.7, 0, 0,  0, 0, 1, 0,   0.45, 0, 0, 1
            ], GPUBufferUsage.VERTEX);

            var colors = createFloatBuffer(device, [
                1, 0, 0, 1,
                0, 1, 0, 1
            ], GPUBufferUsage.VERTEX);

            var shader = device.createShaderModule({
                code:
                    "struct VSOut { @builtin(position) position : vec4f, @location(0) color : vec4f };\n" +
                    "@vertex fn vsMain(@location(0) position : vec2f, @location(1) m0 : vec4f, @location(2) m1 : vec4f, @location(3) m2 : vec4f, @location(4) m3 : vec4f, @location(5) color : vec4f) -> VSOut {\n" +
                    "  var out : VSOut;\n" +
                    "  let world = mat4x4f(m0, m1, m2, m3) * vec4f(position, 0.0, 1.0);\n" +
                    "  out.position = world;\n" +
                    "  out.color = color;\n" +
                    "  return out;\n" +
                    "}\n" +
                    "@fragment fn fsMain(in : VSOut) -> @location(0) vec4f { return in.color; }\n"
            });

            var pipeline = device.createRenderPipeline({
                layout: "auto",
                vertex: {
                    module: shader,
                    entryPoint: "vsMain",
                    buffers: [
                        {
                            arrayStride: 8,
                            attributes: [{ shaderLocation: 0, offset: 0, format: "float32x2" }]
                        },
                        {
                            arrayStride: 64,
                            stepMode: "instance",
                            attributes: [
                                { shaderLocation: 1, offset: 0, format: "float32x4" },
                                { shaderLocation: 2, offset: 16, format: "float32x4" },
                                { shaderLocation: 3, offset: 32, format: "float32x4" },
                                { shaderLocation: 4, offset: 48, format: "float32x4" }
                            ]
                        },
                        {
                            arrayStride: 16,
                            stepMode: "instance",
                            attributes: [{ shaderLocation: 5, offset: 0, format: "float32x4" }]
                        }
                    ]
                },
                fragment: {
                    module: shader,
                    entryPoint: "fsMain",
                    targets: [{ format: "rgba8unorm" }]
                },
                primitive: { topology: "triangle-list" }
            });

            var encoder = device.createCommandEncoder();
            var pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: texture.createView(),
                    loadOp: "clear",
                    storeOp: "store",
                    clearValue: { r: 0, g: 0, b: 0, a: 1 }
                }]
            });
            pass.setPipeline(pipeline);
            pass.setVertexBuffer(0, vertices);
            pass.setVertexBuffer(1, matrices);
            pass.setVertexBuffer(2, colors);
            pass.draw(6, 2);
            pass.end();
            device.queue.submit([encoder.finish()]);
            await device.queue.onSubmittedWorkDone();

            expectPixel(navigator.gpu._testReadTexturePixel(texture, 8, 8), [255, 0, 0, 255], "left matrix/color instance");
            expectPixel(navigator.gpu._testReadTexturePixel(texture, 24, 8), [0, 255, 0, 255], "right matrix/color instance");
        }],
        ["GPUQueue.copyExternalImageToTexture flipY uses a single upload buffer", async function () {
            expect(typeof navigator.gpu._testResetDebugStats === "function", "debug stat reset hook missing");
            expect(typeof navigator.gpu._backendStats === "function", "backend stats hook missing");
            expect(typeof navigator.gpu._testReadTexturePixel === "function", "texture readback test hook missing");

            navigator.gpu._testResetDebugStats();
            var adapter = await navigator.gpu.requestAdapter();
            var device = await adapter.requestDevice();
            var rgba = new Uint8Array([
                255, 0, 0, 255,   0, 255, 0, 255,
                0, 0, 255, 255,   255, 255, 255, 255
            ]);
            var source = {
                _getNativeImageData: function () {
                    return { width: 2, height: 2, data: rgba };
                }
            };
            var usage = GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING;
            var textureNoFlip = device.createTexture({ size: [2, 2, 1], format: "rgba8unorm", usage: usage });
            var stats0 = navigator.gpu._backendStats();
            device.queue.copyExternalImageToTexture({ source: source }, { texture: textureNoFlip }, { width: 2, height: 2 });
            await device.queue.onSubmittedWorkDone();
            var stats1 = navigator.gpu._backendStats();
            expectEqual(stats1.externalImageUploadBorrowedCount, stats0.externalImageUploadBorrowedCount + 1, "contiguous RGBA upload should borrow decoded bytes");
            expectEqual(stats1.externalImageUploadBorrowedBytes, stats0.externalImageUploadBorrowedBytes + 16, "borrowed upload byte count");
            expectEqual(stats1.externalImageUploadOwnedCount, stats0.externalImageUploadOwnedCount, "no-flip RGBA upload should not allocate an owned upload");
            expectPixel(navigator.gpu._testReadTexturePixel(textureNoFlip, 0, 0), [255, 0, 0, 255], "no-flip upload orientation");

            var textureFlip = device.createTexture({ size: [2, 2, 1], format: "rgba8unorm", usage: usage });
            device.queue.copyExternalImageToTexture({ source: source, flipY: true }, { texture: textureFlip }, { width: 2, height: 2 });
            await device.queue.onSubmittedWorkDone();
            var stats2 = navigator.gpu._backendStats();
            expectEqual(stats2.externalImageUploadOwnedCount, stats1.externalImageUploadOwnedCount + 1, "flipY upload should allocate exactly one upload buffer");
            expectEqual(stats2.externalImageUploadOwnedBytes, stats1.externalImageUploadOwnedBytes + 16, "flipY owned upload should be exactly the copy size");
            expectPixel(navigator.gpu._testReadTexturePixel(textureFlip, 0, 0), [0, 0, 255, 255], "flipY upload orientation");
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
