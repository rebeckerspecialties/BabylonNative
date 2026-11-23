// Optional prelude after native_xr_portal_profile.js. Timings include probe overhead.
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var engine = scene.getEngine();
        var patches = [];
        var metrics = [];
        var stack = [];
        var sampling = false;
        var frameNumber = 0;
        var sampledFrames = 0;
        var lastReport = performance.now();

        function wrap(target, method, label) {
            if (!target || typeof target[method] !== "function") {
                return;
            }
            var original = target[method];
            var descriptor = Object.getOwnPropertyDescriptor(target, method);
            var metric = metrics.find(function (value) { return value.name === label; });
            if (!metric) {
                metric = { name: label, calls: 0, inclusiveMs: 0, selfMs: 0 };
                metrics.push(metric);
            }
            var replacement = function () {
                if (!sampling) {
                    return original.apply(this, arguments);
                }
                var entry = { start: performance.now(), childMs: 0 };
                stack.push(entry);
                try {
                    return original.apply(this, arguments);
                } finally {
                    var elapsed = performance.now() - entry.start;
                    stack.pop();
                    metric.calls++;
                    metric.inclusiveMs += elapsed;
                    metric.selfMs += elapsed - entry.childMs;
                    if (stack.length) {
                        stack[stack.length - 1].childMs += elapsed;
                    }
                }
            };
            Object.defineProperty(target, method, {
                configurable: true, enumerable: descriptor ? descriptor.enumerable : false,
                writable: true, value: replacement
            });
            patches.push({ target: target, method: method, descriptor: descriptor, replacement: replacement });
        }

        function wrapPrototype(type, methods) {
            var constructor = BABYLON[type];
            if (!constructor) {
                return;
            }
            for (var i = 0; i < methods.length; i++) {
                wrap(constructor.prototype, methods[i], type + "." + methods[i]);
            }
        }

        wrapPrototype("PBRBaseMaterial", ["isReadyForSubMesh", "bindForSubMesh", "_afterBind", "bindView", "bindViewProjection", "bindEyePosition"]);
        wrapPrototype("StandardMaterial", ["isReadyForSubMesh", "bindForSubMesh"]);
        wrapPrototype("Mesh", ["render", "_processRendering", "transferToEffect"]);
        wrapPrototype("Texture", ["getTextureMatrix", "getReflectionTextureMatrix"]);
        wrapPrototype("Light", ["_bindLight"]);
        wrapPrototype("Effect", ["setMatrix"]);
        wrapPrototype("ImageProcessingConfiguration", ["bind"]);
        wrapPrototype("UniformBuffer", ["update", "_updateOwnerKeyed", "_buffersEqual", "_copyBuffer", "updateUniform", "bindTexture", "bindToEffect"]);
        ["_draw", "_setTexture", "bindUniformBufferBase", "updateUniformBuffer"].forEach(function (method) {
            wrap(engine, method, "engine." + method);
        });
        wrap(engine._cacheRenderPipeline, "getRenderPipeline", "pipeline.getRenderPipeline");
        wrap(engine._cacheBindGroups, "getBindGroups", "bindings.getBindGroups");
        wrap(scene, "_evaluateActiveMeshes", "scene._evaluateActiveMeshes");
        (scene.materials || []).forEach(function (material) {
            wrap(material.prePassConfiguration, "bindForSubMesh", "prepass.bindForSubMesh");
            wrap(material, "_callbackPluginEventBindForSubMesh", "plugins.bindForSubMesh");
            wrap(material, "_callbackPluginEventHardBindForSubMesh", "plugins.hardBindForSubMesh");
        });
        var batching = engine._renderPassCommandBatching;
        wrap(batching && batching.batcher, "submit", "commands.submit");

        reportStatus("native-xr-portal:cpu-calls:start:interval=30:methods=" + metrics.length);
        var before = scene.onBeforeRenderObservable.add(function () {
            sampling = frameNumber++ % 30 === 0;
        });
        var after = scene.onAfterRenderObservable.add(function () {
            if (sampling) {
                sampledFrames++;
            }
            sampling = false;
            var now = performance.now();
            if (sampledFrames === 0 || now - lastReport < 3000) {
                return;
            }
            var values = metrics.map(function (metric) {
                return {
                    name: metric.name, calls: metric.calls / sampledFrames,
                    inclusiveMs: metric.inclusiveMs / sampledFrames, selfMs: metric.selfMs / sampledFrames
                };
            });
            reportStatus("native-xr-portal:cpu-calls:" + JSON.stringify({ frames: sampledFrames, metrics: values }));
            for (var i = 0; i < metrics.length; i++) {
                metrics[i].calls = metrics[i].inclusiveMs = metrics[i].selfMs = 0;
            }
            sampledFrames = 0;
            lastReport = now;
        });
        scene.onDisposeObservable.add(function () {
            sampling = false;
            scene.onBeforeRenderObservable.remove(before);
            scene.onAfterRenderObservable.remove(after);
            for (var i = patches.length - 1; i >= 0; i--) {
                var patch = patches[i];
                if (patch.target[patch.method] !== patch.replacement) {
                    continue;
                }
                if (patch.descriptor) {
                    Object.defineProperty(patch.target, patch.method, patch.descriptor);
                } else {
                    delete patch.target[patch.method];
                }
            }
        });
    };
})();
