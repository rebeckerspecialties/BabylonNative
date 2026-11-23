// Diagnostic only: pair with BABYLON_NATIVE_XR_TRACE_REGISTRATION, not FPS profiling.
(function () {
    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, reportStatus) {
        if (previousStart) {
            previousStart(scene, reportStatus);
        }
        var manager = scene.activeCamera && scene.activeCamera._xrSessionManager;
        if (!manager) {
            reportStatus("native-xr-portal:registration=unavailable:no-xr-session");
            return;
        }
        var nativePoint = new BABYLON.Vector3(0, -1, -2);
        var baseInverse = BABYLON.Matrix.Identity();
        var referencePoseMatrix = BABYLON.Matrix.Identity();
        var baseToReference = BABYLON.Matrix.Identity();
        var observer = scene.onBeforeCameraRenderObservable.add(function (camera) {
            var target = camera && camera.outputRenderTarget;
            if (!target) {
                return;
            }
            var frame = manager.currentFrame;
            var basePose = frame && frame.getViewerPose(manager.baseReferenceSpace);
            if (!basePose) {
                return;
            }
            // NativeXR can reuse pose objects; copy the base matrix before requesting another pose.
            BABYLON.Matrix.FromArrayToRef(basePose.transform.matrix, 0, baseInverse);
            baseInverse.invert();
            var referencePose = frame.getViewerPose(manager.referenceSpace);
            if (!referencePose) {
                return;
            }
            BABYLON.Matrix.FromArrayToRef(referencePose.transform.matrix, 0, referencePoseMatrix);
            baseInverse.multiplyToRef(referencePoseMatrix, baseToReference);
            var point = BABYLON.Vector3.TransformCoordinates(nativePoint, baseToReference);
            if (!scene.useRightHandedSystem) {
                point.z *= -1;
            }
            var size = target.getSize();
            var ndc = BABYLON.Vector3.TransformCoordinates(point, camera.getViewMatrix().multiply(camera.getProjectionMatrix()));
            var position = camera.globalPosition;
            reportStatus("native-xr-portal:registration:position=" + position.x.toFixed(4) + "," + position.y.toFixed(4) + "," +
                (scene.useRightHandedSystem ? position.z : -position.z).toFixed(4) +
                ":pixel=" + ((ndc.x * 0.5 + 0.5) * size.width).toFixed(1) + "," + ((0.5 - ndc.y * 0.5) * size.height).toFixed(1));
        });
        scene.onDisposeObservable.add(function () {
            scene.onBeforeCameraRenderObservable.remove(observer);
        });
    };
})();
