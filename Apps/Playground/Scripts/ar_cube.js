// AR cube: the JavaScript counterpart of the snqr guest-ar-cube sample.
//
// A 0.36 m cube (0.18 m half-extent) spins in front of the viewer. With an immersive-ar
// session the cube is anchored on the first tracked plane or hit-test result; until one
// exists, and when the host has no XR system at all, it is shown as a centered preview
// 1.35 m in front of the camera, the same fallback distance the Wasm sample uses so the
// rotated corners stay inside the Mac camera's vertical frustum. Depth sensing drives
// occlusion and light estimation drives the lighting when the session offers them.

var AR_CUBE_HALF_EXTENT_METERS = 0.18;
var AR_CUBE_CENTERED_PREVIEW_DISTANCE_METERS = 1.35;
var AR_CUBE_SPIN_RADIANS_PER_SECOND = 0.6;

function arCubeLog(message) {
    BABYLON.Tools.Log("[ArCube] " + message);
}

async function createScene(engine) {
    var scene = new BABYLON.Scene(engine);
    scene.clearColor = new BABYLON.Color4(0.08, 0.09, 0.11, 1);

    var camera = new BABYLON.FreeCamera("preview-camera", BABYLON.Vector3.Zero(), scene);
    camera.minZ = 0.05;
    camera.maxZ = 50;
    camera.setTarget(new BABYLON.Vector3(0, 0, -1));

    var hemispheric = new BABYLON.HemisphericLight("fill", new BABYLON.Vector3(0.2, 1, 0.3), scene);
    hemispheric.intensity = 0.6;
    var directional = new BABYLON.DirectionalLight("key", new BABYLON.Vector3(-0.4, -1, -0.6), scene);
    directional.intensity = 1.2;

    var cube = BABYLON.MeshBuilder.CreateBox("ar-cube", { size: AR_CUBE_HALF_EXTENT_METERS * 2 }, scene);
    var material = new BABYLON.PBRMetallicRoughnessMaterial("ar-cube-material", scene);
    material.baseColor = new BABYLON.Color3(0.85, 0.35, 0.2);
    material.metallic = 0.1;
    material.roughness = 0.45;
    cube.material = material;
    cube.position = new BABYLON.Vector3(0, 0, -AR_CUBE_CENTERED_PREVIEW_DISTANCE_METERS);

    var placed = false;
    scene.onBeforeRenderObservable.add(function () {
        var seconds = scene.getEngine().getDeltaTime() / 1000;
        cube.rotation.y += AR_CUBE_SPIN_RADIANS_PER_SECOND * seconds;
        cube.rotation.x += AR_CUBE_SPIN_RADIANS_PER_SECOND * 0.35 * seconds;
    });

    var xrAvailable = typeof navigator !== "undefined" && navigator.xr && typeof navigator.xr.isSessionSupported === "function";
    if (!xrAvailable) {
        arCubeLog("no XR system on this host; rendering the centered preview at " + AR_CUBE_CENTERED_PREVIEW_DISTANCE_METERS + " m");
        return scene;
    }

    var supported = false;
    try {
        supported = await navigator.xr.isSessionSupported("immersive-ar");
    } catch (error) {
        arCubeLog("isSessionSupported failed: " + error);
    }
    if (!supported) {
        arCubeLog("immersive-ar is not supported here; rendering the centered preview");
        return scene;
    }

    var xr = await scene.createDefaultXRExperienceAsync({
        disableDefaultUI: true,
        disableTeleportation: true,
        optionalFeatures: true,
        uiOptions: { sessionMode: "immersive-ar", referenceSpaceType: "local" },
    });
    var features = xr.baseExperience.featuresManager;

    // A WebGPU engine presents XR through projection layers; Babylon.js refuses to enter
    // the session without the Layers feature.
    if (engine.isWebGPU && BABYLON.WebXRLayers) {
        features.enableFeature(BABYLON.WebXRLayers.Name, "latest");
        arCubeLog("WebGPU layers enabled");
    }

    var hitTest = null;
    try {
        hitTest = features.enableFeature(BABYLON.WebXRFeatureName.HIT_TEST, "latest", { disablePermanentHitTest: false });
    } catch (error) {
        arCubeLog("hit-test unavailable: " + error);
    }
    var anchors = null;
    try {
        anchors = features.enableFeature(BABYLON.WebXRFeatureName.ANCHOR_SYSTEM, "latest");
    } catch (error) {
        arCubeLog("anchors unavailable: " + error);
    }
    var planes = null;
    try {
        planes = features.enableFeature(BABYLON.WebXRFeatureName.PLANE_DETECTION, "latest");
    } catch (error) {
        arCubeLog("plane detection unavailable: " + error);
    }
    // Babylon.js attaches these two when the session starts and fails the whole
    // enterXRAsync if the host's session lacks the entry points, so only enable them
    // where the host implements them (NativeXr on ARKit implements neither yet).
    function hostHasMethod(className, methodName) {
        var constructor = globalThis[className];
        return !!constructor && !!constructor.prototype && typeof constructor.prototype[methodName] === "function";
    }
    if (hostHasMethod("XRFrame", "getDepthInformation") || hostHasMethod("XRWebGLBinding", "getDepthInformation")) {
        try {
            features.enableFeature(BABYLON.WebXRFeatureName.DEPTH_SENSING, "latest", {
                usagePreference: ["gpu-optimized", "cpu-optimized"],
                dataFormatPreference: ["float32", "luminance-alpha"],
            });
            arCubeLog("depth sensing enabled (occlusion)");
        } catch (error) {
            arCubeLog("depth sensing unavailable: " + error);
        }
    } else {
        arCubeLog("depth sensing is not implemented by this host");
    }
    if (hostHasMethod("XRSession", "requestLightProbe")) {
        try {
            features.enableFeature(BABYLON.WebXRFeatureName.LIGHT_ESTIMATION, "latest", {
                setSceneEnvironmentTexture: true,
                createDirectionalLightSource: true,
            });
            arCubeLog("light estimation enabled");
        } catch (error) {
            arCubeLog("light estimation unavailable: " + error);
        }
    } else {
        arCubeLog("light estimation is not implemented by this host");
    }

    function placeAt(position) {
        if (placed) {
            return;
        }
        placed = true;
        cube.parent = null;
        cube.position.copyFrom(position);
        cube.position.y += AR_CUBE_HALF_EXTENT_METERS;
        arCubeLog("placed at " + position.toString());
    }

    if (planes) {
        planes.onPlaneAddedObservable.add(function (plane) {
            if (placed || !plane.polygonDefinition || plane.polygonDefinition.length === 0) {
                return;
            }
            var center = BABYLON.Vector3.Zero();
            for (var i = 0; i < plane.polygonDefinition.length; i++) {
                center.addInPlace(plane.polygonDefinition[i]);
            }
            center.scaleInPlace(1 / plane.polygonDefinition.length);
            placeAt(BABYLON.Vector3.TransformCoordinates(center, plane.transformationMatrix));
        });
    }
    if (hitTest) {
        hitTest.onHitTestResultObservable.add(function (results) {
            if (placed || results.length === 0) {
                return;
            }
            var result = results[0];
            if (anchors) {
                anchors.addAnchorPointUsingHitTestResultAsync(result).then(function (anchor) {
                    anchor.attachedNode = cube;
                    placeAt(result.position);
                }, function (error) {
                    arCubeLog("anchor failed, placing without one: " + error);
                    placeAt(result.position);
                });
            } else {
                placeAt(result.position);
            }
        });
    }

    // One line every five seconds while the session delivers frames, so a console capture
    // shows whether XR frames flow without access to the device's screen.
    var xrFrameCount = 0;
    var lastFrameReportTime = 0;
    xr.baseExperience.sessionManager.onXRFrameObservable.add(function () {
        xrFrameCount++;
        var now = Date.now();
        if (now - lastFrameReportTime >= 5000) {
            lastFrameReportTime = now;
            arCubeLog("xr frames=" + xrFrameCount + " fps=" + Math.round(engine.getFps()) + " placed=" + placed);
        }
    });

    xr.baseExperience.onStateChangedObservable.add(function (state) {
        arCubeLog("xr state: " + BABYLON.WebXRState[state]);
        if (state === BABYLON.WebXRState.IN_XR) {
            // Centered preview relative to the viewer until tracking places the cube.
            cube.parent = xr.baseExperience.camera;
            cube.position.set(0, 0, -AR_CUBE_CENTERED_PREVIEW_DISTANCE_METERS);
            arCubeLog("in immersive-ar; centered preview until a plane or hit is tracked");
        }
    });

    try {
        await xr.baseExperience.enterXRAsync("immersive-ar", "local", undefined, {
            optionalFeatures: ["hit-test", "anchors", "plane-detection", "depth-sensing", "light-estimation"],
        });
    } catch (error) {
        arCubeLog("enterXRAsync failed, keeping the preview: " + error);
    }
    return scene;
}
