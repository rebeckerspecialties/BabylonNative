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

    function expectPixel(actual, expected, message) {
        expect(actual && actual.length === 4, message + ": pixel readback did not return four channels");
        for (var i = 0; i < 4; i++) {
            if (Math.abs(Number(actual[i]) - expected[i]) > 2) {
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
        expect(typeof navigator.gpu._testReadTexturePixel === "function", "texture readback test hook missing");

        var adapter = await navigator.gpu.requestAdapter();
        var device = await adapter.requestDevice();
        var texture = device.createTexture({
            size: [width, height, 1],
            format: "rgba8unorm",
            usage: GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
        });
        device.queue.copyExternalImageToTexture({ source: source }, { texture: texture }, { width: width, height: height });
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
