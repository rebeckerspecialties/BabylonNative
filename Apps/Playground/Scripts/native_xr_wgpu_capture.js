globalThis.__babylonUseWebGPU = true;

(function () {
    function reportStatus(message) {
        try {
            if (typeof globalThis.__nativePlaygroundStatus === "function") {
                globalThis.__nativePlaygroundStatus(String(message));
            }
        } catch (error) {
            void error;
        }
    }

    function material(scene, name, color, alpha) {
        var mat = new BABYLON.StandardMaterial(name, scene);
        mat.diffuseColor = color;
        mat.emissiveColor = color.scale(0.25);
        mat.specularColor = new BABYLON.Color3(0.08, 0.08, 0.08);
        mat.alpha = alpha === undefined ? 1 : alpha;
        return mat;
    }

    function addCaptureMarkers(scene) {
        var root = new BABYLON.TransformNode("native-xr-capture-root", scene);
        root.position = new BABYLON.Vector3(0, 0, 1.35);

        var portal = BABYLON.MeshBuilder.CreateTorus("native-xr-capture-portal", {
            diameter: 0.62,
            thickness: 0.026,
            tessellation: 64
        }, scene);
        portal.parent = root;
        portal.rotation.x = Math.PI / 2;
        portal.material = material(scene, "native-xr-capture-portal-material", new BABYLON.Color3(0.04, 0.55, 1.0), 0.72);

        var redTop = BABYLON.MeshBuilder.CreateSphere("native-xr-capture-red-top", {
            diameter: 0.13,
            segments: 16
        }, scene);
        redTop.parent = root;
        redTop.position.y = 0.28;
        redTop.position.z = 0.03;
        redTop.material = material(scene, "native-xr-capture-red-top-material", new BABYLON.Color3(1.0, 0.05, 0.02));

        var cube = BABYLON.MeshBuilder.CreateBox("native-xr-capture-orange-center", {
            size: 0.18
        }, scene);
        cube.parent = root;
        cube.position.z = 0.04;
        cube.material = material(scene, "native-xr-capture-orange-center-material", new BABYLON.Color3(1.0, 0.44, 0.1));

        var greenBottom = BABYLON.MeshBuilder.CreateSphere("native-xr-capture-green-bottom", {
            diameter: 0.11,
            segments: 16
        }, scene);
        greenBottom.parent = root;
        greenBottom.position.y = -0.3;
        greenBottom.position.z = 0.03;
        greenBottom.material = material(scene, "native-xr-capture-green-bottom-material", new BABYLON.Color3(0.0, 0.95, 0.18));

        var shadow = BABYLON.MeshBuilder.CreateDisc("native-xr-capture-shadow-disc", {
            radius: 0.23,
            tessellation: 48
        }, scene);
        shadow.parent = root;
        shadow.position.y = -0.39;
        shadow.rotation.x = Math.PI / 2;
        shadow.material = material(scene, "native-xr-capture-shadow-material", new BABYLON.Color3(0, 0, 0), 0.26);

        scene.onBeforeRenderObservable.add(function () {
            var delta = scene.getEngine().getDeltaTime() * 0.001;
            cube.rotation.x += delta * 0.5;
            cube.rotation.y += delta * 0.9;
            portal.rotation.z += delta * 0.2;
        });
    }

    globalThis.createScene = async function (engine) {
        reportStatus("native-xr-capture:createScene");

        var scene = new BABYLON.Scene(engine);
        scene.clearColor = new BABYLON.Color4(0, 0, 0, 0);
        scene.autoClear = false;

        var camera = new BABYLON.FreeCamera("native-xr-capture-camera", new BABYLON.Vector3(0, 1.55, -2.0), scene);
        camera.minZ = 0.02;
        camera.maxZ = 50;
        camera.setTarget(new BABYLON.Vector3(0, 1.4, 1.0));
        scene.activeCamera = camera;

        var light = new BABYLON.HemisphericLight("native-xr-capture-light", new BABYLON.Vector3(0, 1, 0), scene);
        light.intensity = 0.95;

        addCaptureMarkers(scene);

        setTimeout(function () {
            scene.createDefaultXRExperienceAsync({
                disableDefaultUI: true,
                disableTeleportation: true
            }).then(function (xr) {
                reportStatus("native-xr-capture:xr-created");
                xr.baseExperience.onStateChangedObservable.add(function (state) {
                    reportStatus("native-xr-capture:xr-state=" + String(state));
                });
                return xr.baseExperience.enterXRAsync("immersive-ar", "unbounded", xr.renderTarget, {
                    optionalFeatures: ["hit-test"]
                });
            }).then(function () {
                reportStatus("native-xr-capture:xr-entered");
            }).catch(function (error) {
                reportStatus("native-xr-capture:xr-error:" + String(error && error.message ? error.message : error));
                if (typeof console !== "undefined" && typeof console.error === "function") {
                    console.error(error);
                }
            });
        }, 250);

        setTimeout(function () {
            reportStatus("native-xr-capture:exit");
            if (typeof TestUtils !== "undefined" && TestUtils.exit) {
                TestUtils.exit(0);
            }
        }, 9000);

        return scene;
    };
})();
