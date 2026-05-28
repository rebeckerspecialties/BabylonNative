globalThis.__babylonUseWebGPU = true;

function setNativeArPortalDefault(name, value) {
    if (typeof globalThis[name] === "undefined") {
        globalThis[name] = value;
    }
}

setNativeArPortalDefault("__nativeArPortalDebugDisableOccluders", false);
setNativeArPortalDefault("__nativeArPortalDebugBrightHillValley", false);
setNativeArPortalDefault("__nativeArPortalUseGpuParticles", true);
setNativeArPortalDefault("__nativeArPortalParticleCapacity", 1024);
setNativeArPortalDefault("__nativeArPortalUseWebGPUFastPath", false);
setNativeArPortalDefault("__nativeArPortalUseSceneStaticFastPath", false);
setNativeArPortalDefault("__nativeArPortalFreezeMaterials", false);
setNativeArPortalDefault("__nativeArPortalLogMeshDiagnostics", false);
setNativeArPortalDefault("__nativeArPortalUseOfficialCrossingTest", true);
setNativeArPortalDefault("__nativeArPortalEnablePlaneDetection", true);
setNativeArPortalDefault("__nativeArPortalAllowFallbackPlacement", false);
setNativeArPortalDefault("__nativeArPortalAllowEstimatedPointPlacement", false);
setNativeArPortalDefault("__nativeArPortalUseAnchors", true);
setNativeArPortalDefault("__nativeArPortalApplyAnchorUpdates", false);
setNativeArPortalDefault("__nativeArPortalMarkerSettleFrames", 24);
setNativeArPortalDefault("__nativeArPortalMarkerSettleDistanceMeters", 0.018);
setNativeArPortalDefault("__nativeArPortalInvertOccluderSide", false);
setNativeArPortalDefault("__nativeArPortalUseRealtimeLighting", true);
setNativeArPortalDefault("__nativeArPortalUseImportedPunctualLights", false);
setNativeArPortalDefault("__nativeArPortalUseDirectionalFillLight", false);
setNativeArPortalDefault("__nativeArPortalUseSceneReflectionProbe", false);
setNativeArPortalDefault("__nativeArPortalUseEnvironmentReflectionOverride", true);
setNativeArPortalDefault("__nativeArPortalReflectionProbeSize", 128);
setNativeArPortalDefault("__nativeArPortalUseSSAO2", true);
setNativeArPortalDefault("__nativeArPortalUseIblShadows", false);
setNativeArPortalDefault("__nativeArPortalForceNonXrStress", false);
setNativeArPortalDefault("__nativeArPortalSSAOScale", 0.30);
setNativeArPortalDefault("__nativeArPortalSSAOBlurScale", 0.45);
setNativeArPortalDefault("__nativeArPortalSSAOStrength", 0.42);
setNativeArPortalDefault("__nativeArPortalSSAORadius", 0.85);
setNativeArPortalDefault("__nativeArPortalSSAOBase", 0.76);
setNativeArPortalDefault("__nativeArPortalSSAOSamples", 8);
setNativeArPortalDefault("__nativeArPortalIblShadowScale", 0.22);
setNativeArPortalDefault("__nativeArPortalIblShadowResolutionExp", 3);
setNativeArPortalDefault("__nativeArPortalIblShadowMaxCasters", 24);
setNativeArPortalDefault("__nativeArPortalIblShadowScreenSpace", false);
setNativeArPortalDefault("__nativeArPortalExitDelayMs", 70000);

(function () {
    var EXIT_DELAY_MS = Math.max(0, Math.min(600000, Number(globalThis.__nativeArPortalExitDelayMs || 70000)));
    var ASSET_ROOT_URL = "app:///Scripts/";
    var HILL_VALLEY_FILE = "native_xr_portal_hillvalley.astc.glb";
    var ENVIRONMENT_CUBE_FILE = "native_xr_portal_environment.ktx";
    var BLUE_NOISE_FILE = "native_xr_portal_blue_noise_rgb.png";
    var PARTICLE_TOP_FILE = "app:///Scripts/native_xr_portal_particle_488.json";
    var PARTICLE_SIDE_FILE = "app:///Scripts/native_xr_portal_particle_489.json";
    var lastStats = null;
    var lastFpsLogTime = 0;

    function reportStatus(message) {
        try {
            if (typeof globalThis.__nativePlaygroundStatus === "function") {
                globalThis.__nativePlaygroundStatus(String(message));
            }
        } catch (error) {
            void error;
        }
    }

    function getStatsSnapshot() {
        if (typeof navigator === "undefined" || !navigator.gpu || typeof navigator.gpu._backendStats !== "function") {
            return null;
        }

        try {
            return navigator.gpu._backendStats();
        } catch (error) {
            reportStatus("native-xr-portal:stats-error:" + String(error && error.message ? error.message : error));
            return null;
        }
    }

    function logStatsDelta(prefix) {
        var stats = getStatsSnapshot();
        if (!stats) {
            return;
        }

        var gpuMiB = Number(stats.estimatedGpuMemoryMiB);
        if (!isFinite(gpuMiB)) {
            gpuMiB = Number(stats.estimatedGpuMemoryBytes || 0) / (1024 * 1024);
        }

        if (lastStats) {
            var frames = Number(stats.nativeRenderFrameCount || stats.renderFrames || 0) - Number(lastStats.nativeRenderFrameCount || lastStats.renderFrames || 0);
            var submit = Number(stats.queueSubmitCount || 0) - Number(lastStats.queueSubmitCount || 0);
            var encoders = Number(stats.commandEncoderCreateCount || 0) - Number(lastStats.commandEncoderCreateCount || 0);
            var passes = Number(stats.renderPassBeginCount || 0) - Number(lastStats.renderPassBeginCount || 0);
            var draws = Number(stats.drawCallCount || 0) - Number(lastStats.drawCallCount || 0);
            var commandStreams = Number(stats.renderPassCommandStreamCount || 0) - Number(lastStats.renderPassCommandStreamCount || 0);
            var commandWords = Number(stats.renderPassCommandStreamWordCount || 0) - Number(lastStats.renderPassCommandStreamWordCount || 0);
            var multiDrawCalls = Number(stats.multiDrawIndirectCallCount || 0) - Number(lastStats.multiDrawIndirectCallCount || 0);
            var multiDraws = Number(stats.multiDrawIndirectDrawCount || 0) - Number(lastStats.multiDrawIndirectDrawCount || 0);
            var pipelines = Number(stats.renderPipelineCreateCount || 0) - Number(lastStats.renderPipelineCreateCount || 0);
            var bindGroups = Number(stats.bindGroupCreateCount || 0) - Number(lastStats.bindGroupCreateCount || 0);
            var textures = Number(stats.textureCreateCount || 0) - Number(lastStats.textureCreateCount || 0);
            var textureViews = Number(stats.textureViewCreateCount || 0) - Number(lastStats.textureViewCreateCount || 0);
            var buffers = Number(stats.bufferCreateCount || 0) - Number(lastStats.bufferCreateCount || 0);
            var uploadOwned = Number(stats.externalImageUploadOwnedCount || 0) - Number(lastStats.externalImageUploadOwnedCount || 0);
            var uploadBorrowed = Number(stats.externalImageUploadBorrowedCount || 0) - Number(lastStats.externalImageUploadBorrowedCount || 0);
            reportStatus(prefix + ":frames=" + frames +
                ":submit=" + submit +
                ":encoders=" + encoders +
                ":passes=" + passes +
                ":draws=" + draws +
                ":cmdStreams=" + commandStreams + "/" + commandWords +
                ":multiDraw=" + multiDrawCalls + "/" + multiDraws +
                ":pipelines=" + pipelines +
                ":bindGroups=" + bindGroups +
                ":textures=" + textures +
                ":views=" + textureViews +
                ":buffers=" + buffers +
                ":uploads=" + uploadOwned + "/" + uploadBorrowed +
                ":gpuMiB=" + gpuMiB.toFixed(2) +
                ":drawPath=" + (stats.drawPathActive ? "1" : "0"));
        } else {
            reportStatus(prefix + ":gpuMiB=" + gpuMiB.toFixed(2) +
                ":backend=" + String(stats.backendMode || "unknown") +
                ":presentation=" + String(stats.presentationPath || "unknown") +
                ":drawPath=" + (stats.drawPathActive ? "1" : "0"));
        }

        lastStats = stats;
    }

    function enableWebGPUFastPath(engine) {
        if (!engine || globalThis.__nativeArPortalUseWebGPUFastPath === false) {
            return;
        }

        try {
            if ("compatibilityMode" in engine) {
                engine.compatibilityMode = false;
                reportStatus("native-xr-portal:webgpu-fast-path=render-bundles");
            }
            if ("dbgSanityChecks" in engine) {
                engine.dbgSanityChecks = false;
            }
            if ("dbgLogIfNotDrawWrapper" in engine) {
                engine.dbgLogIfNotDrawWrapper = false;
            }
            if ("dbgShowEmptyEnableEffectCalls" in engine) {
                engine.dbgShowEmptyEnableEffectCalls = false;
            }
        } catch (error) {
            reportStatus("native-xr-portal:webgpu-fast-path-error:" + String(error && error.message ? error.message : error));
        }
    }

    function enableSceneStaticFastPath(scene) {
        try {
            if (BABYLON.ScenePerformancePriority && BABYLON.ScenePerformancePriority.Aggressive !== undefined) {
                scene.performancePriority = BABYLON.ScenePerformancePriority.Aggressive;
            } else {
                scene.skipFrustumClipping = true;
                scene.skipPointerMovePicking = true;
            }
            scene.autoClear = true;
            scene.skipFrustumClipping = true;
            reportStatus("native-xr-portal:scene-fast-path=static");
        } catch (error) {
            reportStatus("native-xr-portal:scene-fast-path-error:" + String(error && error.message ? error.message : error));
        }
    }

    function prepareStaticMeshForFastPath(mesh) {
        if (!mesh) {
            return;
        }

        mesh.isPickable = false;
    }

    function loadTextAsync(url) {
        return new Promise(function (resolve, reject) {
            BABYLON.Tools.LoadFile(
                url,
                function (responseText) {
                    resolve(String(responseText));
                },
                undefined,
                undefined,
                false,
                function (request, exception) {
                    var status = request ? (String(request.status) + " " + String(request.statusText || "")) : "no response";
                    reject(new Error("Failed to load " + url + ": " + status + (exception ? ": " + String(exception) : "")));
                });
        });
    }

    async function loadLocalParticleSystemAsync(url, scene, emitter) {
        var responseText = await loadTextAsync(url);
        var serializationObject = JSON.parse(responseText);
        var capacity = Math.max(256, Number(globalThis.__nativeArPortalParticleCapacity || serializationObject.capacity || 2048));
        var system = null;
        if (globalThis.__nativeArPortalUseGpuParticles !== false && BABYLON.GPUParticleSystem && BABYLON.GPUParticleSystem.IsSupported) {
            try {
                serializationObject.emitRateControl = true;
                system = BABYLON.GPUParticleSystem.Parse(serializationObject, scene, "", true, capacity);
                reportStatus("native-xr-portal:particle-mode:gpu:capacity=" + String(capacity));
            } catch (error) {
                reportStatus("native-xr-portal:particle-gpu-fallback:" + String(error && error.message ? error.message : error));
                system = null;
            }
        }
        if (!system) {
            system = BABYLON.ParticleSystem.Parse(serializationObject, scene, "", true, capacity);
            reportStatus("native-xr-portal:particle-mode:cpu:capacity=" + String(capacity));
        }
        system.emitter = emitter;
        system.start();
        reportStatus("native-xr-portal:particle-loaded:" + url);
        return system;
    }

    function formatVector3(value) {
        if (!value) {
            return "none";
        }

        return Number(value.x || 0).toFixed(3) + "," + Number(value.y || 0).toFixed(3) + "," + Number(value.z || 0).toFixed(3);
    }

    function vectorDistance(a, b) {
        if (!a || !b) {
            return Number.MAX_VALUE;
        }

        var dx = Number(a.x || 0) - Number(b.x || 0);
        var dy = Number(a.y || 0) - Number(b.y || 0);
        var dz = Number(a.z || 0) - Number(b.z || 0);
        return Math.sqrt((dx * dx) + (dy * dy) + (dz * dz));
    }

    function compactResourceName(value) {
        var text = String(value || "unnamed");
        if (text.length <= 96) {
            return text;
        }

        return text.slice(0, 92) + "...";
    }

    function ensureBrowserLocationForLoaders() {
        try {
            if (typeof window !== "undefined") {
                if (!window.location) {
                    window.location = { href: "app:///Scripts/native_xr_babylon_portal_demo.js" };
                } else if (typeof window.location.href !== "string") {
                    window.location.href = "app:///Scripts/native_xr_babylon_portal_demo.js";
                }
            }
        } catch (error) {
            reportStatus("native-xr-portal:location-polyfill-error:" + String(error && error.message ? error.message : error));
        }
    }

    function augmentNativePortalElement(element, type, width, height) {
        var tagName = String(type || "div").toUpperCase();
        element.nodeType = element.nodeType || 1;
        element.nodeName = element.nodeName || tagName;
        element.tagName = element.tagName || tagName;
        element.style = element.style || {};
        element.attributes = element.attributes || {};
        element.children = element.children || [];
        element.childNodes = element.childNodes || element.children;
        element.clientWidth = element.clientWidth || width || 1;
        element.clientHeight = element.clientHeight || height || 1;
        element.width = element.width || element.clientWidth;
        element.height = element.height || element.clientHeight;
        element.addEventListener = element.addEventListener || function () { };
        element.removeEventListener = element.removeEventListener || function () { };
        element.setAttribute = element.setAttribute || function (name, value) {
            element.attributes[String(name)] = String(value);
            element[String(name)] = String(value);
        };
        element.getAttribute = element.getAttribute || function (name) {
            var key = String(name);
            return Object.prototype.hasOwnProperty.call(element.attributes, key) ? element.attributes[key] : null;
        };
        element.appendChild = element.appendChild || function (child) {
            if (child) {
                child.parentNode = element;
                element.children.push(child);
            }
            return child;
        };
        element.removeChild = element.removeChild || function (child) {
            var index = element.children.indexOf(child);
            if (index !== -1) {
                element.children.splice(index, 1);
                child.parentNode = null;
            }
            return child;
        };
        return element;
    }

    function createNativePortalCanvas(width, height) {
        var canvas = typeof _native !== "undefined" && _native.Canvas ? new _native.Canvas() : {};
        canvas.width = width || canvas.width || 64;
        canvas.height = height || canvas.height || 64;
        return augmentNativePortalElement(canvas, "canvas", canvas.width, canvas.height);
    }

    function createNativePortalDocumentElement(type) {
        var tagName = String(type || "div").toLowerCase();
        if (tagName === "canvas") {
            return createNativePortalCanvas(64, 64);
        }
        return augmentNativePortalElement({}, tagName, 1, 1);
    }

    function installNativePortalDocumentShim() {
        if (typeof document !== "undefined") {
            return;
        }

        try {
            var nativeDocument = {
                createElement: function (type) {
                    var element = createNativePortalDocumentElement(type);
                    element.ownerDocument = nativeDocument;
                    return element;
                },
                createElementNS: function (namespace, type) {
                    var element = createNativePortalDocumentElement(type);
                    element.namespaceURI = namespace;
                    element.ownerDocument = nativeDocument;
                    return element;
                },
                createTextNode: function (text) {
                    return {
                        nodeType: 3,
                        nodeName: "#text",
                        textContent: String(text),
                        parentNode: null
                    };
                },
                getElementsByTagName: function (tagName) {
                    switch (String(tagName).toLowerCase()) {
                        case "head":
                            return [nativeDocument.head];
                        case "body":
                            return [nativeDocument.body];
                        case "html":
                            return [nativeDocument.documentElement];
                        default:
                            return [];
                    }
                },
                getElementById: function () {
                    return null;
                },
                querySelector: function () {
                    return null;
                },
                querySelectorAll: function () {
                    return [];
                },
                addEventListener: function () { },
                removeEventListener: function () { },
                dispatchEvent: function () { return true; }
            };
            nativeDocument.body = augmentNativePortalElement({}, "body", 1, 1);
            nativeDocument.head = augmentNativePortalElement({}, "head", 1, 1);
            nativeDocument.documentElement = augmentNativePortalElement({}, "html", 1, 1);
            nativeDocument.body.ownerDocument = nativeDocument;
            nativeDocument.head.ownerDocument = nativeDocument;
            nativeDocument.documentElement.ownerDocument = nativeDocument;
            globalThis.document = nativeDocument;
            globalThis.OffscreenCanvas = globalThis.OffscreenCanvas || createNativePortalCanvas;
            globalThis.HTMLCanvasElement = globalThis.HTMLCanvasElement || function HTMLCanvasElement() { };
            if (typeof window !== "undefined") {
                window.document = nativeDocument;
            }
            reportStatus("native-xr-portal:document-shim=1");
        } catch (error) {
            reportStatus("native-xr-portal:document-shim-error:" + String(error && error.message ? error.message : error));
        }
    }

    var base64Characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    var maxNativeImageLoads = 4;
    var activeNativeImageLoads = 0;
    var pendingNativeImageLoads = [];

    function toUint8Array(buffer) {
        if (buffer instanceof Uint8Array) {
            return buffer;
        }
        if (buffer instanceof ArrayBuffer) {
            return new Uint8Array(buffer);
        }
        if (ArrayBuffer.isView(buffer)) {
            return new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength);
        }
        if (typeof buffer === "string") {
            var textBytes = new Uint8Array(buffer.length);
            for (var textIndex = 0; textIndex < buffer.length; textIndex++) {
                textBytes[textIndex] = buffer.charCodeAt(textIndex) & 0xff;
            }
            return textBytes;
        }
        return new Uint8Array(0);
    }

    function bytesToBase64(bytes) {
        var output = "";
        var index = 0;
        for (; index + 2 < bytes.length; index += 3) {
            var triplet = (bytes[index] << 16) | (bytes[index + 1] << 8) | bytes[index + 2];
            output += base64Characters[(triplet >> 18) & 63];
            output += base64Characters[(triplet >> 12) & 63];
            output += base64Characters[(triplet >> 6) & 63];
            output += base64Characters[triplet & 63];
        }
        if (index < bytes.length) {
            var remaining = bytes.length - index;
            var tail = bytes[index] << 16;
            if (remaining === 2) {
                tail |= bytes[index + 1] << 8;
            }
            output += base64Characters[(tail >> 18) & 63];
            output += base64Characters[(tail >> 12) & 63];
            output += remaining === 2 ? base64Characters[(tail >> 6) & 63] : "=";
            output += "=";
        }
        return output;
    }

    function markNativeImageAsBitmap(image) {
        if (image && typeof image.close !== "function") {
            image.close = function () {
                if (typeof image.dispose === "function") {
                    image.dispose();
                }
            };
        }
        return image;
    }

    function releaseNativeImageAfterUpload(image) {
        if (!image || typeof image.close !== "function") {
            return;
        }
        setTimeout(function () {
            try {
                image.close();
            } catch (error) {
                void error;
            }
        }, 0);
    }

    function pumpNativeImageLoadQueue() {
        while (activeNativeImageLoads < maxNativeImageLoads && pendingNativeImageLoads.length) {
            var task = pendingNativeImageLoads.shift();
            activeNativeImageLoads++;
            task(function () {
                activeNativeImageLoads--;
                pumpNativeImageLoadQueue();
            });
        }
    }

    function enqueueNativeImageLoad(task) {
        pendingNativeImageLoads.push(task);
        pumpNativeImageLoadQueue();
    }

    function imageFromNativeImageSource(source) {
        return new Promise(function (resolve, reject) {
            if (typeof _native === "undefined" || !_native.Image) {
                reject(new Error("Native image support is unavailable."));
                return;
            }

            var image = new _native.Image();
            image.onload = function () {
                resolve(markNativeImageAsBitmap(image));
            };
            image.onerror = function (error) {
                reject(error instanceof Error ? error : new Error("Native image decode failed."));
            };
            image.src = source;
        });
    }

    function imageFromDataUrl(dataUrl) {
        return imageFromNativeImageSource(dataUrl);
    }

    function imageFromArrayBuffer(buffer, mimeType) {
        var bytes = toUint8Array(buffer);
        if (!bytes.length) {
            return Promise.reject(new Error("Image buffer is empty."));
        }
        var dataUrl = "data:" + (mimeType || "application/octet-stream") + ";base64," + bytesToBase64(bytes);
        return imageFromDataUrl(dataUrl);
    }

    function installNativeWebGPUImageLoadingShim() {
        if (globalThis.__nativeArPortalImageLoadingShimInstalled) {
            return;
        }
        if (!BABYLON || !BABYLON.Tools || typeof BABYLON.Tools.LoadFile !== "function") {
            return;
        }
        if (typeof _native === "undefined" || !_native.Image) {
            reportStatus("native-xr-portal:image-shim-unavailable");
            return;
        }

        globalThis.__nativeArPortalImageLoadingShimInstalled = true;

        function reportImageError(onError, message, error) {
            reportStatus("native-xr-portal:image-error:" + message + (error && error.message ? ":" + error.message : ""));
            if (onError) {
                onError(message, error);
            }
        }

        function decodeBuffer(buffer, mimeType, onLoad, onError) {
            enqueueNativeImageLoad(function (done) {
                imageFromArrayBuffer(buffer, mimeType).then(function (image) {
                    onLoad(image);
                    releaseNativeImageAfterUpload(image);
                    done();
                }).catch(function (error) {
                    reportImageError(onError, "decode", error);
                    done();
                });
            });
        }

        function loadBlob(blob, onLoad, onError) {
            if (blob && typeof blob.arrayBuffer === "function") {
                blob.arrayBuffer().then(function (buffer) {
                    decodeBuffer(buffer, blob.type, onLoad, onError);
                }).catch(function (error) {
                    reportImageError(onError, "blob", error);
                });
                return true;
            }
            return false;
        }

        function loadImageWithNativeCanvas(source, onLoad, onError, offlineProvider, mimeType) {
            void offlineProvider;

            if (source && typeof source._getNativeImageData === "function") {
                onLoad(markNativeImageAsBitmap(source));
                return source;
            }

            if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
                decodeBuffer(source, mimeType, onLoad, onError);
                return null;
            }

            if (loadBlob(source, onLoad, onError)) {
                return null;
            }

            if (typeof source === "string") {
                enqueueNativeImageLoad(function (done) {
                    imageFromNativeImageSource(source).then(function (image) {
                        onLoad(image);
                        releaseNativeImageAfterUpload(image);
                        done();
                    }).catch(function (error) {
                        reportImageError(onError, source, error);
                        done();
                    });
                });
                return null;
            }

            reportImageError(onError, "unsupported-source", source);
            return null;
        }

        globalThis.createImageBitmap = function (source) {
            if (source && typeof source._getNativeImageData === "function") {
                return Promise.resolve(markNativeImageAsBitmap(source));
            }
            if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
                return imageFromArrayBuffer(source);
            }
            if (loadBlob(source, function () { }, function () { })) {
                return source.arrayBuffer().then(function (buffer) {
                    return imageFromArrayBuffer(buffer, source.type);
                });
            }
            return Promise.reject(new Error("Unsupported createImageBitmap source for native portal."));
        };

        BABYLON.Tools.LoadImage = loadImageWithNativeCanvas;
        if (BABYLON.FileTools) {
            BABYLON.FileTools.LoadImage = loadImageWithNativeCanvas;
        }
        if (BABYLON.Engine) {
            BABYLON.Engine._FileToolsLoadImage = loadImageWithNativeCanvas;
        }
        if (BABYLON.ThinEngine) {
            BABYLON.ThinEngine._FileToolsLoadImage = loadImageWithNativeCanvas;
        }
        if (BABYLON.AbstractEngine) {
            BABYLON.AbstractEngine._FileToolsLoadImage = loadImageWithNativeCanvas;
        }

        reportStatus("native-xr-portal:image-shim=1");
    }

    function installNativePortalAssetUrlShim() {
        if (!BABYLON || !BABYLON.Tools || globalThis.__nativeArPortalAssetUrlShimInstalled) {
            return;
        }

        globalThis.__nativeArPortalAssetUrlShimInstalled = true;
        var originalGetAssetUrl = BABYLON.Tools.GetAssetUrl;
        BABYLON.Tools.GetAssetUrl = function (url) {
            if (String(url || "") === "https://assets.babylonjs.com/core/blue_noise/blue_noise_rgb.png") {
                return ASSET_ROOT_URL + BLUE_NOISE_FILE;
            }
            return originalGetAssetUrl ? originalGetAssetUrl.call(BABYLON.Tools, url) : url;
        };
        reportStatus("native-xr-portal:asset-url-shim=1");
    }

    function logTextureReadiness(scene, totalTextureCount, label) {
        var readyTextures = 0;
        var pendingSamples = [];
        for (var textureIndex = 0; textureIndex < (scene.textures || []).length; textureIndex++) {
            var texture = scene.textures[textureIndex];
            try {
                if (!texture || typeof texture.isReady !== "function" || texture.isReady()) {
                    readyTextures++;
                } else if (pendingSamples.length < 3) {
                    pendingSamples.push(compactResourceName(texture.url || texture.name || "unnamed"));
                }
            } catch (error) {
                if (pendingSamples.length < 3) {
                    pendingSamples.push("error:" + String(error && error.message ? error.message : error));
                }
            }
        }
        reportStatus("native-xr-portal:textures-ready:" + label + "=" + String(readyTextures) + "/" + String(totalTextureCount) +
            (pendingSamples.length ? ":pending=" + pendingSamples.join("|") : ""));
    }

    function materialNameMatches(material, expression) {
        return expression.test(String(material && material.name ? material.name : "").toLowerCase());
    }

    function isPortalBackdropMaterial(material) {
        return materialNameMatches(material, /pano|fond|mountain|sky|ciel|background|welcome/);
    }

    function applyRealtimeMaterialOverrides(scene) {
        if (globalThis.__nativeArPortalUseRealtimeLighting === false) {
            return;
        }

        var pbrCount = 0;
        var emissiveCount = 0;
        var sharedAmbientTextureCount = 0;
        var environmentReflectionCount = 0;
        for (var materialIndex = 0; materialIndex < scene.materials.length; materialIndex++) {
            var material = scene.materials[materialIndex];
            if (!material) {
                continue;
            }

            if ("unlit" in material) {
                material.unlit = false;
            }
            if ("disableLighting" in material) {
                material.disableLighting = false;
            }
            if ("environmentIntensity" in material) {
                material.environmentIntensity = 1.08;
            }
            if ("directIntensity" in material) {
                material.directIntensity = 0.65;
            }

            if (isPortalBackdropMaterial(material)) {
                if ("unlit" in material) {
                    material.unlit = true;
                }
                if ("disableLighting" in material) {
                    material.disableLighting = true;
                }
                if ("emissiveTexture" in material && !material.emissiveTexture && material.albedoTexture) {
                    material.emissiveTexture = material.albedoTexture;
                }
                if ("emissiveColor" in material) {
                    material.emissiveColor = new BABYLON.Color3(1, 1, 1);
                }
                if ("emissiveIntensity" in material) {
                    material.emissiveIntensity = 1.0;
                }
                if ("metallic" in material) {
                    material.metallic = 0;
                }
                if ("roughness" in material) {
                    material.roughness = 1;
                }
                emissiveCount++;
                continue;
            }

            if ("metallic" in material && "roughness" in material) {
                pbrCount++;
                if (materialNameMatches(material, /metal|carrosserie|chrome|wheel|wheels|reactor|plate|vents|delorean/)) {
                    material.metallic = materialNameMatches(material, /seat|volant|convecteur/) ? 0.25 : 0.8;
                    material.roughness = materialNameMatches(material, /carrosserie|chrome|plate/) ? 0.2 : 0.34;
                } else if (materialNameMatches(material, /cable|grid/)) {
                    material.metallic = 0.35;
                    material.roughness = 0.36;
                } else if (material.roughness === undefined || material.roughness > 0.85) {
                    material.roughness = 0.72;
                }

                if (materialNameMatches(material, /glass|vitre|window/)) {
                    material.metallic = 0;
                    material.roughness = 0.08;
                    material.alpha = Math.min(Number(material.alpha || 1), materialNameMatches(material, /delorean/) ? 0.42 : 0.62);
                }
            }

            if (globalThis.__nativeArPortalUseEnvironmentReflectionOverride === true && scene.environmentTexture && isPortalReflectiveMaterial(material)) {
                if ("reflectionTexture" in material) {
                    material.reflectionTexture = scene.environmentTexture;
                }
                if ("environmentIntensity" in material) {
                    material.environmentIntensity = materialNameMatches(material, /carrosserie|chrome|plate/) ? 1.42 : 1.22;
                }
                environmentReflectionCount++;
            }

            if (materialNameMatches(material, /neon|feux|flare|convecteur_light/)) {
                if ("emissiveTexture" in material && !material.emissiveTexture && material.albedoTexture) {
                    material.emissiveTexture = material.albedoTexture;
                }
                if ("emissiveColor" in material) {
                    material.emissiveColor = new BABYLON.Color3(0.82, 0.82, 0.82);
                }
                if ("emissiveIntensity" in material) {
                    material.emissiveIntensity = 1.15;
                }
                emissiveCount++;
            } else if (materialNameMatches(material, /lampe|lamp/)) {
                if ("emissiveTexture" in material && !material.emissiveTexture && material.albedoTexture) {
                    material.emissiveTexture = material.albedoTexture;
                }
                if ("emissiveColor" in material) {
                    material.emissiveColor = new BABYLON.Color3(0.34, 0.34, 0.34);
                }
                if ("emissiveIntensity" in material) {
                    material.emissiveIntensity = 0.35;
                }
                emissiveCount++;
            } else if (material.albedoTexture && "emissiveTexture" in material && "emissiveColor" in material && !materialNameMatches(material, /sky|glass|vitre|window|metal|chrome|carrosserie/)) {
                material.emissiveTexture = material.albedoTexture;
                material.emissiveColor = new BABYLON.Color3(0.16, 0.16, 0.16);
                sharedAmbientTextureCount++;
            }
        }

        reportStatus("native-xr-portal:realtime-materials:pbr=" + String(pbrCount) +
            ":emissive=" + String(emissiveCount) +
            ":sharedAmbient=" + String(sharedAmbientTextureCount) +
            ":envReflections=" + String(environmentReflectionCount));
    }

    function logPortalMaterialParity(scene) {
        var backdropCount = 0;
        var unlitBackdropCount = 0;
        var emissiveBackdropCount = 0;
        var reflectiveCount = 0;
        var environmentReflectionCount = 0;
        var lampCount = 0;
        var emissiveLampCount = 0;
        var issueSamples = [];

        for (var materialIndex = 0; materialIndex < scene.materials.length; materialIndex++) {
            var material = scene.materials[materialIndex];
            if (!material) {
                continue;
            }

            var materialName = compactResourceName(material.name || "unnamed");
            if (isPortalBackdropMaterial(material)) {
                backdropCount++;
                if (material.unlit === true || material.disableLighting === true) {
                    unlitBackdropCount++;
                } else if (issueSamples.length < 4) {
                    issueSamples.push(materialName + ":lit-backdrop");
                }
                if (material.emissiveTexture || material.emissiveColor) {
                    emissiveBackdropCount++;
                } else if (issueSamples.length < 4) {
                    issueSamples.push(materialName + ":no-emissive");
                }
            }

            if (isPortalReflectiveMaterial(material)) {
                reflectiveCount++;
                if (material.reflectionTexture === scene.environmentTexture || (!material.reflectionTexture && scene.environmentTexture)) {
                    environmentReflectionCount++;
                } else if (issueSamples.length < 4) {
                    issueSamples.push(materialName + ":no-env-reflection");
                }
            }

            if (materialNameMatches(material, /lampe|lamp|poteau|post/)) {
                lampCount++;
                if (material.emissiveTexture || material.emissiveColor) {
                    emissiveLampCount++;
                } else if (issueSamples.length < 4) {
                    issueSamples.push(materialName + ":lamp-not-emissive");
                }
            }
        }

        reportStatus("native-xr-portal:material-parity:backdrops=" + String(unlitBackdropCount) + "/" + String(backdropCount) +
            ":backdropEmissive=" + String(emissiveBackdropCount) + "/" + String(backdropCount) +
            ":reflectiveEnv=" + String(environmentReflectionCount) + "/" + String(reflectiveCount) +
            ":lampEmissive=" + String(emissiveLampCount) + "/" + String(lampCount) +
            ":issues=" + String(issueSamples.length));
        if (issueSamples.length) {
            reportStatus("native-xr-portal:material-parity-issues:" + issueSamples.join("|"));
        }
    }

    function includeOnlyPortalMeshes(light, meshes) {
        if (!light || !meshes || !("includedOnlyMeshes" in light)) {
            return;
        }

        light.includedOnlyMeshes = [];
        for (var meshIndex = 0; meshIndex < meshes.length; meshIndex++) {
            var mesh = meshes[meshIndex];
            if (mesh && mesh.getTotalVertices && mesh.getTotalVertices() > 0) {
                light.includedOnlyMeshes.push(mesh);
            }
        }
    }

    function configurePortalRealtimeLights(scene, meshes) {
        if (globalThis.__nativeArPortalUseRealtimeLighting === false) {
            return;
        }

        var importedLightCount = 0;
        for (var lightIndex = scene.lights.length - 1; lightIndex >= 0; lightIndex--) {
            var light = scene.lights[lightIndex];
            if (!light || /^native-xr-portal-/.test(String(light.name || ""))) {
                continue;
            }
            if (globalThis.__nativeArPortalUseImportedPunctualLights === true) {
                importedLightCount++;
                if ("intensity" in light) {
                    light.intensity = Math.max(Number(light.intensity || 0), 0.55);
                }
                includeOnlyPortalMeshes(light, meshes);
            } else if (typeof light.dispose === "function") {
                light.dispose();
            }
        }

        if (globalThis.__nativeArPortalUseDirectionalFillLight === true) {
            var key = new BABYLON.DirectionalLight("native-xr-portal-key-light", new BABYLON.Vector3(0.35, -0.82, 0.42), scene);
            key.intensity = 0.55;
            key.diffuse = new BABYLON.Color3(1.0, 0.96, 0.9);
            key.specular = new BABYLON.Color3(0.55, 0.58, 0.62);
            includeOnlyPortalMeshes(key, meshes);
        }

        var fill = new BABYLON.HemisphericLight("native-xr-portal-fill-light", new BABYLON.Vector3(-0.25, 1.0, 0.35), scene);
        fill.intensity = 0.48;
        fill.diffuse = new BABYLON.Color3(0.78, 0.88, 1.0);
        fill.groundColor = new BABYLON.Color3(0.28, 0.22, 0.18);
        includeOnlyPortalMeshes(fill, meshes);

        reportStatus("native-xr-portal:lights=runtime-fill:imported=" + String(importedLightCount));
    }

    function isPortalReflectiveMaterial(material) {
        return materialNameMatches(material, /carrosserie|chrome|delorean|reactor|plate|wheel|wheels|metal_01|convecteur/);
    }

    function isPortalProbeRenderMesh(mesh) {
        if (!mesh || !mesh.getTotalVertices || mesh.getTotalVertices() <= 0 || !mesh.isEnabled()) {
            return false;
        }
        var name = String(mesh.name || "").toLowerCase();
        if (/d1_|delorean/.test(name)) {
            return false;
        }
        return true;
    }

    function computePortalReflectionProbePosition(meshes, fallback) {
        var center = new BABYLON.Vector3();
        var count = 0;
        for (var meshIndex = 0; meshIndex < meshes.length; meshIndex++) {
            var mesh = meshes[meshIndex];
            if (!mesh || !/d1_|delorean/i.test(String(mesh.name || "")) || !mesh.getBoundingInfo) {
                continue;
            }
            mesh.computeWorldMatrix(true);
            center.addInPlace(mesh.getBoundingInfo().boundingBox.centerWorld);
            count++;
        }

        if (count > 0) {
            center.scaleInPlace(1 / count);
            center.y += 0.45;
            return center;
        }

        return fallback ? fallback.clone() : BABYLON.Vector3.Zero();
    }

    function createSceneReflectionProbeForPortal(scene, meshes, fallbackPosition) {
        if (globalThis.__nativeArPortalUseSceneReflectionProbe === false || !BABYLON.ReflectionProbe) {
            reportStatus("native-xr-portal:reflection-probe=disabled");
            return null;
        }

        try {
            var size = Math.max(64, Math.min(256, Number(globalThis.__nativeArPortalReflectionProbeSize || 128)));
            var probe = new BABYLON.ReflectionProbe("native-xr-portal-scene-probe", size, scene, true);
            probe.samples = 1;
            probe.refreshRate = BABYLON.RenderTargetTexture && BABYLON.RenderTargetTexture.REFRESHRATE_RENDER_ONCE !== undefined ? BABYLON.RenderTargetTexture.REFRESHRATE_RENDER_ONCE : 1;
            probe.position.copyFrom(computePortalReflectionProbePosition(meshes, fallbackPosition));

            var renderList = [];
            for (var meshIndex = 0; meshIndex < meshes.length; meshIndex++) {
                if (isPortalProbeRenderMesh(meshes[meshIndex])) {
                    renderList.push(meshes[meshIndex]);
                }
            }
            probe.renderList = renderList;

            var materialCount = 0;
            for (var materialIndex = 0; materialIndex < scene.materials.length; materialIndex++) {
                var material = scene.materials[materialIndex];
                if (material && isPortalReflectiveMaterial(material) && "reflectionTexture" in material) {
                    material.reflectionTexture = probe.cubeTexture;
                    if ("environmentIntensity" in material) {
                        material.environmentIntensity = 1.15;
                    }
                    materialCount++;
                }
            }

            if (probe.cubeTexture && typeof probe.cubeTexture.resetRefreshCounter === "function") {
                probe.cubeTexture.resetRefreshCounter();
            }
            reportStatus("native-xr-portal:reflection-probe=scene:size=" + String(size) +
                ":meshes=" + String(renderList.length) +
                ":materials=" + String(materialCount) +
                ":position=" + formatVector3(probe.position));
            return probe;
        } catch (error) {
            reportStatus("native-xr-portal:reflection-probe-error:" + String(error && error.message ? error.message : error));
            return null;
        }
    }

    function createPortalEnvironmentTexture(scene) {
        if (globalThis.__nativeArPortalUseRealtimeLighting === false || !BABYLON.CubeTexture) {
            return null;
        }

        try {
            var environmentUrl = ASSET_ROOT_URL + ENVIRONMENT_CUBE_FILE;
            var environmentTexture = new BABYLON.CubeTexture(
                environmentUrl,
                scene,
                {
                    // Passing an explicit empty file list keeps Babylon from expanding
                    // a single compressed cubemap into six cascade URLs.
                    files: [],
                    noMipmap: false,
                    forcedExtension: ".ktx",
                    createPolynomials: true,
                    lodScale: 0.8,
                    lodOffset: 0,
                    useSRGBBuffer: true,
                    onLoad: function () {
                        reportStatus("native-xr-portal:environment-ready");
                    },
                    onError: function (message, exception) {
                        reportStatus("native-xr-portal:environment-error:" + String(message || "") +
                            (exception && exception.message ? ":" + exception.message : ""));
                    }
                });
            environmentTexture.name = "native-xr-portal-environment";
            environmentTexture.coordinatesMode = BABYLON.Texture.CUBIC_MODE;
            environmentTexture.level = 1.0;
            scene.environmentTexture = environmentTexture;
            scene.environmentIntensity = 1.0;
            reportStatus("native-xr-portal:environment=ktx-cube");
            return environmentTexture;
        } catch (error) {
            reportStatus("native-xr-portal:environment-error:" + String(error && error.message ? error.message : error));
            return null;
        }
    }

    function enablePortalSSAO2(scene, camera) {
        if (globalThis.__nativeArPortalUseSSAO2 === false || !BABYLON.SSAO2RenderingPipeline) {
            reportStatus("native-xr-portal:ssao2=disabled");
            return null;
        }

        try {
            var ratio = {
                ssaoRatio: Math.max(0.25, Math.min(1, Number(globalThis.__nativeArPortalSSAOScale || 0.45))),
                blurRatio: Math.max(0.25, Math.min(1, Number(globalThis.__nativeArPortalSSAOBlurScale || 0.5)))
            };
            var geometryBufferRenderer = scene.geometryBufferRenderer || (typeof scene.enableGeometryBufferRenderer === "function" ? scene.enableGeometryBufferRenderer() : null);
            if (!geometryBufferRenderer) {
                reportStatus("native-xr-portal:ssao2=disabled:no-geometry-buffer");
                return null;
            }
            var pipeline = new BABYLON.SSAO2RenderingPipeline("native-xr-portal-ssao2", scene, ratio, [camera], geometryBufferRenderer);
            pipeline.samples = Math.max(4, Math.min(16, Number(globalThis.__nativeArPortalSSAOSamples || 8)));
            pipeline.textureSamples = 1;
            pipeline.radius = Math.max(0.05, Number(globalThis.__nativeArPortalSSAORadius || 0.85));
            pipeline.totalStrength = Math.max(0, Number(globalThis.__nativeArPortalSSAOStrength || 0.42));
            pipeline.base = Math.max(0, Math.min(1, Number(globalThis.__nativeArPortalSSAOBase || 0.76)));
            pipeline.maxZ = 48;
            pipeline.expensiveBlur = false;
            pipeline.bilateralSamples = 8;
            if ("epsilon" in pipeline) {
                pipeline.epsilon = 0.04;
            }
            if ("bilateralSoften" in pipeline) {
                pipeline.bilateralSoften = 0.45;
            }
            if ("bilateralTolerance" in pipeline) {
                pipeline.bilateralTolerance = 0.25;
            }
            reportStatus("native-xr-portal:ssao2=enabled:ratio=" + ratio.ssaoRatio.toFixed(2) +
                ":blur=" + ratio.blurRatio.toFixed(2) +
                ":samples=" + String(pipeline.samples) +
                ":radius=" + pipeline.radius.toFixed(2) +
                ":strength=" + pipeline.totalStrength.toFixed(2) +
                ":base=" + pipeline.base.toFixed(2));
            return pipeline;
        } catch (error) {
            reportStatus("native-xr-portal:ssao2-error:" + String(error && error.message ? error.message : error));
            return null;
        }
    }

    function selectIblShadowCasters(meshes) {
        var maxCasters = Math.max(0, Number(globalThis.__nativeArPortalIblShadowMaxCasters || 96));
        var candidates = [];
        for (var meshIndex = 0; meshIndex < meshes.length; meshIndex++) {
            var mesh = meshes[meshIndex];
            if (!mesh || !mesh.getTotalVertices || mesh.getTotalVertices() <= 0 || !mesh.isEnabled()) {
                continue;
            }
            if (materialNameMatches(mesh.material, /sky|glass|vitre|neon|light|lampe|lamp|flare/)) {
                continue;
            }
            var vertexCount = Number(mesh.getTotalVertices() || 0);
            candidates.push({ mesh: mesh, score: vertexCount });
        }
        candidates.sort(function (a, b) {
            return b.score - a.score;
        });
        var selected = [];
        for (var index = 0; index < candidates.length && selected.length < maxCasters; index++) {
            selected.push(candidates[index].mesh);
        }
        return selected;
    }

    function enablePortalIblShadows(scene, camera, meshes) {
        if (globalThis.__nativeArPortalUseIblShadows === false || !BABYLON.IblShadowsRenderPipeline || !scene.environmentTexture) {
            reportStatus("native-xr-portal:ibl-shadows=disabled");
            return null;
        }

        try {
            var pipeline = new BABYLON.IblShadowsRenderPipeline("native-xr-portal-ibl-shadows", scene, {
                resolutionExp: Math.max(3, Math.min(6, Number(globalThis.__nativeArPortalIblShadowResolutionExp || 5))),
                shadowRenderSizeFactor: Math.max(0.2, Math.min(1, Number(globalThis.__nativeArPortalIblShadowScale || 0.35))),
                shadowOpacity: 0.28,
                voxelShadowOpacity: 0.32,
                ssShadowsEnabled: globalThis.__nativeArPortalIblShadowScreenSpace === true,
                ssShadowSampleCount: 4,
                ssShadowStride: 12,
                ssShadowDistanceScale: 0.95,
                ssShadowThicknessScale: 1.2,
                shadowRemanence: 0.88,
                sampleDirections: 1,
                triPlanarVoxelization: false
            }, [camera]);
            pipeline.coloredShadows = false;
            pipeline.addShadowReceivingMaterial();
            var casters = selectIblShadowCasters(meshes);
            pipeline.addShadowCastingMesh(casters);
            pipeline.toggleShadow(false);
            reportStatus("native-xr-portal:ibl-shadows=prepared:casters=" + String(casters.length));
            return pipeline;
        } catch (error) {
            reportStatus("native-xr-portal:ibl-shadows-error:" + String(error && error.message ? error.message : error));
            return null;
        }
    }

    function updatePortalIblShadowsAfterPlacement(pipeline) {
        if (!pipeline) {
            return;
        }

        try {
            pipeline.toggleShadow(true);
            pipeline.updateSceneBounds();
            pipeline.updateVoxelization();
            reportStatus("native-xr-portal:ibl-shadows=voxelizing");
        } catch (error) {
            reportStatus("native-xr-portal:ibl-shadows-update-error:" + String(error && error.message ? error.message : error));
        }
    }

    async function createPortalScene(engine) {
        reportStatus("native-xr-portal:local-createScene");
        ensureBrowserLocationForLoaders();
        installNativePortalDocumentShim();
        installNativeWebGPUImageLoadingShim();
        installNativePortalAssetUrlShim();
        engine.enableOfflineSupport = false;

        var canvas = typeof engine.getRenderingCanvas === "function" ? engine.getRenderingCanvas() : globalThis.canvas;
        var scene = new BABYLON.Scene(engine);
        scene.clearColor = new BABYLON.Color4(0, 0, 0, 0);
        if (globalThis.__nativeArPortalUseSceneStaticFastPath === true) {
            enableSceneStaticFastPath(scene);
        }

        var camera = new BABYLON.FreeCamera("camera1", new BABYLON.Vector3(0, 1, -5), scene);
        camera.setTarget(BABYLON.Vector3.Zero());
        if (canvas) {
            camera.attachControl(canvas, true);
        }

        var arAvailable = await BABYLON.WebXRSessionManager.IsSessionSupportedAsync("immersive-ar");
        var rectangle = { isVisible: false };
        if (!arAvailable) {
            reportStatus("native-xr-portal:ar-not-available");
            return scene;
        }

        var xr = await scene.createDefaultXRExperienceAsync({
            disableDefaultUI: true,
            disableTeleportation: true,
            optionalFeatures: true
        });

        var fm = xr.baseExperience.featuresManager;
        var nativeHitTestEntityTypes = globalThis.__nativeArPortalAllowEstimatedPointPlacement === true ? ["plane", "point"] : ["plane"];
        var xrTest = fm.enableFeature(BABYLON.WebXRHitTest.Name, "latest", {
            entityTypes: nativeHitTestEntityTypes
        });
        var xrCamera = xr.baseExperience.camera;
        createPortalEnvironmentTexture(scene);
        var nativePlaneDetector = null;
        var nativeAnchorSystem = null;
        var nativePortalAnchor = null;
        var nativePlaneUpdateLogTime = 0;
        var nativeFloorPlanes = {};
        var nativeFloorPlaneCount = 0;
        var nativeFloorHitWaitLogTime = 0;
        var nativeHitPositionScratch = new BABYLON.Vector3();

        function getPlaneSemanticType(plane) {
            try {
                var sceneObject = plane && plane.xrPlane ? plane.xrPlane.parentSceneObject : null;
                return String(sceneObject && sceneObject.type ? sceneObject.type : "none");
            } catch (error) {
                return "none";
            }
        }

        function trackSemanticFloorPlane(plane) {
            if (!plane || plane.id === undefined || plane.id === null) {
                return;
            }

            var key = String(plane.id);
            var isSemanticFloor = getPlaneSemanticType(plane) === "floor";
            var hasUsablePolygon = !!plane.transformationMatrix && !!plane.polygonDefinition && plane.polygonDefinition.length >= 3;
            if (isSemanticFloor && hasUsablePolygon) {
                if (!nativeFloorPlanes[key]) {
                    nativeFloorPlaneCount++;
                }
                nativeFloorPlanes[key] = plane;
            } else if (nativeFloorPlanes[key]) {
                delete nativeFloorPlanes[key];
                nativeFloorPlaneCount = Math.max(0, nativeFloorPlaneCount - 1);
            }
        }

        function untrackSemanticFloorPlane(plane) {
            if (!plane || plane.id === undefined || plane.id === null) {
                return;
            }

            var key = String(plane.id);
            if (nativeFloorPlanes[key]) {
                delete nativeFloorPlanes[key];
                nativeFloorPlaneCount = Math.max(0, nativeFloorPlaneCount - 1);
            }
        }

        function positionFromHitResult(result, out) {
            if (result && result.position) {
                out.copyFrom(result.position);
                return out;
            }

            if (result && result.transformationMatrix) {
                result.transformationMatrix.decompose(undefined, undefined, out);
                return out;
            }

            out.set(0, 0, 0);
            return out;
        }

        function findSemanticFloorForPosition(position) {
            var bestFloor = null;
            var bestScore = Number.POSITIVE_INFINITY;
            var margin = 0.18;
            var infinitePlaneHorizontalTolerance = 1.5;
            var verticalTolerance = 0.12;

            for (var key in nativeFloorPlanes) {
                if (!Object.prototype.hasOwnProperty.call(nativeFloorPlanes, key)) {
                    continue;
                }

                var plane = nativeFloorPlanes[key];
                var polygon = plane && plane.polygonDefinition;
                var matrix = plane && plane.transformationMatrix;
                if (!polygon || !matrix || polygon.length < 3) {
                    continue;
                }

                var minX = Number.POSITIVE_INFINITY;
                var maxX = Number.NEGATIVE_INFINITY;
                var minZ = Number.POSITIVE_INFINITY;
                var maxZ = Number.NEGATIVE_INFINITY;
                var averageY = 0;
                for (var pointIndex = 0; pointIndex < polygon.length; pointIndex++) {
                    var worldPoint = BABYLON.Vector3.TransformCoordinates(polygon[pointIndex], matrix);
                    minX = Math.min(minX, worldPoint.x);
                    maxX = Math.max(maxX, worldPoint.x);
                    minZ = Math.min(minZ, worldPoint.z);
                    maxZ = Math.max(maxZ, worldPoint.z);
                    averageY += worldPoint.y;
                }

                averageY /= polygon.length;
                var verticalDistance = Math.abs(Number(position.y || 0) - averageY);
                var horizontalDistance = 0;
                if (position.x < minX - margin) {
                    horizontalDistance = Math.max(horizontalDistance, (minX - margin) - position.x);
                } else if (position.x > maxX + margin) {
                    horizontalDistance = Math.max(horizontalDistance, position.x - (maxX + margin));
                }
                if (position.z < minZ - margin) {
                    horizontalDistance = Math.max(horizontalDistance, (minZ - margin) - position.z);
                } else if (position.z > maxZ + margin) {
                    horizontalDistance = Math.max(horizontalDistance, position.z - (maxZ + margin));
                }

                var score = verticalDistance + (horizontalDistance * 0.01);
                if (verticalDistance <= verticalTolerance &&
                    horizontalDistance <= infinitePlaneHorizontalTolerance &&
                    score < bestScore) {
                    bestFloor = {
                        id: key,
                        verticalDistance: verticalDistance,
                        horizontalDistance: horizontalDistance
                    };
                    bestScore = score;
                }
            }

            return bestFloor;
        }

        function selectSemanticFloorHitResult(results) {
            if (!results || !results.length || nativeFloorPlaneCount === 0) {
                return null;
            }

            for (var resultIndex = 0; resultIndex < results.length; resultIndex++) {
                var result = results[resultIndex];
                var position = positionFromHitResult(result, nativeHitPositionScratch);
                var floor = findSemanticFloorForPosition(position);
                if (floor) {
                    result.__nativeFloorPlaneId = floor.id;
                    result.__nativeFloorPlaneDistance = floor.verticalDistance;
                    result.__nativeFloorHorizontalDistance = floor.horizontalDistance;
                    return result;
                }
            }

            return null;
        }

        function logFloorHitWaiting(results) {
            var now = performance.now();
            if (now - nativeFloorHitWaitLogTime < 1000) {
                return;
            }

            nativeFloorHitWaitLogTime = now;
            reportStatus("native-xr-portal:floor-hit-waiting:floorPlanes=" + String(nativeFloorPlaneCount) +
                ":results=" + String(results ? results.length : 0));
        }

        function requestNativePlaneDetection(reason) {
            try {
                var session = xr.baseExperience.sessionManager.session;
                if (session && typeof session.updateWorldTrackingState === "function") {
                    session.updateWorldTrackingState({
                        planeDetectionState: {
                            enabled: true
                        }
                    });
                    reportStatus("native-xr-portal:plane-detection-request:" + String(reason || "manual"));
                }
            } catch (error) {
                reportStatus("native-xr-portal:plane-detection-request-error:" + String(error && error.message ? error.message : error));
            }
        }

        if (globalThis.__nativeArPortalEnablePlaneDetection !== false && BABYLON.WebXRPlaneDetector) {
            try {
                nativePlaneDetector = fm.enableFeature(BABYLON.WebXRPlaneDetector.Name, "latest");
                reportStatus("native-xr-portal:plane-detection=enabled");
                nativePlaneDetector.onPlaneAddedObservable.add(function (plane) {
                    trackSemanticFloorPlane(plane);
                    reportStatus("native-xr-portal:plane-added:id=" + String(plane && plane.id !== undefined ? plane.id : "none") +
                        ":type=" + getPlaneSemanticType(plane) +
                        ":points=" + String(plane && plane.polygonDefinition ? plane.polygonDefinition.length : 0) +
                        ":floorPlanes=" + String(nativeFloorPlaneCount));
                });
                nativePlaneDetector.onPlaneUpdatedObservable.add(function (plane) {
                    trackSemanticFloorPlane(plane);
                    var now = performance.now();
                    if (now - nativePlaneUpdateLogTime < 1000) {
                        return;
                    }
                    nativePlaneUpdateLogTime = now;
                    reportStatus("native-xr-portal:plane-updated:id=" + String(plane && plane.id !== undefined ? plane.id : "none") +
                        ":type=" + getPlaneSemanticType(plane) +
                        ":points=" + String(plane && plane.polygonDefinition ? plane.polygonDefinition.length : 0) +
                        ":floorPlanes=" + String(nativeFloorPlaneCount));
                });
                nativePlaneDetector.onPlaneRemovedObservable.add(function (plane) {
                    untrackSemanticFloorPlane(plane);
                    reportStatus("native-xr-portal:plane-removed:id=" + String(plane && plane.id !== undefined ? plane.id : "none") +
                        ":floorPlanes=" + String(nativeFloorPlaneCount));
                });
            } catch (error) {
                reportStatus("native-xr-portal:plane-detection-error:" + String(error && error.message ? error.message : error));
            }
        }
        if (globalThis.__nativeArPortalUseAnchors !== false && BABYLON.WebXRAnchorSystem) {
            try {
                nativeAnchorSystem = fm.enableFeature(BABYLON.WebXRAnchorSystem.Name, "latest", {
                    doNotRemoveAnchorsOnSessionEnded: false,
                    clearAnchorsOnSessionInit: true
                });
                reportStatus("native-xr-portal:anchors=enabled");
            } catch (error) {
                reportStatus("native-xr-portal:anchors-error:" + String(error && error.message ? error.message : error));
            }
        }

        var gl = new BABYLON.GlowLayer("glow", scene, {
            mainTextureSamples: 1,
            mainTextureFixedSize: 256,
            blurKernelSize: 64
        });
        gl.neutralColor = new BABYLON.Color4(0, 0, 0, 0);
        gl.intensity = 2.2;
        gl.customEmissiveColorSelector = function (_mesh, _subMesh, _material, result) {
            result.set(0.04, 0.65, 1.0, 1.0);
        };

        function registerGlowMesh(mesh) {
            gl.addIncludedOnlyMesh(mesh);
            if (typeof gl.setEffectIntensity === "function") {
                gl.setEffectIntensity(mesh, 2.35);
            }
        }

        var neonColor = new BABYLON.Color3(0.04, 0.55, 1.0);
        var neonMaterial = new BABYLON.StandardMaterial("neonMaterial", scene);
        neonMaterial.disableLighting = true;
        neonMaterial.diffuseColor = neonColor;
        neonMaterial.emissiveColor = neonColor;
        neonMaterial.specularColor = BABYLON.Color3.Black();

        var marker = BABYLON.MeshBuilder.CreateTorus("marker", { diameter: 0.15, thickness: 0.05, tessellation: 32 }, scene);
        marker.isVisible = false;
        marker.isPickable = false;
        marker.rotationQuaternion = new BABYLON.Quaternion();
        registerGlowMesh(marker);
        marker.material = neonMaterial;

        var hitTest;
        var markerSettlePosition = new BABYLON.Vector3();
        var markerSettleRotation = new BABYLON.Quaternion();
        var markerSettleFrames = 0;
        var markerSettleHasCandidate = false;
        var markerSettleLogged = false;
        var markerSettleFloorPlaneId = null;
        var nativeHitTestLogged = false;
        var nativeNearHitSkipLogged = false;
        xrTest.onHitTestResultObservable.add(function (results) {
            if (portalAppearded) {
                applyMarkerVisibility(false);
                return;
            }

            var floorHit = selectSemanticFloorHitResult(results);
            if (floorHit) {
                applyMarkerVisibility(true);
                updateMarkerPlacementCandidate(floorHit);
                if (!nativeHitTestLogged) {
                    nativeHitTestLogged = true;
                    reportStatus("native-xr-portal:hit-test=" + String(results.length) +
                        ":floorPlane=" + String(floorHit.__nativeFloorPlaneId) +
                        ":floorDistance=" + Number(floorHit.__nativeFloorPlaneDistance || 0).toFixed(3) +
                        ":horizontalDistance=" + Number(floorHit.__nativeFloorHorizontalDistance || 0).toFixed(3));
                }
                if (isMarkerPlacementSettled()) {
                    nativeAutoPlacePortal("settled-hit-test");
                }
            } else {
                applyMarkerVisibility(false);
                markerSettleHasCandidate = false;
                markerSettleFrames = 0;
                markerSettleFloorPlaneId = null;
                hitTest = undefined;
                logFloorHitWaiting(results);
            }
        });

        var rootPortal = new BABYLON.TransformNode("rootPortal", scene);
        rootPortal.rotationQuaternion = new BABYLON.Quaternion();
        var rootOccluder = new BABYLON.TransformNode("rootOccluder", scene);
        rootOccluder.rotationQuaternion = new BABYLON.Quaternion();
        var rootScene = new BABYLON.TransformNode("rootScene", scene);
        rootScene.rotationQuaternion = new BABYLON.Quaternion();
        var rootPilar = new BABYLON.TransformNode("rootPilar", scene);
        rootPilar.rotationQuaternion = new BABYLON.Quaternion();
        rootOccluder.parent = rootPortal;
        rootScene.parent = rootPortal;
        rootPilar.parent = rootPortal;

        var oclVisibility = 0.001;
        var ground = BABYLON.MeshBuilder.CreateBox("ground", { width: 500, depth: 500, height: 0.001 }, scene);
        var hole = BABYLON.MeshBuilder.CreateBox("hole", { size: 2, width: 1, height: 0.01 }, scene);

        var groundCSG = BABYLON.CSG.FromMesh(ground);
        var holeCSG = BABYLON.CSG.FromMesh(hole);
        var booleanCSG = groundCSG.subtract(holeCSG);
        var booleanRCSG = holeCSG.subtract(groundCSG);
        var occluder = booleanCSG.toMesh("occluder", null, scene);
        var occluderR = booleanRCSG.toMesh("occluderR", null, scene);
        var occluderFloor = BABYLON.MeshBuilder.CreateBox("ground", { width: 7, depth: 7, height: 0.001 }, scene);
        var occluderTop = BABYLON.MeshBuilder.CreateBox("occluderTop", { width: 7, depth: 7, height: 0.001 }, scene);
        var occluderRight = BABYLON.MeshBuilder.CreateBox("occluderRight", { width: 7, depth: 7, height: 0.001 }, scene);
        var occluderLeft = BABYLON.MeshBuilder.CreateBox("occluderLeft", { width: 7, depth: 7, height: 0.001 }, scene);
        var occluderback = BABYLON.MeshBuilder.CreateBox("occluderback", { width: 7, depth: 7, height: 0.001 }, scene);
        var occluderMaterial = new BABYLON.StandardMaterial("om", scene);
        occluderMaterial.disableLighting = true;
        occluderMaterial.forceDepthWrite = true;
        occluderMaterial.disableColorWrite = true;
        occluder.material = occluderMaterial;
        occluderR.material = occluderMaterial;
        occluderFloor.material = occluderMaterial;
        occluderTop.material = occluderMaterial;
        occluderRight.material = occluderMaterial;
        occluderLeft.material = occluderMaterial;
        occluderback.material = occluderMaterial;
        ground.dispose();
        hole.dispose();
        prepareStaticMeshForFastPath(occluder);
        prepareStaticMeshForFastPath(occluderR);
        prepareStaticMeshForFastPath(occluderFloor);
        prepareStaticMeshForFastPath(occluderTop);
        prepareStaticMeshForFastPath(occluderRight);
        prepareStaticMeshForFastPath(occluderLeft);
        prepareStaticMeshForFastPath(occluderback);

        var virtualWorldResult = await BABYLON.SceneLoader.ImportMeshAsync("", ASSET_ROOT_URL, HILL_VALLEY_FILE, scene);
        scene.clearColor = new BABYLON.Color4(0, 0, 0, 0);
        configurePortalRealtimeLights(scene, virtualWorldResult.meshes);
        applyRealtimeMaterialOverrides(scene);
        logPortalMaterialParity(scene);
        var nativePortalIblShadows = enablePortalIblShadows(scene, xrCamera, virtualWorldResult.meshes);
        var nativePortalSSAO2 = enablePortalSSAO2(scene, xrCamera);
        void nativePortalSSAO2;

        var nativeHillDebugMaterial = new BABYLON.StandardMaterial("nativeHillDebugMaterial", scene);
        nativeHillDebugMaterial.disableLighting = true;
        nativeHillDebugMaterial.diffuseColor = new BABYLON.Color3(0.1, 1, 0.25);
        nativeHillDebugMaterial.emissiveColor = new BABYLON.Color3(0.1, 1, 0.25);

        var importedNodes = [];
        for (var importedMeshIndex = 0; importedMeshIndex < virtualWorldResult.meshes.length; importedMeshIndex++) {
            importedNodes.push(virtualWorldResult.meshes[importedMeshIndex]);
        }
        if (virtualWorldResult.transformNodes) {
            for (var importedTransformIndex = 0; importedTransformIndex < virtualWorldResult.transformNodes.length; importedTransformIndex++) {
                importedNodes.push(virtualWorldResult.transformNodes[importedTransformIndex]);
            }
        }

        function isImportedNode(node) {
            for (var importedNodeIndex = 0; importedNodeIndex < importedNodes.length; importedNodeIndex++) {
                if (importedNodes[importedNodeIndex] === node) {
                    return true;
                }
            }
            return false;
        }

        for (var childIndex = 0; childIndex < virtualWorldResult.meshes.length; childIndex++) {
            var child = virtualWorldResult.meshes[childIndex];
            child.renderingGroupId = 1;
            if (globalThis.__nativeArPortalDebugBrightHillValley === true && child.getTotalVertices && child.getTotalVertices() > 0) {
                child.material = nativeHillDebugMaterial;
            }
            prepareStaticMeshForFastPath(child);
            if (!isImportedNode(child.parent)) {
                child.parent = rootScene;
            }
        }
        for (var nodeIndex = 0; nodeIndex < importedNodes.length; nodeIndex++) {
            var importedNode = importedNodes[nodeIndex];
            if (importedNode && !isImportedNode(importedNode.parent)) {
                importedNode.parent = rootScene;
            }
        }

        var nativeTextureCount = scene.textures ? scene.textures.length : 0;
        reportStatus("native-xr-portal:virtual-world:meshes=" + String(virtualWorldResult.meshes.length) +
            ":sceneMaterials=" + String(scene.materials.length) +
            ":textures=" + String(nativeTextureCount));
        setTimeout(function () {
            logTextureReadiness(scene, nativeTextureCount, "4s");
        }, 4000);
        setTimeout(function () {
            logTextureReadiness(scene, nativeTextureCount, "12s");
        }, 12000);

        occluder.renderingGroupId = 0;
        occluderR.renderingGroupId = 0;
        occluderFloor.renderingGroupId = 0;
        occluderTop.renderingGroupId = 0;
        occluderRight.renderingGroupId = 0;
        occluderLeft.renderingGroupId = 0;
        occluderback.renderingGroupId = 0;

        occluder.parent = rootOccluder;
        occluderR.parent = rootOccluder;
        occluderFloor.parent = rootOccluder;
        occluderTop.parent = rootOccluder;
        occluderRight.parent = rootOccluder;
        occluderLeft.parent = rootOccluder;
        occluderback.parent = rootOccluder;

        occluder.isVisible = true;
        occluderR.isVisible = false;
        occluderFloor.isVisible = true;
        occluderTop.isVisible = true;
        occluderRight.isVisible = true;
        occluderLeft.isVisible = true;
        occluderback.isVisible = true;

        occluder.visibility = oclVisibility;
        occluderR.visibility = oclVisibility;
        occluderFloor.visibility = oclVisibility;
        occluderTop.visibility = oclVisibility;
        occluderRight.visibility = oclVisibility;
        occluderLeft.visibility = oclVisibility;
        occluderback.visibility = oclVisibility;

        scene.setRenderingAutoClearDepthStencil(1, false, false, false);
        scene.setRenderingAutoClearDepthStencil(0, true, true, true);
        scene.autoClear = true;

        rootScene.setEnabled(false);
        rootOccluder.setEnabled(false);

        var portalAppearded = false;
        var portalPosition = new BABYLON.Vector3();
        var nativePoseLogTime = 0;
        var nativeOccluderMode = "";
        var nativeMarkerVisible = false;
        var portalInside = false;
        var portalFrontSideSign = 1;
        var portalLastSignedDistance = null;
        var portalLastLocalCoordinates = { x: 0, y: 0, z: 0 };
        var portalLastInAperture = false;
        var portalMeshDiagnosticLogged = false;
        var portalMeshDiagnosticFrames = 0;
        var portalPlaneCenter = new BABYLON.Vector3();
        var portalRight = new BABYLON.Vector3(1, 0, 0);
        var portalUp = new BABYLON.Vector3(0, 1, 0);
        var portalNormal = new BABYLON.Vector3(0, 0, 1);
        var portalRotationQuaternion = new BABYLON.Quaternion();
        var portalAnchorPosition = new BABYLON.Vector3();
        var portalAnchorRotation = new BABYLON.Quaternion();
        var portalAnchorUpdateIgnoredLogged = false;
        var portalYaw = 0;
        var portalHalfWidth = 0.62;
        var portalHalfHeight = 1.08;
        var portalCrossingHysteresis = 0.05;
        var markerSettleFrameTarget = Number(globalThis.__nativeArPortalMarkerSettleFrames || 24);
        var markerSettleDistanceMeters = Number(globalThis.__nativeArPortalMarkerSettleDistanceMeters || 0.018);
        var nativeInvertOccluderSide = globalThis.__nativeArPortalInvertOccluderSide !== false;

        function applyMarkerVisibility(visible) {
            if (nativeMarkerVisible === visible) {
                return;
            }
            nativeMarkerVisible = visible;
            marker.isVisible = visible;
        }

        function applyOccluderMode(mode) {
            if (nativeOccluderMode === mode) {
                return;
            }
            nativeOccluderMode = mode;
            var inside = mode === "inside";
            occluder.isVisible = !inside;
            occluderR.isVisible = inside;
            occluderFloor.isVisible = !inside;
            occluderTop.isVisible = !inside;
            occluderRight.isVisible = !inside;
            occluderLeft.isVisible = !inside;
            occluderback.isVisible = !inside;
            reportStatus("native-xr-portal:occluder-mode=" + mode);
        }

        function applyPortalOcclusionForInsideState(logicalInside) {
            var renderInside = nativeInvertOccluderSide ? !logicalInside : logicalInside;
            applyOccluderMode(renderInside ? "inside" : "outside");
        }

        function currentCameraPosition() {
            return xrCamera ? (xrCamera.globalPosition || xrCamera.position) : null;
        }

        function updatePortalBasisFromYaw() {
            var sinYaw = Math.sin(portalYaw);
            var cosYaw = Math.cos(portalYaw);
            portalRight.set(cosYaw, 0, -sinYaw);
            portalUp.set(0, 1, 0);
            portalNormal.set(sinYaw, 0, cosYaw);
        }

        function facePortalTowardCamera(cameraPosition) {
            if (!cameraPosition) {
                portalYaw = 0;
            } else {
                var dx = Number(cameraPosition.x || 0) - portalPosition.x;
                var dz = Number(cameraPosition.z || 0) - portalPosition.z;
                if (Math.sqrt((dx * dx) + (dz * dz)) > 0.001) {
                    portalYaw = Math.atan2(dx, dz) + Math.PI;
                }
            }

            portalRotationQuaternion = BABYLON.Quaternion.RotationAxis(BABYLON.Axis.Y, portalYaw);
            rootPortal.rotationQuaternion.copyFrom(portalRotationQuaternion);
            updatePortalBasisFromYaw();
        }

        function updatePortalPlaneFrame() {
            portalPlaneCenter.copyFrom(portalPosition);
            portalPlaneCenter.x += (portalUp.x * 1.0) + (portalNormal.x * 0.05);
            portalPlaneCenter.y += (portalUp.y * 1.0) + (portalNormal.y * 0.05);
            portalPlaneCenter.z += (portalUp.z * 1.0) + (portalNormal.z * 0.05);
        }

        function portalLocalCoordinates(position) {
            updatePortalPlaneFrame();
            if (!position) {
                return { x: 0, y: 0, z: 0 };
            }

            return {
                x: ((Number(position.x || 0) - portalPlaneCenter.x) * portalRight.x) +
                    ((Number(position.y || 0) - portalPlaneCenter.y) * portalRight.y) +
                    ((Number(position.z || 0) - portalPlaneCenter.z) * portalRight.z),
                y: ((Number(position.x || 0) - portalPlaneCenter.x) * portalUp.x) +
                    ((Number(position.y || 0) - portalPlaneCenter.y) * portalUp.y) +
                    ((Number(position.z || 0) - portalPlaneCenter.z) * portalUp.z),
                z: ((Number(position.x || 0) - portalPlaneCenter.x) * portalNormal.x) +
                    ((Number(position.y || 0) - portalPlaneCenter.y) * portalNormal.y) +
                    ((Number(position.z || 0) - portalPlaneCenter.z) * portalNormal.z)
            };
        }

        function resetPortalCrossingState(cameraPosition) {
            var local = portalLocalCoordinates(cameraPosition);
            portalInside = false;
            portalFrontSideSign = local.z >= 0 ? 1 : -1;
            portalLastSignedDistance = local.z;
            portalLastLocalCoordinates = local;
            portalLastInAperture = Math.abs(local.x) <= portalHalfWidth && Math.abs(local.y) <= portalHalfHeight;
            reportStatus("native-xr-portal:crossing-reset:local=" + formatVector3(local) +
                ":frontSign=" + String(portalFrontSideSign) +
                ":invertOccluder=" + String(nativeInvertOccluderSide));
        }

        function updatePortalInsideState(cameraPosition) {
            var local = portalLocalCoordinates(cameraPosition);
            var inAperture = Math.abs(local.x) <= portalHalfWidth && Math.abs(local.y) <= portalHalfHeight;
            if (portalLastSignedDistance !== null && inAperture) {
                var previousRelativeSide = portalFrontSideSign * portalLastSignedDistance;
                var currentRelativeSide = portalFrontSideSign * local.z;
                if (!portalInside && previousRelativeSide > portalCrossingHysteresis && currentRelativeSide < -portalCrossingHysteresis) {
                    portalInside = true;
                } else if (portalInside && previousRelativeSide < -portalCrossingHysteresis && currentRelativeSide > portalCrossingHysteresis) {
                    portalInside = false;
                }
            }

            portalLastSignedDistance = local.z;
            portalLastLocalCoordinates = local;
            portalLastInAperture = inAperture;
            return portalInside;
        }

        function tryCreatePortalAnchor(hitResult) {
            if (!nativeAnchorSystem || !hitResult || !hitResult.xrHitResult || typeof hitResult.xrHitResult.createAnchor !== "function") {
                return;
            }

            nativeAnchorSystem.addAnchorPointUsingHitTestResultAsync(hitResult).then(function (anchor) {
                nativePortalAnchor = anchor;
                reportStatus("native-xr-portal:anchor-created:id=" + String(anchor.id));
            }).catch(function (error) {
                reportStatus("native-xr-portal:anchor-error:" + String(error && error.message ? error.message : error));
            });
        }

        if (nativeAnchorSystem) {
            nativeAnchorSystem.onAnchorUpdatedObservable.add(function (anchor) {
                if (anchor !== nativePortalAnchor || !portalAppearded || !anchor.transformationMatrix) {
                    return;
                }

                anchor.transformationMatrix.decompose(undefined, portalAnchorRotation, portalAnchorPosition);
                if (globalThis.__nativeArPortalApplyAnchorUpdates !== true) {
                    if (!portalAnchorUpdateIgnoredLogged) {
                        portalAnchorUpdateIgnoredLogged = true;
                        reportStatus("native-xr-portal:anchor-update-ignored:delta=" + vectorDistance(portalAnchorPosition, portalPosition).toFixed(3) +
                            ":anchor=" + formatVector3(portalAnchorPosition) +
                            ":portal=" + formatVector3(portalPosition));
                    }
                    return;
                }

                if (vectorDistance(portalAnchorPosition, portalPosition) > 0.35) {
                    reportStatus("native-xr-portal:anchor-update-rejected:delta=" + vectorDistance(portalAnchorPosition, portalPosition).toFixed(3) +
                        ":anchor=" + formatVector3(portalAnchorPosition) +
                        ":portal=" + formatVector3(portalPosition));
                    return;
                }
                rootPortal.position.copyFrom(portalAnchorPosition);
                portalPosition.copyFrom(portalAnchorPosition);
            });
        }

        function freezeStaticPortalMeshes() {
            var staticRoots = [rootScene, rootOccluder, rootPilar];
            for (var rootIndex = 0; rootIndex < staticRoots.length; rootIndex++) {
                var root = staticRoots[rootIndex];
                var meshes = root && typeof root.getChildMeshes === "function" ? root.getChildMeshes(false) : [];
                for (var meshIndex = 0; meshIndex < meshes.length; meshIndex++) {
                    prepareStaticMeshForFastPath(meshes[meshIndex]);
                }
            }

            var frozenMaterialCount = 0;
            if (globalThis.__nativeArPortalFreezeMaterials === true) {
                for (var materialIndex = 0; materialIndex < scene.materials.length; materialIndex++) {
                    if (scene.materials[materialIndex] && typeof scene.materials[materialIndex].freeze === "function") {
                        scene.materials[materialIndex].freeze();
                        frozenMaterialCount++;
                    }
                }
            }

            reportStatus("native-xr-portal:static-meshes-frozen:materials=" + String(frozenMaterialCount));
        }

        function logPortalMeshDiagnostics(label) {
            if (globalThis.__nativeArPortalLogMeshDiagnostics !== true) {
                return;
            }
            if (portalMeshDiagnosticLogged) {
                return;
            }
            portalMeshDiagnosticLogged = true;

            var rootMeshes = rootScene && typeof rootScene.getChildMeshes === "function" ? rootScene.getChildMeshes(false) : [];
            var activeMeshes = scene._activeMeshes && scene._activeMeshes.data ? scene._activeMeshes.data : [];
            var activeMeshCount = scene._activeMeshes && scene._activeMeshes.length !== undefined ? scene._activeMeshes.length : activeMeshes.length;
            var activeRootMeshes = 0;
            var activeDeloreanMeshes = 0;
            var activeNonDeloreanMeshes = 0;
            var enabledRootMeshes = 0;
            var visibleRootMeshes = 0;

            var rootMeshSet = new Set();
            for (var rootMeshIndex = 0; rootMeshIndex < rootMeshes.length; rootMeshIndex++) {
                var rootMesh = rootMeshes[rootMeshIndex];
                rootMeshSet.add(rootMesh);
                if (rootMesh && rootMesh.isEnabled && rootMesh.isEnabled()) {
                    enabledRootMeshes++;
                }
                if (rootMesh && rootMesh.isVisible !== false && rootMesh.visibility !== 0) {
                    visibleRootMeshes++;
                }
            }

            for (var activeMeshIndex = 0; activeMeshIndex < activeMeshCount; activeMeshIndex++) {
                var activeMesh = activeMeshes[activeMeshIndex];
                if (!rootMeshSet.has(activeMesh)) {
                    continue;
                }

                activeRootMeshes++;
                if (/d1_|delorean/i.test(String(activeMesh.name || ""))) {
                    activeDeloreanMeshes++;
                } else {
                    activeNonDeloreanMeshes++;
                }
            }

            reportStatus("native-xr-portal:mesh-diagnostics:" + String(label || "post-place") +
                ":root=" + String(rootMeshes.length) +
                ":enabled=" + String(enabledRootMeshes) +
                ":visible=" + String(visibleRootMeshes) +
                ":activeRoot=" + String(activeRootMeshes) +
                ":activeDelorean=" + String(activeDeloreanMeshes) +
                ":activeNonDelorean=" + String(activeNonDeloreanMeshes));
        }

        function updateMarkerPlacementCandidate(result) {
            var candidatePosition = new BABYLON.Vector3();
            var candidateRotation = new BABYLON.Quaternion();
            result.transformationMatrix.decompose(undefined, candidateRotation, candidatePosition);
            marker.position.copyFrom(candidatePosition);
            marker.rotationQuaternion.copyFrom(candidateRotation);

            if (!markerSettleHasCandidate || vectorDistance(candidatePosition, markerSettlePosition) > markerSettleDistanceMeters) {
                markerSettleHasCandidate = true;
                markerSettleFrames = 0;
                markerSettleLogged = false;
                markerSettleFloorPlaneId = result.__nativeFloorPlaneId || null;
                markerSettlePosition.copyFrom(candidatePosition);
                markerSettleRotation.copyFrom(candidateRotation);
            } else {
                markerSettleFrames++;
                markerSettleFloorPlaneId = result.__nativeFloorPlaneId || markerSettleFloorPlaneId;
                markerSettlePosition.copyFrom(candidatePosition);
                markerSettleRotation.copyFrom(candidateRotation);
            }

            hitTest = {
                position: markerSettlePosition.clone(),
                rotationQuaternion: markerSettleRotation.clone(),
                transformationMatrix: result.transformationMatrix.clone(),
                xrHitResult: result.xrHitResult,
                floorPlaneId: markerSettleFloorPlaneId
            };
        }

        function isMarkerPlacementSettled() {
            if (!markerSettleHasCandidate || markerSettleFrames < markerSettleFrameTarget) {
                return false;
            }

            if (!markerSettleLogged) {
                markerSettleLogged = true;
                reportStatus("native-xr-portal:marker-settled:frames=" + String(markerSettleFrames) +
                    ":position=" + formatVector3(markerSettlePosition) +
                    ":floorPlane=" + String(markerSettleFloorPlaneId));
            }
            return true;
        }

        function nativeAutoPlacePortal(reason) {
            if (portalAppearded || !hitTest || xr.baseExperience.state !== BABYLON.WebXRState.IN_XR) {
                return false;
            }
            if (!hitTest.floorPlaneId && globalThis.__nativeArPortalAllowFallbackPlacement !== true) {
                reportStatus("native-xr-portal:auto-place-skip:no-floor-plane");
                return false;
            }

            var candidatePosition = new BABYLON.Vector3();
            hitTest.transformationMatrix.decompose(undefined, undefined, candidatePosition);
            var cameraPosition = xrCamera ? (xrCamera.globalPosition || xrCamera.position) : null;
            var hitDistance = vectorDistance(candidatePosition, cameraPosition);
            if (hitDistance < 0.35) {
                if (!nativeNearHitSkipLogged) {
                    nativeNearHitSkipLogged = true;
                    reportStatus("native-xr-portal:auto-place-skip:near-hit:" + hitDistance.toFixed(3) +
                        ":candidate=" + formatVector3(candidatePosition) +
                        ":camera=" + formatVector3(cameraPosition));
                }
                return false;
            }

            reportStatus("native-xr-portal:auto-place:" + String(reason || "hit-test"));
            scene.onPointerDown({ type: "native-auto-pointerdown" }, null);
            reportStatus("native-xr-portal:auto-place-result:" + String(portalAppearded));
            return portalAppearded;
        }

        function nativeFallbackHitTestFromCamera() {
            if (!xrCamera || typeof xrCamera.getForwardRay !== "function") {
                return false;
            }

            var ray = xrCamera.getForwardRay(1.8);
            var direction = ray.direction.normalize();
            var origin = xrCamera.globalPosition || xrCamera.position;
            var position = origin.add(direction.scale(1.8));
            position.y = Number(origin.y || 0) - 1.25;
            hitTest = {
                transformationMatrix: BABYLON.Matrix.Compose(BABYLON.Vector3.One(), BABYLON.Quaternion.Identity(), position),
                floorPlaneId: null
            };
            marker.position.copyFrom(position);
            reportStatus("native-xr-portal:fallback-hit:camera=" + formatVector3(origin) +
                ":portal=" + formatVector3(position) +
                ":forward=" + formatVector3(direction));
            return true;
        }

        scene.onPointerDown = function () {
            if (hitTest && xr.baseExperience.state === BABYLON.WebXRState.IN_XR && !portalAppearded) {
                portalAppearded = true;

                rootScene.setEnabled(true);
                rootOccluder.setEnabled(true);
                applyMarkerVisibility(false);
                if (xrTest) {
                    xrTest.paused = true;
                }

                if (hitTest.position) {
                    portalPosition.copyFrom(hitTest.position);
                } else {
                    hitTest.transformationMatrix.decompose(undefined, undefined, portalPosition);
                }
                facePortalTowardCamera(currentCameraPosition());
                reportStatus("native-xr-portal:placed:portal=" + formatVector3(portalPosition) +
                    ":camera=" + formatVector3(xrCamera.position) +
                    ":yaw=" + portalYaw.toFixed(3) +
                    ":floorPlane=" + String(hitTest.floorPlaneId || "none"));

                rootPortal.position.copyFrom(portalPosition);
                rootOccluder.position.set(0, 0, 0);
                rootScene.position.set(0, 0, 0);
                rootPilar.position.set(0, 0, 0);

                rootScene.translate(BABYLON.Axis.Y, -1);
                rootScene.translate(BABYLON.Axis.X, 29);
                rootScene.translate(BABYLON.Axis.Z, -11);

                rootOccluder.translate(BABYLON.Axis.Y, 3);
                rootOccluder.rotationQuaternion = BABYLON.Quaternion.RotationAxis(new BABYLON.Vector3(-1, 0, 0), Math.PI / 2);
                rootOccluder.translate(BABYLON.Axis.Z, -2);
                occluderFloor.rotationQuaternion = BABYLON.Quaternion.RotationAxis(new BABYLON.Vector3(-1, 0, 0), Math.PI / 2);
                occluderFloor.translate(BABYLON.Axis.Y, 1);
                occluderFloor.translate(BABYLON.Axis.Z, 3.5);
                occluderTop.rotationQuaternion = BABYLON.Quaternion.RotationAxis(new BABYLON.Vector3(-1, 0, 0), Math.PI / 2);
                occluderTop.translate(BABYLON.Axis.Y, -2);
                occluderTop.translate(BABYLON.Axis.Z, 3.5);
                occluderback.translate(BABYLON.Axis.Y, 7);
                occluderback.translate(BABYLON.Axis.Z, 2);
                occluderRight.rotationQuaternion = BABYLON.Quaternion.RotationAxis(BABYLON.Axis.Z, Math.PI / 2);
                occluderRight.translate(BABYLON.Axis.Y, -3.4);
                occluderRight.translate(BABYLON.Axis.X, 3.5);
                occluderLeft.rotationQuaternion = BABYLON.Quaternion.RotationAxis(BABYLON.Axis.Z, Math.PI / 2);
                occluderLeft.translate(BABYLON.Axis.Y, 3.4);
                occluderLeft.translate(BABYLON.Axis.X, 3.5);

                var pilar1 = BABYLON.MeshBuilder.CreateBox("pilar1", { height: 2, width: 0.1, depth: 0.1 }, scene);
                var pilar2 = BABYLON.MeshBuilder.CreateBox("pilar2", { height: 2, width: 0.1, depth: 0.1 }, scene);
                var pilar3 = BABYLON.MeshBuilder.CreateBox("pilar3", { height: 1.1, width: 0.1, depth: 0.1 }, scene);

                pilar2.translate(BABYLON.Axis.X, 1, BABYLON.Space.LOCAL);
                pilar3.addRotation(0, 0, Math.PI / 2);
                pilar3.translate(BABYLON.Axis.Y, 1, BABYLON.Space.LOCAL);
                pilar3.translate(BABYLON.Axis.Y, -0.5, BABYLON.Space.LOCAL);

                pilar1.parent = rootPilar;
                pilar2.parent = rootPilar;
                pilar3.parent = rootPilar;

                rootPilar.translate(BABYLON.Axis.Y, 1);
                rootPilar.translate(BABYLON.Axis.X, -0.5);
                rootPilar.translate(BABYLON.Axis.Z, 0.05);
                createSceneReflectionProbeForPortal(scene, virtualWorldResult.meshes, rootPortal.position);
                resetPortalCrossingState(currentCameraPosition());
                tryCreatePortalAnchor(hitTest);
                updatePortalIblShadowsAfterPlacement(nativePortalIblShadows);

                registerGlowMesh(pilar1);
                registerGlowMesh(pilar2);
                registerGlowMesh(pilar3);
                pilar1.material = neonMaterial;
                pilar2.material = neonMaterial;
                pilar3.material = neonMaterial;
                prepareStaticMeshForFastPath(pilar1);
                prepareStaticMeshForFastPath(pilar2);
                prepareStaticMeshForFastPath(pilar3);

                var particleLoads = [];
                particleLoads.push(loadLocalParticleSystemAsync(PARTICLE_TOP_FILE, scene, pilar3).catch(function (error) {
                    reportStatus("native-xr-portal:particle-error:" + String(error && error.message ? error.message : error));
                }));
                particleLoads.push(loadLocalParticleSystemAsync(PARTICLE_SIDE_FILE, scene, pilar1).catch(function (error) {
                    reportStatus("native-xr-portal:particle-error:" + String(error && error.message ? error.message : error));
                }));
                particleLoads.push(loadLocalParticleSystemAsync(PARTICLE_SIDE_FILE, scene, pilar2).catch(function (error) {
                    reportStatus("native-xr-portal:particle-error:" + String(error && error.message ? error.message : error));
                }));

                if (globalThis.__nativeArPortalDebugDisableOccluders === true) {
                    reportStatus("native-xr-portal:debug-disable-occluders");
                    rootOccluder.setEnabled(false);
                }

                applyMarkerVisibility(false);
                applyPortalOcclusionForInsideState(portalInside);
                Promise.all(particleLoads).then(function () {
                    freezeStaticPortalMeshes();
                });
            }
        };

        xr.baseExperience.sessionManager.onXRSessionInit.add(function () {
            rectangle.isVisible = false;
            requestNativePlaneDetection("session-init");
        });
        xr.baseExperience.sessionManager.onXRSessionEnded.add(function () {
            rectangle.isVisible = true;
        });
        xr.baseExperience.onStateChangedObservable.add(function (state) {
            reportStatus("native-xr-portal:xr-state=" + String(state));
        });

        setTimeout(function () {
            reportStatus("native-xr-portal:enter-request");
            xr.baseExperience.enterXRAsync("immersive-ar", "unbounded", xr.renderTarget, {
                optionalFeatures: ["hit-test", "plane-detection", "anchors"]
            }).then(function () {
                reportStatus("native-xr-portal:xr-entered");
                requestNativePlaneDetection("xr-entered");
            }).catch(function (error) {
                reportStatus("native-xr-portal:xr-error:" + String(error && error.message ? error.message : error));
                if (typeof console !== "undefined" && typeof console.error === "function") {
                    console.error(error);
                }
            });
        }, 250);

        setTimeout(function () {
            if (globalThis.__nativeArPortalAllowFallbackPlacement === true && !portalAppearded && xr.baseExperience.state === BABYLON.WebXRState.IN_XR && nativeFallbackHitTestFromCamera()) {
                nativeAutoPlacePortal("fallback");
            } else if (!portalAppearded && xr.baseExperience.state === BABYLON.WebXRState.IN_XR) {
                reportStatus("native-xr-portal:auto-place-waiting-for-real-plane");
            }
        }, 6500);

        scene.onBeforeRenderObservable.add(function () {
            applyMarkerVisibility(!portalAppearded && markerSettleHasCandidate);

            if (portalPosition && portalAppearded) {
                var now = performance.now();
                if (now - nativePoseLogTime >= 1000) {
                    nativePoseLogTime = now;
                    var nativeViewerPosePosition = null;
                    try {
                        var frame = xr.baseExperience.sessionManager.currentFrame;
                        var referenceSpace = xr.baseExperience.sessionManager.referenceSpace;
                        var viewerPose = frame && referenceSpace ? frame.getViewerPose(referenceSpace) : null;
                        nativeViewerPosePosition = viewerPose && viewerPose.transform ? viewerPose.transform.position : null;
                    } catch (error) {
                        nativeViewerPosePosition = null;
                    }
                    reportStatus("native-xr-portal:pose:camera=" + formatVector3(xrCamera.position) +
                        ":viewer=" + formatVector3(nativeViewerPosePosition) +
                        ":portal=" + formatVector3(portalPosition) +
                        ":portalLocal=" + formatVector3(portalLastLocalCoordinates) +
                        ":inAperture=" + String(portalLastInAperture) +
                        ":portalInside=" + String(portalInside));
                }
            }

            if (xrCamera !== undefined && portalPosition !== undefined) {
                if (portalAppearded && globalThis.__nativeArPortalUseOfficialCrossingTest === true) {
                    applyPortalOcclusionForInsideState(updatePortalInsideState(currentCameraPosition()));
                }
            }

        });

        if (globalThis.__nativeArPortalLogMeshDiagnostics === true) {
            scene.onAfterRenderObservable.add(function () {
                if (portalAppearded && !portalMeshDiagnosticLogged) {
                    portalMeshDiagnosticFrames++;
                    if (portalMeshDiagnosticFrames >= 8) {
                        logPortalMeshDiagnostics("post-place");
                    }
                }
            });
        }

        return scene;
    }

    async function createPortalStressScene(engine) {
        reportStatus("native-xr-portal:stress-createScene");
        ensureBrowserLocationForLoaders();
        installNativePortalDocumentShim();
        installNativeWebGPUImageLoadingShim();
        installNativePortalAssetUrlShim();

        var scene = new BABYLON.Scene(engine);
        scene.clearColor = new BABYLON.Color4(0.02, 0.024, 0.03, 1);

        var canvas = typeof engine.getRenderingCanvas === "function" ? engine.getRenderingCanvas() : null;
        var camera = new BABYLON.ArcRotateCamera(
            "native-xr-portal-stress-camera",
            -Math.PI * 0.56,
            Math.PI * 0.42,
            36,
            new BABYLON.Vector3(18, 3.5, -8),
            scene);
        camera.minZ = 0.05;
        camera.maxZ = 180;
        camera.fov = 0.74;
        if (canvas && typeof camera.attachControl === "function") {
            camera.attachControl(canvas, true);
        }
        scene.activeCamera = camera;

        createPortalEnvironmentTexture(scene);
        var virtualWorldResult = await BABYLON.SceneLoader.ImportMeshAsync("", ASSET_ROOT_URL, HILL_VALLEY_FILE, scene);
        configurePortalRealtimeLights(scene, virtualWorldResult.meshes);
        applyRealtimeMaterialOverrides(scene);
        logPortalMaterialParity(scene);

        var importedRoot = new BABYLON.TransformNode("native-xr-portal-stress-root", scene);
        importedRoot.rotationQuaternion = BABYLON.Quaternion.Identity();
        importedRoot.position.set(0, 0, 0);
        importedRoot.translate(BABYLON.Axis.Y, -1);
        importedRoot.translate(BABYLON.Axis.X, 29);
        importedRoot.translate(BABYLON.Axis.Z, -11);

        var importedNodes = [];
        for (var meshIndex = 0; meshIndex < virtualWorldResult.meshes.length; meshIndex++) {
            importedNodes.push(virtualWorldResult.meshes[meshIndex]);
        }
        if (virtualWorldResult.transformNodes) {
            for (var transformIndex = 0; transformIndex < virtualWorldResult.transformNodes.length; transformIndex++) {
                importedNodes.push(virtualWorldResult.transformNodes[transformIndex]);
            }
        }

        function isImportedNode(node) {
            for (var importedNodeIndex = 0; importedNodeIndex < importedNodes.length; importedNodeIndex++) {
                if (importedNodes[importedNodeIndex] === node) {
                    return true;
                }
            }
            return false;
        }

        var vertexMeshCount = 0;
        var deloreanMeshCount = 0;
        for (var importedMeshIndex = 0; importedMeshIndex < virtualWorldResult.meshes.length; importedMeshIndex++) {
            var importedMesh = virtualWorldResult.meshes[importedMeshIndex];
            prepareStaticMeshForFastPath(importedMesh);
            importedMesh.renderingGroupId = 1;
            if (importedMesh.getTotalVertices && importedMesh.getTotalVertices() > 0) {
                vertexMeshCount++;
            }
            if (/d1_|delorean/i.test(String(importedMesh.name || ""))) {
                deloreanMeshCount++;
            }
            if (!isImportedNode(importedMesh.parent)) {
                importedMesh.parent = importedRoot;
            }
        }
        for (var nodeIndex = 0; nodeIndex < importedNodes.length; nodeIndex++) {
            var importedNode = importedNodes[nodeIndex];
            if (importedNode && !isImportedNode(importedNode.parent)) {
                importedNode.parent = importedRoot;
            }
        }

        var nativeTextureCount = scene.textures ? scene.textures.length : 0;
        reportStatus("native-xr-portal:stress-world:meshes=" + String(virtualWorldResult.meshes.length) +
            ":vertexMeshes=" + String(vertexMeshCount) +
            ":deloreanMeshes=" + String(deloreanMeshCount) +
            ":sceneMaterials=" + String(scene.materials.length) +
            ":textures=" + String(nativeTextureCount));
        setTimeout(function () {
            logTextureReadiness(scene, nativeTextureCount, "stress-4s");
        }, 4000);
        setTimeout(function () {
            logTextureReadiness(scene, nativeTextureCount, "stress-12s");
        }, 12000);

        createSceneReflectionProbeForPortal(scene, virtualWorldResult.meshes, importedRoot.position);
        var nativePortalSSAO2 = enablePortalSSAO2(scene, camera);
        void nativePortalSSAO2;

        if (globalThis.__nativeArPortalUseSceneStaticFastPath === true) {
            enableSceneStaticFastPath(scene);
        }

        return scene;
    }

    globalThis.__nativeArPortalReport = reportStatus;
    globalThis.__nativeArPortalPlaygroundUrl = "app:///Scripts/native_xr_babylon_portal_demo.js";

    globalThis.createScene = async function (engineArg) {
        reportStatus("native-xr-portal:createScene");
        logStatsDelta("native-xr-portal:stats-start");
        globalThis.engine = engineArg;
        enableWebGPUFastPath(engineArg);

        var canvas = typeof engineArg.getRenderingCanvas === "function" ? engineArg.getRenderingCanvas() : null;
        if (canvas) {
            globalThis.canvas = canvas;
        }

        try {
            if (typeof engineArg.captureGPUFrameTime === "function") {
                engineArg.captureGPUFrameTime(true);
            }
        } catch (error) {
            reportStatus("native-xr-portal:gpu-frame-time-unavailable");
        }

        var scene = globalThis.__nativeArPortalForceNonXrStress === true ?
            await createPortalStressScene(engineArg) :
            await createPortalScene(engineArg);
        scene.onAfterRenderObservable.add(function () {
            var now = performance.now();
            if (now - lastFpsLogTime >= 2000) {
                lastFpsLogTime = now;
                reportStatus("native-xr-portal:fps=" + Math.round(engineArg.getFps()));
                logStatsDelta("native-xr-portal:stats");
            }
        });

        setTimeout(function () {
            reportStatus("native-xr-portal:exit");
            if (typeof TestUtils !== "undefined" && TestUtils.exit) {
                TestUtils.exit(0);
            }
        }, EXIT_DELAY_MS);

        return scene;
    };
})();
