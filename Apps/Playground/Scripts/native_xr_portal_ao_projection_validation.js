// Validation only: test projection reconstruction and bounded kernel indexing.
// Load after the profile prelude, before the demo creates its SSAO effects.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var shader = store.ssao2PixelShader;
    function replaceOnce(pattern, replacement) {
        var matches = shader.match(new RegExp(pattern.source, "g"));
        if (!matches || matches.length !== 1) {
            throw new Error("SSAO validation shader no longer matches the published reconstruction/kernel code: " + pattern.source);
        }
        shader = shader.replace(pattern, replacement);
    }
    replaceOnce(/\(input\.vUV\.x\s*\*\s*2\.0\s*-\s*1\.0\)\s*\*\s*uniforms\.xViewport/,
        "(input.vUV.x * 2.0 - 1.0 - uniforms.projection[2].x * depthSign) * uniforms.xViewport");
    replaceOnce(/\(input\.vUV\.y\s*\*\s*2\.0\s*-\s*1\.0\)\s*\*\s*uniforms\.yViewport/,
        "(input.vUV.y * 2.0 - 1.0 - uniforms.projection[2].y * depthSign) * uniforms.yViewport");
    replaceOnce(/uniforms\.sampleSphere\[\(i\s*\+\s*i32\(random\.y\s*\*\s*16\.0\)\)\s*%\s*16\]/,
        "uniforms.sampleSphere[(i + i32(random.y * f32(SAMPLES))) % SAMPLES]");
    store.ssao2PixelShader = shader;
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        var ssao = pipeline && pipeline._ssaoPostProcess;
        if (!ssao) {
            reportStatus("native-xr-portal:ao-projection-validation=unavailable");
            return;
        }
        var reported = false;
        var geometry = scene.geometryBufferRenderer;
        var gbuffer = geometry && geometry.getGBuffer();
        var geometryState = null;
        var logCoordinates = globalThis.__nativeArPortalLogAoCoordinates !== false;
        var geometryObserver = logCoordinates && gbuffer && gbuffer.onAfterRenderObservable.add(function () {
            geometryState = {
                camera: scene.activeCamera.name,
                size: gbuffer.getSize(),
                attachments: gbuffer.textures.map(function (texture) {
                    var internal = texture.getInternalTexture();
                    return { name: texture.name, type: internal && internal.type, format: internal && internal.format };
                }),
                viewport: scene.getEngine()._cachedViewport,
                pixelViewport: Object.assign({}, scene.getEngine()._viewportsCurrent),
                scissor: Object.assign({}, scene.getEngine()._scissorCached),
                projection: Array.from(scene.getProjectionMatrix().m),
                view: Array.from(scene.getViewMatrix().m)
            };
        });
        // onApply runs before EffectWrapper.bind, which would overwrite these values.
        var observer = ssao.onBeforeRenderObservable.add(function (effect) {
            var camera = scene.activeCamera;
            if (!camera || camera.mode !== BABYLON.Camera.PERSPECTIVE_CAMERA) {
                return;
            }
            var projection = camera.getProjectionMatrix().m;
            var xViewport = 1 / projection[0];
            var yViewport = 1 / projection[5];
            if (!Number.isFinite(xViewport) || !Number.isFinite(yViewport) || xViewport === 0 || yViewport === 0) {
                throw new Error("SSAO projection validation requires finite, nonzero projection scales.");
            }
            effect.setFloat("xViewport", xViewport);
            effect.setFloat("yViewport", yViewport);
            if (!reported) {
                reported = true;
                reportStatus("native-xr-portal:ao-projection-validation=enabled:screenAspect=" +
                    scene.getEngine().getAspectRatio(camera, true) + ":projectionAspect=" +
                    projection[5] / projection[0] + ":principalPoint=" + projection[8] + "," + projection[9]);
            }
        });
        var lastReport = -Infinity;
        var reports = 0;
        var afterObserver = logCoordinates && ssao.onAfterRenderObservable.add(function (effect) {
            if (!geometryState || reports >= 12 || performance.now() - lastReport < 5000) {
                return;
            }
            lastReport = performance.now();
            reports++;
            var uniformBuffer = effect.getPipelineContext().uniformBuffer;
            function uniform(name, length) {
                var offset = uniformBuffer && uniformBuffer._uniformLocations[name];
                return offset === undefined ? null : Array.from(uniformBuffer.getData().slice(offset, offset + length));
            }
            var camera = scene.activeCamera;
            reportStatus("native-xr-portal:ao-coordinates=" + JSON.stringify({
                geometry: geometryState,
                ao: {
                    camera: camera.name,
                    viewport: camera.viewport,
                    pixelViewport: Object.assign({}, scene.getEngine()._viewportsCurrent),
                    scissor: Object.assign({}, scene.getEngine()._scissorCached),
                    size: { width: ssao.width, height: ssao.height },
                    projection: uniform("projection", 16),
                    rayX: uniform("xViewport", 1),
                    rayY: uniform("yViewport", 1),
                    view: Array.from(camera.getViewMatrix().m)
                }
            }));
        });
        scene.onDisposeObservable.add(function () {
            ssao.onBeforeRenderObservable.remove(observer);
            if (afterObserver) {
                ssao.onAfterRenderObservable.remove(afterObserver);
            }
            if (geometryObserver) {
                gbuffer.onAfterRenderObservable.remove(geometryObserver);
            }
        });
    };
})();
