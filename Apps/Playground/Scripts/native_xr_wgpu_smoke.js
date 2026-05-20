globalThis.__babylonUseWebGPU = true;

(function () {
    var lastStats = null;
    var frameCount = 0;
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
            reportStatus("native-xr-smoke:stats-error:" + String(error && error.message ? error.message : error));
            return null;
        }
    }

    function logStatsDelta(prefix) {
        var stats = getStatsSnapshot();
        if (!stats) {
            return;
        }

        if (lastStats) {
            var renderDelta = Number(stats.renderFrames || 0) - Number(lastStats.renderFrames || 0);
            var drawRequested = stats.webgpuDrawRequested ? "1" : "0";
            reportStatus(prefix + ":frames=" + renderDelta +
                ":gpuMiB=" + Number(stats.estimatedGpuMemoryMiB || 0).toFixed(2) +
                ":draw=" + drawRequested);
        } else {
            reportStatus(prefix + ":gpuMiB=" + Number(stats.estimatedGpuMemoryMiB || 0).toFixed(2));
        }

        lastStats = stats;
    }

    function makePortalScene(scene) {
        var root = new BABYLON.TransformNode("native-xr-smoke-root", scene);
        root.position = new BABYLON.Vector3(0, 0, 1.25);

        var portalMaterial = new BABYLON.StandardMaterial("native-xr-portal-material", scene);
        portalMaterial.diffuseColor = new BABYLON.Color3(0.05, 0.55, 1.0);
        portalMaterial.emissiveColor = new BABYLON.Color3(0.01, 0.18, 0.35);
        portalMaterial.alpha = 0.7;

        var portal = BABYLON.MeshBuilder.CreateTorus("native-xr-portal", {
            diameter: 0.55,
            thickness: 0.025,
            tessellation: 48
        }, scene);
        portal.parent = root;
        portal.rotation.x = Math.PI / 2;
        portal.material = portalMaterial;

        var cubeMaterial = new BABYLON.StandardMaterial("native-xr-cube-material", scene);
        cubeMaterial.diffuseColor = new BABYLON.Color3(1.0, 0.42, 0.12);
        cubeMaterial.specularColor = new BABYLON.Color3(0.2, 0.2, 0.2);

        var cube = BABYLON.MeshBuilder.CreateBox("native-xr-cube", { size: 0.18 }, scene);
        cube.parent = root;
        cube.position.z = 0.05;
        cube.material = cubeMaterial;

        var shadowDiscMaterial = new BABYLON.StandardMaterial("native-xr-shadow-material", scene);
        shadowDiscMaterial.diffuseColor = new BABYLON.Color3(0, 0, 0);
        shadowDiscMaterial.alpha = 0.22;

        var shadowDisc = BABYLON.MeshBuilder.CreateDisc("native-xr-shadow-disc", {
            radius: 0.22,
            tessellation: 48
        }, scene);
        shadowDisc.parent = root;
        shadowDisc.position.y = -0.22;
        shadowDisc.rotation.x = Math.PI / 2;
        shadowDisc.material = shadowDiscMaterial;

        scene.onBeforeRenderObservable.add(function () {
            var delta = scene.getEngine().getDeltaTime() * 0.001;
            cube.rotation.x += delta * 0.7;
            cube.rotation.y += delta * 1.1;
            portal.rotation.z += delta * 0.28;
        });
    }

    globalThis.createScene = async function (engine) {
        reportStatus("native-xr-smoke:createScene");
        logStatsDelta("native-xr-smoke:stats-start");

        var scene = new BABYLON.Scene(engine);
        scene.clearColor = new BABYLON.Color4(0, 0, 0, 0);
        scene.autoClear = false;

        var camera = new BABYLON.FreeCamera("native-xr-camera", new BABYLON.Vector3(0, 1.55, -2.0), scene);
        camera.minZ = 0.02;
        camera.maxZ = 50;
        camera.setTarget(new BABYLON.Vector3(0, 1.4, 1.0));
        scene.activeCamera = camera;

        var light = new BABYLON.HemisphericLight("native-xr-light", new BABYLON.Vector3(0, 1, 0), scene);
        light.intensity = 0.9;

        makePortalScene(scene);

        try {
            engine.captureGPUFrameTime(true);
        } catch (error) {
            reportStatus("native-xr-smoke:gpu-frame-time-unavailable");
        }

        scene.onAfterRenderObservable.add(function () {
            frameCount += 1;
            var now = performance.now();
            if (now - lastFpsLogTime >= 2000) {
                lastFpsLogTime = now;
                reportStatus("native-xr-smoke:fps=" + Math.round(engine.getFps()));
                logStatsDelta("native-xr-smoke:stats");
            }
        });

        setTimeout(function () {
            scene.createDefaultXRExperienceAsync({
                disableDefaultUI: true,
                disableTeleportation: true
            }).then(function (xr) {
                reportStatus("native-xr-smoke:xr-created");

                xr.baseExperience.onStateChangedObservable.add(function (state) {
                    reportStatus("native-xr-smoke:xr-state=" + String(state));
                });

                return xr.baseExperience.enterXRAsync("immersive-ar", "unbounded", xr.renderTarget, {
                    optionalFeatures: ["hit-test"]
                });
            }).then(function () {
                reportStatus("native-xr-smoke:xr-entered");
            }).catch(function (error) {
                reportStatus("native-xr-smoke:xr-error:" + String(error && error.message ? error.message : error));
                if (typeof console !== "undefined" && typeof console.error === "function") {
                    console.error(error);
                }
            });
        }, 250);

        return scene;
    };
})();
