package com.library.babylonnative.xr;

import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Choreographer;
import android.view.Surface;
import android.view.View;
import android.view.Window;

import androidx.annotation.Keep;
import androidx.annotation.Nullable;
import androidx.xr.runtime.Config;
import androidx.xr.runtime.FieldOfView;
import androidx.xr.runtime.Session;
import androidx.xr.runtime.SessionCreateApkRequired;
import androidx.xr.runtime.SessionCreateResult;
import androidx.xr.runtime.SessionCreateSuccess;
import androidx.xr.runtime.SessionCreateUnsupportedDevice;
import androidx.xr.runtime.SessionConfigureCalibrationRequired;
import androidx.xr.runtime.SessionConfigureGooglePlayServicesLocationLibraryNotLinked;
import androidx.xr.runtime.SessionConfigureResult;
import androidx.xr.runtime.SessionConfigureSuccess;
import androidx.xr.runtime.math.FloatSize2d;
import androidx.xr.runtime.math.FloatSize3d;
import androidx.xr.runtime.math.IntSize2d;
import androidx.xr.runtime.math.Pose;
import androidx.xr.runtime.math.Quaternion;
import androidx.xr.runtime.math.Vector3;
import androidx.xr.scenecore.CameraView;
import androidx.xr.scenecore.SpatialCapability;
import androidx.xr.scenecore.Space;
import androidx.xr.scenecore.Scene;
import androidx.xr.scenecore.SessionExt;
import androidx.xr.scenecore.SurfaceEntity;

import com.library.babylonnative.Wrapper;

import java.lang.ref.WeakReference;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executor;

/**
 * Jetpack XR + SceneCore bootstrap used by Android XR build variants to request
 * full-space mode and keep the system compositor informed about session lifecycle.
 */
@Keep
public final class XrSessionController {
    private static final String TAG = "BabylonXrSession";
    private static final int VIEW_DATA_FLOATS_PER_EYE = 11;
    private static final int VIEW_DATA_FLOATS = VIEW_DATA_FLOATS_PER_EYE * 2;

    // Conservative default per-eye resolution. The native runtime composes left+right side-by-side.
    private static final int DEFAULT_EYE_WIDTH_PX = 1024;
    private static final int DEFAULT_EYE_HEIGHT_PX = 1024;

    // SceneCore renders Surfaces as entities. To feel immersive, keep the surface head-locked and
    // sized to the current per-eye FOV so edges remain out of view.
    private static final float IMMERSIVE_SURFACE_DISTANCE_METERS = 1.0f;
    private static final float IMMERSIVE_SURFACE_FOV_PADDING = 1.15f;
    private static final float IMMERSIVE_SURFACE_MIN_SIZE_METERS = 0.25f;
    private static final float IMMERSIVE_SURFACE_MAX_SIZE_METERS = 50.0f;

    private static final float FALLBACK_IPD_METERS = 0.064f;
    private static final float FALLBACK_FOV_HALF_ANGLE_RADIANS = (float) Math.toRadians(55.0);
    private static final FieldOfView FALLBACK_FOV = new FieldOfView(
            -FALLBACK_FOV_HALF_ANGLE_RADIANS,
            FALLBACK_FOV_HALF_ANGLE_RADIANS,
            FALLBACK_FOV_HALF_ANGLE_RADIANS,
            -FALLBACK_FOV_HALF_ANGLE_RADIANS);

    private static WeakReference<Activity> currentActivityRef;
    private static boolean loggedDeviceInfo;

    private static @Nullable Session session;
    private static @Nullable Scene scene;
    private static @Nullable SurfaceEntity renderSurfaceEntity;
    private static @Nullable Surface renderSurface;
    private static int eyeWidthPx = DEFAULT_EYE_WIDTH_PX;
    private static int eyeHeightPx = DEFAULT_EYE_HEIGHT_PX;
    private static boolean immersiveSurfaceConfigured;
    private static boolean loggedFirstHeadLockUpdate;
    private static boolean loggedStereoViewDataReady;
    private static boolean loggedStereoViewDataUnavailable;
    private static boolean loggedCameraViewsUnavailable;
    private static boolean loggedCameraViewsException;
    private static boolean loggedFallbackViewData;
    private static boolean fullSpaceReady;
    private static boolean mainPanelHidden;
    private static boolean sessionConfigured;
    private static boolean stereoViewDataValid;
    private static final float[] stereoViewData = new float[VIEW_DATA_FLOATS];
    private static boolean frameUpdateRunning;
    private static final Choreographer.FrameCallback frameUpdateCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            if (!frameUpdateRunning) {
                return;
            }
            updateFromScene();
            Choreographer.getInstance().postFrameCallback(this);
        }
    };
    private static final Executor mainThreadExecutor = command -> {
        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(command);
    };

    private XrSessionController() {
    }

    public static void setCurrentActivity(Activity activity) {
        if (activity == null) {
            return;
        }

        currentActivityRef = new WeakReference<>(activity);
        logRuntimeContextOnce(activity);
        configureDecorForImmersive(activity);
        try {
            Wrapper.setCurrentActivity(activity);
        } catch (Throwable t) {
            Log.w(TAG, "Wrapper.setCurrentActivity failed", t);
        }
        ensureSession(activity);
    }

    public static boolean isSceneReady() {
        return session != null && scene != null;
    }

    public static boolean isFullSpaceReady() {
        return fullSpaceReady;
    }

    public static @Nullable Session getSession() {
        return session;
    }

    /**
     * Ensures a SceneCore {@link SurfaceEntity} exists and is wired to native rendering via JNI.
     * Intended to be called from native code when starting an immersive session.
     */
    public static boolean ensureRenderSurface(int desiredEyeWidthPx, int desiredEyeHeightPx) {
        if (desiredEyeWidthPx > 0) {
            eyeWidthPx = desiredEyeWidthPx;
        }
        if (desiredEyeHeightPx > 0) {
            eyeHeightPx = desiredEyeHeightPx;
        }

        Activity activity = getCurrentActivity();
        if (activity == null) {
            return false;
        }

        ensureSession(activity);
        return renderSurfaceEntity != null && renderSurface != null;
    }

    /**
     * Populates {@code outFloats} with 2x view data entries (left then right):
     * [posX,posY,posZ, quatX,quatY,quatZ,quatW, fovLeft,fovRight,fovUp,fovDown] per eye.
     */
    public static boolean getStereoViewData(float[] outFloats) {
        if (outFloats == null || outFloats.length < VIEW_DATA_FLOATS) {
            return false;
        }

        if (!stereoViewDataValid) {
            if (!loggedStereoViewDataUnavailable) {
                loggedStereoViewDataUnavailable = true;
                Log.i(TAG, "Stereo view data not ready; using fallback view parameters.");
            }
            return false;
        }

        synchronized (XrSessionController.class) {
            System.arraycopy(stereoViewData, 0, outFloats, 0, VIEW_DATA_FLOATS);
        }
        return true;
    }

    public static void notifySessionStarted() {
        requestFullSpaceMode("session-started");
    }

    public static void notifyFirstFrameSubmitted() {
        requestFullSpaceMode("first-frame");
    }

    public static void notifySessionStopped() {
        requestHomeSpaceMode("session-stopped");
    }

    public static void onActivityDestroyed(Activity activity) {
        if (currentActivityRef != null) {
            Activity existing = currentActivityRef.get();
            if (existing == activity) {
                shutdownSession();
                currentActivityRef.clear();
                currentActivityRef = null;
            }
        }
    }

    private static void shutdownSession() {
        immersiveSurfaceConfigured = false;
        loggedFirstHeadLockUpdate = false;
        loggedStereoViewDataReady = false;
        loggedStereoViewDataUnavailable = false;
        loggedCameraViewsUnavailable = false;
        loggedCameraViewsException = false;
        loggedFallbackViewData = false;
        fullSpaceReady = false;
        mainPanelHidden = false;
        sessionConfigured = false;
        stereoViewDataValid = false;
        stopFrameUpdates();

        if (renderSurface != null) {
            try {
                Wrapper.xrSurfaceChanged(null);
            } catch (Throwable t) {
                Log.w(TAG, "Wrapper.xrSurfaceChanged(null) failed", t);
            }
        }

        if (renderSurfaceEntity != null) {
            try {
                renderSurfaceEntity.dispose();
            } catch (Throwable t) {
                Log.w(TAG, "Failed to dispose SurfaceEntity.", t);
            }
            renderSurfaceEntity = null;
        }

        if (renderSurface != null) {
            try {
                renderSurface.release();
            } catch (Throwable t) {
                Log.w(TAG, "Failed to release render Surface.", t);
            }
            renderSurface = null;
        }

        if (scene != null) {
            try {
                scene.clearSpatialVisibilityChangedListener();
            } catch (Throwable t) {
                Log.w(TAG, "Failed to clear spatial visibility listener.", t);
            }
            try {
                scene.clearSpatialModeChangedListener();
            } catch (Throwable t) {
                Log.w(TAG, "Failed to clear spatial mode listener.", t);
            }
            try {
                scene.close();
            } catch (Throwable t) {
                Log.w(TAG, "Failed to close Scene.", t);
            }
            try {
                SessionExt.removeSceneFromCache(scene);
            } catch (Throwable t) {
                Log.w(TAG, "Failed to remove Scene from cache.", t);
            }
            scene = null;
        }

        session = null;
    }

    private static void ensureSession(Activity activity) {
        if (session != null && scene != null) {
            ensureRenderSurface(activity);
            return;
        }

        SessionCreateResult result;
        try {
            result = Session.create(activity);
        } catch (Throwable t) {
            Log.w(TAG, "Jetpack XR Session.create failed", t);
            return;
        }

        if (result instanceof SessionCreateSuccess) {
            session = ((SessionCreateSuccess) result).getSession();
            configureSessionForXr(session);
            scene = SessionExt.getScene(session);
            Log.i(TAG, "Jetpack XR session initialized.");
            try {
                Log.i(TAG, "Initial spatial capabilities: " + scene.getSpatialCapabilities());
            } catch (Throwable t) {
                Log.w(TAG, "Failed to query initial spatial capabilities.", t);
            }
            attachSceneDiagnostics(scene);
            requestFullSpaceMode("session-created");
            ensureRenderSurface(activity);
            updateFullSpaceReady(scene, null, null);
            return;
        }

        if (result instanceof SessionCreateUnsupportedDevice) {
            Log.w(TAG, "Jetpack XR unsupported device.");
            return;
        }

        if (result instanceof SessionCreateApkRequired) {
            String requiredApk = ((SessionCreateApkRequired) result).getRequiredApk();
            Log.w(TAG, "Jetpack XR requires APK: " + requiredApk);
            return;
        }

        Log.w(TAG, "Jetpack XR returned unexpected result: " + result.getClass().getSimpleName());
    }

    private static void configureSessionForXr(Session session) {
        if (sessionConfigured) {
            return;
        }
        sessionConfigured = true;

        try {
            Config current = session.getConfig();
            Config desired = current.copy(
                    current.getPlaneTracking(),
                    current.getHandTracking(),
                    Config.HeadTrackingMode.LAST_KNOWN,
                    current.getDepthEstimation(),
                    current.getAnchorPersistence());

            SessionConfigureResult configureResult = session.configure(desired);
            if (configureResult instanceof SessionConfigureSuccess) {
                Log.i(TAG, "Jetpack XR session configured (headTracking=LAST_KNOWN).");
                return;
            }

            if (configureResult instanceof SessionConfigureCalibrationRequired) {
                SessionConfigureCalibrationRequired calibrationRequired = (SessionConfigureCalibrationRequired) configureResult;
                Log.w(TAG, "Jetpack XR session config requires calibration: " + calibrationRequired.getCalibrationType());
                return;
            }

            if (configureResult instanceof SessionConfigureGooglePlayServicesLocationLibraryNotLinked) {
                Log.w(TAG, "Jetpack XR session config requires Google Play Services Location dependency.");
                return;
            }

            Log.w(TAG, "Jetpack XR session configure returned: " + configureResult.getClass().getSimpleName());
        } catch (Throwable t) {
            Log.w(TAG, "Jetpack XR session configure failed.", t);
        }
    }

    private static void ensureRenderSurface(Activity activity) {
        if (session == null || scene == null || renderSurfaceEntity != null) {
            return;
        }

        // Create a head-locked quad. Size and stereo view data are refined once camera views become available.
        try {
            Pose pose = new Pose(new Vector3(0f, 0f, -IMMERSIVE_SURFACE_DISTANCE_METERS), Quaternion.Identity);
            FloatSize2d initialSize = computeImmersiveQuadSizeMeters(FALLBACK_FOV, IMMERSIVE_SURFACE_DISTANCE_METERS);
            if (initialSize == null) {
                initialSize = new FloatSize2d(2.0f, 2.0f);
            }
            SurfaceEntity.Shape shape = new SurfaceEntity.Shape.Quad(initialSize);
            SurfaceEntity.StereoMode stereoMode = SurfaceEntity.StereoMode.SIDE_BY_SIDE;

            renderSurfaceEntity = SurfaceEntity.create(session, pose, shape, stereoMode);
            renderSurfaceEntity.setEdgeFeatheringParams(new SurfaceEntity.EdgeFeatheringParams.NoFeathering());
            try {
                renderSurfaceEntity.setParent(scene.getActivitySpace());
            } catch (Throwable t) {
                Log.w(TAG, "Failed to parent SurfaceEntity to ActivitySpace.", t);
            }
            try {
                scene.setKeyEntity(renderSurfaceEntity);
            } catch (Throwable t) {
                Log.w(TAG, "Failed to set key entity.", t);
            }
            hideMainPanel(scene);

            int fullWidth = eyeWidthPx * 2;
            int fullHeight = eyeHeightPx;
            renderSurfaceEntity.setSurfacePixelDimensions(new IntSize2d(fullWidth, fullHeight));

            renderSurface = renderSurfaceEntity.getSurface();
            Wrapper.xrSurfaceChanged(renderSurface);

            Log.i(TAG, "SceneCore render surface created. eye=" + eyeWidthPx + "x" + eyeHeightPx
                    + " full=" + fullWidth + "x" + fullHeight);
        } catch (Throwable t) {
            Log.w(TAG, "Failed to create SceneCore render surface.", t);
        }
    }

    private static void writeViewData(CameraView view, float[] out, int offset) {
        Pose pose = view.getActivitySpacePose();
        Vector3 translation = pose.getTranslation();
        Quaternion rotation = pose.getRotation();

        out[offset + 0] = translation.getX();
        out[offset + 1] = translation.getY();
        out[offset + 2] = translation.getZ();
        out[offset + 3] = rotation.getX();
        out[offset + 4] = rotation.getY();
        out[offset + 5] = rotation.getZ();
        out[offset + 6] = rotation.getW();

        androidx.xr.runtime.FieldOfView fov = view.getFov();
        out[offset + 7] = fov.getAngleLeft();
        out[offset + 8] = fov.getAngleRight();
        out[offset + 9] = fov.getAngleUp();
        out[offset + 10] = fov.getAngleDown();
    }

    private static void requestFullSpaceMode(String reason) {
        Scene current = scene;
        Activity activity = getCurrentActivity();
        if (activity != null) {
            configureDecorForImmersive(activity);
        }

        if (current == null) {
            Log.w(TAG, "requestFullSpaceMode ignored; no Scene available. reason=" + reason);
            return;
        }

        Log.i(TAG, "Requesting full-space mode. reason=" + reason);
        current.requestFullSpaceMode();
    }

    private static void requestHomeSpaceMode(String reason) {
        Scene current = scene;
        if (current == null) {
            Log.i(TAG, "requestHomeSpaceMode ignored; no Scene available. reason=" + reason);
            return;
        }

        Log.i(TAG, "Requesting home-space mode. reason=" + reason);
        current.requestHomeSpaceMode();
    }

    private static void hideMainPanel(Scene currentScene) {
        if (mainPanelHidden || currentScene == null) {
            return;
        }

        try {
            currentScene.getMainPanelEntity().setEnabled(false);
            mainPanelHidden = true;
            Log.i(TAG, "Main panel hidden.");
        } catch (Throwable t) {
            Log.w(TAG, "Failed to hide main panel.", t);
        }
    }

    private static @Nullable Activity getCurrentActivity() {
        return currentActivityRef != null ? currentActivityRef.get() : null;
    }

    private static void attachSceneDiagnostics(Scene scene) {
        try {
            scene.addSpatialCapabilitiesChangedListener(mainThreadExecutor, capabilities -> {
                Log.i(TAG, "Spatial capabilities changed: " + capabilities);
                updateFullSpaceReady(scene, capabilities, null);
            });
        } catch (Throwable t) {
            Log.w(TAG, "Failed to attach spatial capabilities listener.", t);
        }

        try {
            scene.getActivitySpace().addOnBoundsChangedListener(mainThreadExecutor, bounds -> {
                Log.i(TAG, "ActivitySpace bounds changed: " + bounds);
                updateFullSpaceReady(scene, null, bounds);
            });
        } catch (Throwable t) {
            Log.w(TAG, "Failed to attach ActivitySpace bounds listener.", t);
        }

        try {
            scene.setSpatialVisibilityChangedListener(mainThreadExecutor, visibility ->
                    Log.i(TAG, "Spatial visibility: " + visibility));
        } catch (Throwable t) {
            Log.w(TAG, "Failed to attach spatial visibility listener.", t);
        }

        try {
            scene.setSpatialModeChangedListener(mainThreadExecutor, event ->
                    Log.i(TAG, "Spatial mode change event: " + event));
        } catch (Throwable t) {
            Log.w(TAG, "Failed to attach spatial mode listener.", t);
        }
    }

    private static void startFrameUpdates() {
        if (frameUpdateRunning) {
            return;
        }

        frameUpdateRunning = true;
        mainThreadExecutor.execute(() -> Choreographer.getInstance().postFrameCallback(frameUpdateCallback));
    }

    private static void stopFrameUpdates() {
        if (!frameUpdateRunning) {
            return;
        }

        frameUpdateRunning = false;
        mainThreadExecutor.execute(() -> Choreographer.getInstance().removeFrameCallback(frameUpdateCallback));
    }

    private static void updateFromScene() {
        Scene activeScene = scene;
        if (activeScene == null) {
            return;
        }

        Pose headPose = null;
        try {
            headPose = activeScene.getSpatialUser().getHead().getActivitySpacePose();
        } catch (Throwable t) {
            headPose = null;
        }

        SurfaceEntity entity = renderSurfaceEntity;
        if (entity != null && headPose != null) {
            try {
                Pose targetPose = computeHeadLockedPose(headPose, IMMERSIVE_SURFACE_DISTANCE_METERS);
                entity.setPose(targetPose, Space.ACTIVITY);
                if (!loggedFirstHeadLockUpdate) {
                    loggedFirstHeadLockUpdate = true;
                    Log.i(TAG, "Head-locked surface updates active.");
                }
            } catch (Throwable ignored) {
                // Ignore; we'll still provide fallback view data.
            }
        }

        CameraView leftView = null;
        CameraView rightView = null;
        Map<CameraView.CameraType, CameraView> cameraViews = null;
        boolean cameraViewReady = false;
        try {
            cameraViews = activeScene.getSpatialUser().getCameraViews();
            leftView = cameraViews.get(CameraView.CameraType.LEFT_EYE);
            rightView = cameraViews.get(CameraView.CameraType.RIGHT_EYE);
            cameraViewReady = leftView != null && rightView != null;
        } catch (Throwable t) {
            if (!loggedCameraViewsException) {
                loggedCameraViewsException = true;
                try {
                    Log.i(TAG, "Failed to query camera views. capabilities=" + activeScene.getSpatialCapabilities(), t);
                } catch (Throwable inner) {
                    Log.i(TAG, "Failed to query camera views.", t);
                }
            }
        }

        synchronized (XrSessionController.class) {
            if (cameraViewReady) {
                writeViewData(leftView, stereoViewData, 0);
                writeViewData(rightView, stereoViewData, VIEW_DATA_FLOATS_PER_EYE);
            } else {
                Pose fallbackHeadPose = headPose;
                if (fallbackHeadPose == null) {
                    fallbackHeadPose = new Pose(new Vector3(0f, 1.6f, 0f), Quaternion.Identity);
                }
                writeFallbackViewData(fallbackHeadPose, stereoViewData, 0, -FALLBACK_IPD_METERS * 0.5f);
                writeFallbackViewData(fallbackHeadPose, stereoViewData, VIEW_DATA_FLOATS_PER_EYE, FALLBACK_IPD_METERS * 0.5f);
                if (!loggedFallbackViewData) {
                    loggedFallbackViewData = true;
                    if (cameraViews == null) {
                        Log.i(TAG, "Using head-pose fallback view data (camera views unavailable).");
                    } else {
                        Log.i(TAG, "Using head-pose fallback view data (camera view keys=" + cameraViews.keySet() + ").");
                    }
                }
            }
            stereoViewDataValid = true;
        }

        updateFullSpaceReady(activeScene, null, null);

        if (cameraViewReady) {
            if (!loggedStereoViewDataReady) {
                loggedStereoViewDataReady = true;
                Log.i(TAG, "Stereo view data ready.");
            }

            if (entity != null && !immersiveSurfaceConfigured) {
                FloatSize2d size = computeImmersiveQuadSizeMeters(leftView, rightView, IMMERSIVE_SURFACE_DISTANCE_METERS);
                if (size != null) {
                    try {
                        entity.setShape(new SurfaceEntity.Shape.Quad(size));
                        immersiveSurfaceConfigured = true;
                        Log.i(TAG, "Configured immersive surface size: " + size);
                    } catch (Throwable t) {
                        Log.w(TAG, "Failed to set SurfaceEntity shape.", t);
                    }
                }
            }
        } else if (!loggedCameraViewsUnavailable && cameraViews != null) {
            loggedCameraViewsUnavailable = true;
            Log.i(TAG, "Camera views unavailable. keys=" + cameraViews.keySet());
        }
    }

    private static void updateFullSpaceReady(Scene activeScene, @Nullable Set<SpatialCapability> capabilitiesOverride, @Nullable FloatSize3d boundsOverride) {
        Set<SpatialCapability> capabilities = capabilitiesOverride;
        FloatSize3d bounds = boundsOverride;
        try {
            if (capabilities == null) {
                capabilities = activeScene.getSpatialCapabilities();
            }
            if (bounds == null) {
                bounds = activeScene.getActivitySpace().getBounds();
            }
        } catch (Throwable ignored) {
            return;
        }

        boolean has3dContent = capabilities.contains(SpatialCapability.SPATIAL_3D_CONTENT);
        boolean infiniteBounds = Float.isInfinite(bounds.getWidth()) || Float.isInfinite(bounds.getHeight()) || Float.isInfinite(bounds.getDepth());
        boolean ready = has3dContent && infiniteBounds;

        if (ready != fullSpaceReady) {
            fullSpaceReady = ready;
            Log.i(TAG, "Full-space ready: " + fullSpaceReady + " caps=" + capabilities + " bounds=" + bounds);
        }

        // Avoid querying head pose and camera views until the system transitions into full-space mode.
        // This prevents noisy Perception/OpenXR warnings on startup on the Android XR emulator.
        if (ready && !frameUpdateRunning) {
            startFrameUpdates();
        }
    }

    private static @Nullable FloatSize2d computeImmersiveQuadSizeMeters(CameraView leftView, CameraView rightView, float distanceMeters) {
        if (leftView == null || rightView == null) {
            return null;
        }

        FieldOfView leftFov = leftView.getFov();
        FieldOfView rightFov = rightView.getFov();

        float width = Math.max(computeQuadWidthMeters(leftFov, distanceMeters), computeQuadWidthMeters(rightFov, distanceMeters));
        float height = Math.max(computeQuadHeightMeters(leftFov, distanceMeters), computeQuadHeightMeters(rightFov, distanceMeters));

        if (!isFinitePositive(width) || !isFinitePositive(height)) {
            return null;
        }

        width *= IMMERSIVE_SURFACE_FOV_PADDING;
        height *= IMMERSIVE_SURFACE_FOV_PADDING;

        width = clamp(width, IMMERSIVE_SURFACE_MIN_SIZE_METERS, IMMERSIVE_SURFACE_MAX_SIZE_METERS);
        height = clamp(height, IMMERSIVE_SURFACE_MIN_SIZE_METERS, IMMERSIVE_SURFACE_MAX_SIZE_METERS);
        return new FloatSize2d(width, height);
    }

    private static @Nullable FloatSize2d computeImmersiveQuadSizeMeters(FieldOfView fov, float distanceMeters) {
        float width = computeQuadWidthMeters(fov, distanceMeters);
        float height = computeQuadHeightMeters(fov, distanceMeters);
        if (!isFinitePositive(width) || !isFinitePositive(height)) {
            return null;
        }

        width *= IMMERSIVE_SURFACE_FOV_PADDING;
        height *= IMMERSIVE_SURFACE_FOV_PADDING;

        width = clamp(width, IMMERSIVE_SURFACE_MIN_SIZE_METERS, IMMERSIVE_SURFACE_MAX_SIZE_METERS);
        height = clamp(height, IMMERSIVE_SURFACE_MIN_SIZE_METERS, IMMERSIVE_SURFACE_MAX_SIZE_METERS);
        return new FloatSize2d(width, height);
    }

    private static void writeFallbackViewData(Pose headPose, float[] out, int offset, float localEyeOffsetMeters) {
        Vector3 headTranslation = headPose.getTranslation();
        Quaternion headRotation = headPose.getRotation();

        Vector3 localOffset = new Vector3(localEyeOffsetMeters, 0f, 0f);
        Vector3 worldOffset = rotateVector(headRotation, localOffset);
        Vector3 eyeTranslation = new Vector3(
                headTranslation.getX() + worldOffset.getX(),
                headTranslation.getY() + worldOffset.getY(),
                headTranslation.getZ() + worldOffset.getZ());

        out[offset + 0] = eyeTranslation.getX();
        out[offset + 1] = eyeTranslation.getY();
        out[offset + 2] = eyeTranslation.getZ();
        out[offset + 3] = headRotation.getX();
        out[offset + 4] = headRotation.getY();
        out[offset + 5] = headRotation.getZ();
        out[offset + 6] = headRotation.getW();

        out[offset + 7] = FALLBACK_FOV.getAngleLeft();
        out[offset + 8] = FALLBACK_FOV.getAngleRight();
        out[offset + 9] = FALLBACK_FOV.getAngleUp();
        out[offset + 10] = FALLBACK_FOV.getAngleDown();
    }

    private static float computeQuadWidthMeters(FieldOfView fov, float distanceMeters) {
        double left = Math.tan(fov.getAngleLeft());
        double right = Math.tan(fov.getAngleRight());
        return (float) ((right - left) * distanceMeters);
    }

    private static float computeQuadHeightMeters(FieldOfView fov, float distanceMeters) {
        double down = Math.tan(fov.getAngleDown());
        double up = Math.tan(fov.getAngleUp());
        return (float) ((up - down) * distanceMeters);
    }

    private static Pose computeHeadLockedPose(Pose headPose, float distanceMeters) {
        Vector3 translation = headPose.getTranslation();
        Quaternion rotation = headPose.getRotation();
        Vector3 forward = rotateVector(rotation, new Vector3(0f, 0f, -1f));
        Vector3 offset = new Vector3(forward.getX() * distanceMeters, forward.getY() * distanceMeters, forward.getZ() * distanceMeters);
        Vector3 targetTranslation = new Vector3(
                translation.getX() + offset.getX(),
                translation.getY() + offset.getY(),
                translation.getZ() + offset.getZ());
        return new Pose(targetTranslation, rotation);
    }

    private static Vector3 rotateVector(Quaternion rotation, Vector3 vector) {
        float qx = rotation.getX();
        float qy = rotation.getY();
        float qz = rotation.getZ();
        float qw = rotation.getW();

        float vx = vector.getX();
        float vy = vector.getY();
        float vz = vector.getZ();

        float tx = 2f * (qy * vz - qz * vy);
        float ty = 2f * (qz * vx - qx * vz);
        float tz = 2f * (qx * vy - qy * vx);

        return new Vector3(
                vx + qw * tx + (qy * tz - qz * ty),
                vy + qw * ty + (qz * tx - qx * tz),
                vz + qw * tz + (qx * ty - qy * tx));
    }

    private static boolean isFinitePositive(float value) {
        return !Float.isNaN(value) && !Float.isInfinite(value) && value > 0f;
    }

    private static float clamp(float value, float minValue, float maxValue) {
        if (value < minValue) {
            return minValue;
        }
        if (value > maxValue) {
            return maxValue;
        }
        return value;
    }

    @SuppressWarnings("deprecation")
    private static void configureDecorForImmersive(Activity activity) {
        Window window = activity.getWindow();
        if (window == null) {
            return;
        }

        View decor = window.peekDecorView();
        if (decor == null) {
            return;
        }

        int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
        decor.setSystemUiVisibility(flags);
    }

    private static void logRuntimeContextOnce(Activity activity) {
        if (loggedDeviceInfo) {
            return;
        }

        loggedDeviceInfo = true;

        Context context = activity.getApplicationContext();
        PackageManager pm = context.getPackageManager();
        boolean supportsOpenXr = pm.hasSystemFeature("android.software.xr.api.openxr");
        boolean supportsSpatial = pm.hasSystemFeature("android.software.xr.api.spatial");

        Log.i(TAG, "Android XR feature flags: openxr=" + supportsOpenXr
                + " spatial=" + supportsSpatial
                + " brand=" + Build.BRAND
                + " model=" + Build.MODEL
                + " sdk=" + Build.VERSION.SDK_INT);
    }
}
