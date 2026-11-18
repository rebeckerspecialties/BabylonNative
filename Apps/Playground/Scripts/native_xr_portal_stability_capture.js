// Validation only: captures fixed poses under exact screen-space shifts so that
// frame-to-frame instability can be measured without reprojection. The shifts
// are applied through the projection matrix, which moves the whole image
// uniformly. Analyze with Apps/scripts/analyzePortalStability.mjs.
// Load in place of the moving benchmark, after the color-reuse prelude.
globalThis.__nativeArPortalForceNonXrStress = true;
globalThis.__nativeArPortalSSAOUsePrePass = true;
globalThis.__nativeArPortalExitDelayMs = 3600000;
globalThis.__nativeArPortalLogMeshDiagnostics = false;
globalThis.__nativePlaygroundStatus = function (message) { console.log(message); };
if (typeof __nativeValidationSetFrameTimerEnabled === "function") {
    __nativeValidationSetFrameTimerEnabled(true);
}
(function () {
    var config = globalThis.__nativeArPortalStability || {};
    var view = config.view || "final";
    var mode = config.mode || "whole-pixel";
    var label = config.label || "capture";
    if ((view !== "final" && view !== "ao") || (mode !== "whole-pixel" && mode !== "sub-pixel") || !/^[a-z0-9-]+$/.test(label)) {
        throw new Error("Stability capture needs view final|ao, mode whole-pixel|sub-pixel and a lowercase label.");
    }
    var poses = [
        { name: "orbit", position: [4.37, 4.75, -1.6], target: [-1.07, 0.89, 3.67] },
        { name: "cafe", position: [2.5, 1.7, 1.0], target: [-10, 1.3, 1.0] },
        { name: "car", position: [3.5, 1.3, 0.5], target: [-1.0, 0.5, 3.7] },
        { name: "trees", position: [-6, 2.2, 6], target: [26, 4, 4] },
        { name: "bush", position: [0, 2.0, -4], target: [20, 1.0, -10] }
    ];
    // Whole pixels leave rasterization identical, isolating screen-locked
    // sampling (AO grid and pattern). A constant sub-pixel velocity exposes
    // coverage popping: a band-limited image changes smoothly between frames.
    var shifts = [[0, 0], [1, 0], [2, 1], [3, 1], [4, 2], [5, 3], [6, 3], [7, 4]];
    if (mode === "sub-pixel") {
        shifts = shifts.concat([[8, 4], [9, 5]]).map(function (shift, index) { return [0.25 * index, 0.125 * index]; });
    }
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        if (view === "ao") {
            showAmbientOcclusion(scene);
        }
        var engine = scene.getEngine();
        var camera = scene.activeCamera;
        camera.detachControl();
        camera.inputs.clear();
        camera.inertia = 0;
        camera.lowerRadiusLimit = null;
        camera.upperRadiusLimit = null;
        var width = engine.getRenderWidth();
        var height = engine.getRenderHeight();
        var poseIndex = 0;
        var shiftIndex = -1;
        var wait = 0;
        var frame = 0;
        var pending = false;
        var done = false;
        var projection = null;
        function applyPose() {
            camera.unfreezeProjectionMatrix();
            camera.setTarget(BABYLON.Vector3.FromArray(poses[poseIndex].target));
            camera.setPosition(BABYLON.Vector3.FromArray(poses[poseIndex].position));
            projection = null;
        }
        function applyShift() {
            if (!projection) projection = Array.from(camera.getProjectionMatrix(true).m);
            var shifted = projection.slice();
            // Perspective clip.xy gains w times these terms, so NDC moves uniformly.
            var handedness = shifted[11] >= 0 ? 1 : -1;
            shifted[8] += handedness * 2 * shifts[shiftIndex][0] / width;
            shifted[9] += handedness * 2 * shifts[shiftIndex][1] / height;
            camera.freezeProjectionMatrix(BABYLON.Matrix.FromArray(shifted));
        }
        status("stability:manifest=" + JSON.stringify({ label: label, view: view, mode: mode, width: width, height: height,
            poses: poses.map(function (pose) { return pose.name; }), shifts: shifts }));
        applyPose();
        scene.onAfterRenderObservable.add(function () {
            if (pending || done) return;
            frame++;
            if (frame < 240 || !scene.isReady()) return;
            if (shiftIndex < 0) {
                shiftIndex = 0;
                applyShift();
                wait = 24;
                return;
            }
            if (--wait > 0) return;
            pending = true;
            var name = ["stability", label, view, mode, poses[poseIndex].name, String(shiftIndex).padStart(2, "0")].join("-") + ".png";
            TestUtils.getFrameBufferData(function (data, capturedWidth, capturedHeight) {
                if (capturedWidth !== width || capturedHeight !== height || data.length !== width * height * 4) {
                    throw new Error("Stability capture must read back the render size exactly.");
                }
                TestUtils.writePNG(data, width, height, TestUtils.getOutputDirectory() + "/" + name);
                shiftIndex++;
                if (shiftIndex < shifts.length) {
                    applyShift();
                    wait = 3;
                } else if (++poseIndex < poses.length) {
                    shiftIndex = -1;
                    applyPose();
                } else {
                    done = true;
                    status("stability:complete");
                    TestUtils.exit(0);
                }
                pending = false;
            });
        });
    };

    // Replaces the combined color with the filtered, upsampled AO term.
    function showAmbientOcclusion(scene) {
        var store = BABYLON.ShaderStore.ShadersStoreWGSL;
        var pipeline = scene.postProcessRenderPipelineManager.supportedPipelines.find(function (candidate) {
            return candidate.name === "native-xr-portal-ssao2";
        });
        var combined = "vec4f(sceneColor.rgb * ssaoColor.r, sceneColor.a)";
        var source = store.portalSsaoColorReusePixelShader;
        if (!pipeline || typeof source !== "string" || source.split(combined).length !== 2) {
            throw new Error("The AO view requires the R8 and color-reuse combine contract.");
        }
        store.portalSsaoOnlyPixelShader = source.replace(combined, "vec4f(vec3f(ssaoColor.r), 1.0)");
        pipeline._ssaoCombinePostProcess.updateEffect(null, null, null, undefined, undefined, undefined, undefined, "portalSsaoOnly");
    }
})();
