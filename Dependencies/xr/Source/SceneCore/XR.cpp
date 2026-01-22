#include <XR.h>
#include <XRHelpers.h>

#include <AndroidExtensions/Globals.h>
#include <android/log.h>

#include <cmath>
#include <mutex>
#include <optional>
#include <stdexcept>

#include "Include/IXrContextSceneCore.h"

using namespace android::global;

namespace xr
{
    namespace
    {
        constexpr const char* TAG = "BabylonNativeXR";
        constexpr size_t VIEW_COUNT = 2;
        constexpr size_t VIEW_DATA_FLOATS_PER_EYE = 11;
        constexpr size_t VIEW_DATA_FLOATS = VIEW_DATA_FLOATS_PER_EYE * VIEW_COUNT;
        constexpr int DEFAULT_EYE_WIDTH_PX = 1024;
        constexpr int DEFAULT_EYE_HEIGHT_PX = 1024;
        constexpr float DEFAULT_IPD_M = 0.064f;
        constexpr float DEFAULT_FOV_ANGLE_RAD = 0.55f;

        void PopulateProjectionMatrix(float angleLeft, float angleRight, float angleUp, float angleDown, float nearZ, float farZ, std::array<float, 16>& out)
        {
            const float tanLeft = std::tan(angleLeft);
            const float tanRight = std::tan(angleRight);
            const float tanDown = std::tan(angleDown);
            const float tanUp = std::tan(angleUp);

            const float tanWidth = tanRight - tanLeft;
            const float tanHeight = tanUp - tanDown;

            out = {};
            out[0] = 2.0f / tanWidth;
            out[5] = 2.0f / tanHeight;
            out[8] = (tanRight + tanLeft) / tanWidth;
            out[9] = (tanUp + tanDown) / tanHeight;
            out[10] = -(farZ + nearZ) / (farZ - nearZ);
            out[11] = -1.0f;
            out[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        }

        [[nodiscard]] bool ClearIfJavaException(JNIEnv* env, const char* context)
        {
            if (!env->ExceptionCheck())
            {
                return false;
            }

            env->ExceptionDescribe();
            env->ExceptionClear();
            __android_log_print(ANDROID_LOG_WARN, TAG, "JNI exception in %s", context);
            return true;
        }

        jclass LoadAppClass(JNIEnv* env, const char* className)
        {
            android::app::Activity activity = GetCurrentActivity();
            jobject activityObj = static_cast<jobject>(activity);
            if (!activityObj)
            {
                __android_log_print(ANDROID_LOG_WARN, TAG, "LoadAppClass(%s) no current activity.", className);
                return nullptr;
            }

            jclass activityClass = env->GetObjectClass(activityObj);
            jmethodID getClassLoader = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
            jobject classLoader = env->CallObjectMethod(activityObj, getClassLoader);
            env->DeleteLocalRef(activityClass);
            if (ClearIfJavaException(env, "Activity.getClassLoader"))
            {
                if (classLoader)
                {
                    env->DeleteLocalRef(classLoader);
                }
                return nullptr;
            }

            jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
            jmethodID loadClass = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            jstring name = env->NewStringUTF(className);
            auto clazzObj = static_cast<jclass>(env->CallObjectMethod(classLoader, loadClass, name));
            env->DeleteLocalRef(name);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(classLoaderClass);
            if (ClearIfJavaException(env, "ClassLoader.loadClass"))
            {
                return nullptr;
            }

            return clazzObj;
        }

        struct SceneCoreControllerJni
        {
            jclass clazz{};
            jmethodID isSceneReady{};
            jmethodID isFullSpaceReady{};
            jmethodID ensureRenderSurface{};
            jmethodID getStereoViewData{};
            jmethodID notifyFirstFrameSubmitted{};
        };

        SceneCoreControllerJni& GetSceneCoreControllerJni(JNIEnv* env)
        {
            static std::mutex s_mutex;
            static SceneCoreControllerJni s_jni;
            static bool s_initialized = false;

            std::lock_guard<std::mutex> lock{s_mutex};
            if (s_initialized)
            {
                return s_jni;
            }

            constexpr const char* kControllerClass = "com.library.babylonnative.xr.XrSessionController";

            jclass localClass = LoadAppClass(env, kControllerClass);
            if (!localClass)
            {
                throw std::runtime_error{"Failed to load XrSessionController class."};
            }

            s_jni.clazz = static_cast<jclass>(env->NewGlobalRef(localClass));
            env->DeleteLocalRef(localClass);

            s_jni.isSceneReady = env->GetStaticMethodID(s_jni.clazz, "isSceneReady", "()Z");
            s_jni.isFullSpaceReady = env->GetStaticMethodID(s_jni.clazz, "isFullSpaceReady", "()Z");
            s_jni.ensureRenderSurface = env->GetStaticMethodID(s_jni.clazz, "ensureRenderSurface", "(II)Z");
            s_jni.getStereoViewData = env->GetStaticMethodID(s_jni.clazz, "getStereoViewData", "([F)Z");
            s_jni.notifyFirstFrameSubmitted = env->GetStaticMethodID(s_jni.clazz, "notifyFirstFrameSubmitted", "()V");

            if (ClearIfJavaException(env, "GetStaticMethodID"))
            {
                throw std::runtime_error{"Failed to resolve SceneCore JNI methods."};
            }

            s_initialized = true;
            return s_jni;
        }
    }

    struct XrContextSceneCore : IXrContextSceneCore
    {
        bool Initialized{false};

        bool IsInitialized() const override
        {
            return Initialized;
        }

        ~XrContextSceneCore() override = default;
    };

    struct System::Impl
    {
        std::shared_ptr<XrContextSceneCore> XrContext{std::make_shared<XrContextSceneCore>()};
        RuntimeCapabilities Capabilities{};

        Impl(const std::string& /*applicationName*/)
        {
            Capabilities.SupportsImmersiveVR = true;
            Capabilities.SupportsImmersiveAR = false;
            Capabilities.SupportsInline = false;

            Capabilities.SupportsAnchors = false;
            Capabilities.SupportsPlaneDetection = false;
            Capabilities.SupportsMeshDetection = false;
            Capabilities.SupportsHitTest = false;
            Capabilities.SupportsImageTracking = false;
            Capabilities.SupportsLightEstimation = false;
            Capabilities.SupportsPassthrough = false;

            Capabilities.SupportsVulkan = true;
            Capabilities.SupportsOpenGLES = true;
            Capabilities.SupportedColorFormats = {TextureFormat::RGBA8_SRGB, TextureFormat::BGRA8_SRGB};
            Capabilities.SupportedDepthFormats = {TextureFormat::D24S8, TextureFormat::D16};
        }

        bool IsInitialized() const
        {
            return true;
        }

        bool TryInitialize()
        {
            return true;
        }
    };

    struct System::Session::Impl
    {
        explicit Impl(System::Impl& systemImpl)
            : SystemImpl{systemImpl}
        {
        }

        ~Impl()
        {
            if (ViewDataArray != nullptr)
            {
                JNIEnv* env = GetEnvForCurrentThread();
                env->DeleteGlobalRef(ViewDataArray);
                ViewDataArray = nullptr;
            }
        }

        System::Impl& SystemImpl;
        bool SessionEnded{false};
        bool FirstFrameNotified{false};
        bool TrackingValid{false};

        float DepthNearZ{System::DEFAULT_DEPTH_NEAR_Z};
        float DepthFarZ{System::DEFAULT_DEPTH_FAR_Z};

        int EyeWidthPx{DEFAULT_EYE_WIDTH_PX};
        int EyeHeightPx{DEFAULT_EYE_HEIGHT_PX};

        std::vector<Frame::View> ActiveFrameViews{VIEW_COUNT};
        std::vector<Frame::InputSource> InputSources{};
        std::vector<FeaturePoint> FeaturePointCloud{};
        std::optional<Space> EyeTrackerSpace{};

        std::vector<Frame::SceneObject::Identifier> UpdatedSceneObjects{};
        std::vector<Frame::SceneObject::Identifier> RemovedSceneObjects{};
        std::vector<Frame::Plane::Identifier> UpdatedPlanes{};
        std::vector<Frame::Plane::Identifier> RemovedPlanes{};
        std::vector<Frame::Mesh::Identifier> UpdatedMeshes{};
        std::vector<Frame::Mesh::Identifier> RemovedMeshes{};
        std::vector<Frame::ImageTrackingResult::Identifier> UpdatedImageTrackingResults{};
        std::vector<uint8_t> CameraImageRGBA{};
        size_t CameraImageWidth{0};
        size_t CameraImageHeight{0};

        jfloatArray ViewDataArray{};

        void Initialize()
        {
            if (SystemImpl.XrContext->Initialized)
            {
                return;
            }

            JNIEnv* env = GetEnvForCurrentThread();
            auto& jni = GetSceneCoreControllerJni(env);

            // Ask Java to create (or reuse) a SurfaceEntity and wire its Surface to native via Wrapper.xrSurfaceChanged.
            const auto ensured = env->CallStaticBooleanMethod(jni.clazz, jni.ensureRenderSurface, EyeWidthPx, EyeHeightPx);
            if (ClearIfJavaException(env, "ensureRenderSurface"))
            {
                throw std::runtime_error{"SceneCore ensureRenderSurface threw an exception."};
            }
            if (ensured == JNI_FALSE)
            {
                throw std::runtime_error{"SceneCore render surface unavailable."};
            }

            // Allocate reusable float[] buffer for view data updates.
            auto localArray = env->NewFloatArray(static_cast<jsize>(VIEW_DATA_FLOATS));
            ViewDataArray = static_cast<jfloatArray>(env->NewGlobalRef(localArray));
            env->DeleteLocalRef(localArray);

            SystemImpl.XrContext->Initialized = true;
        }

        bool IsTracking()
        {
            return TrackingValid;
        }

        void UpdateViews()
        {
            JNIEnv* env = GetEnvForCurrentThread();
            auto& jni = GetSceneCoreControllerJni(env);

            const auto ok = env->CallStaticBooleanMethod(jni.clazz, jni.getStereoViewData, ViewDataArray);
            if (ClearIfJavaException(env, "getStereoViewData") || ok == JNI_FALSE)
            {
                // Head tracking (and thus camera views) can be unavailable transiently on the Android XR emulator.
                // Provide a stable fallback pose + projection so WebXR can still render content.
                TrackingValid = true;

                const float eyeOffset = DEFAULT_IPD_M * 0.5f;
                const float angleLeft = -DEFAULT_FOV_ANGLE_RAD;
                const float angleRight = DEFAULT_FOV_ANGLE_RAD;
                const float angleUp = DEFAULT_FOV_ANGLE_RAD;
                const float angleDown = -DEFAULT_FOV_ANGLE_RAD;

                for (size_t eye = 0; eye < VIEW_COUNT; ++eye)
                {
                    auto& view = ActiveFrameViews[eye];
                    view.Space.Pose.Position.X = eye == 0 ? -eyeOffset : eyeOffset;
                    view.Space.Pose.Position.Y = 1.6f;
                    view.Space.Pose.Position.Z = 0.0f;
                    view.Space.Pose.Orientation.X = 0.0f;
                    view.Space.Pose.Orientation.Y = 0.0f;
                    view.Space.Pose.Orientation.Z = 0.0f;
                    view.Space.Pose.Orientation.W = 1.0f;

                    PopulateProjectionMatrix(angleLeft, angleRight, angleUp, angleDown, DepthNearZ, DepthFarZ, view.ProjectionMatrix);

                    const auto fullWidth = static_cast<size_t>(EyeWidthPx) * VIEW_COUNT;
                    const auto fullHeight = static_cast<size_t>(EyeHeightPx);

                    view.ColorTextureFormat = TextureFormat::RGBA8_SRGB;
                    view.ColorTexturePointer = nullptr;
                    view.ColorTextureSize = {fullWidth, fullHeight, 1};

                    view.DepthTextureFormat = TextureFormat::D24S8;
                    view.DepthTexturePointer = nullptr;
                    view.DepthTextureSize = {fullWidth, fullHeight, 1};

                    view.DepthNearZ = DepthNearZ;
                    view.DepthFarZ = DepthFarZ;

                    view.IsFirstPersonObserver = false;
                    view.RequiresAppClear = true;
                }

                return;
            }

            TrackingValid = true;

            jboolean isCopy{};
            jfloat* values = env->GetFloatArrayElements(ViewDataArray, &isCopy);
            if (!values)
            {
                TrackingValid = false;
                return;
            }

            for (size_t eye = 0; eye < VIEW_COUNT; ++eye)
            {
                const size_t base = eye * VIEW_DATA_FLOATS_PER_EYE;
                auto& view = ActiveFrameViews[eye];

                view.Space.Pose.Position.X = values[base + 0];
                view.Space.Pose.Position.Y = values[base + 1];
                view.Space.Pose.Position.Z = values[base + 2];
                view.Space.Pose.Orientation.X = values[base + 3];
                view.Space.Pose.Orientation.Y = values[base + 4];
                view.Space.Pose.Orientation.Z = values[base + 5];
                view.Space.Pose.Orientation.W = values[base + 6];

                const float angleLeft = values[base + 7];
                const float angleRight = values[base + 8];
                const float angleUp = values[base + 9];
                const float angleDown = values[base + 10];

                PopulateProjectionMatrix(angleLeft, angleRight, angleUp, angleDown, DepthNearZ, DepthFarZ, view.ProjectionMatrix);

                const auto fullWidth = static_cast<size_t>(EyeWidthPx) * VIEW_COUNT;
                const auto fullHeight = static_cast<size_t>(EyeHeightPx);

                view.ColorTextureFormat = TextureFormat::RGBA8_SRGB;
                view.ColorTexturePointer = nullptr;
                view.ColorTextureSize = {fullWidth, fullHeight, 1};

                view.DepthTextureFormat = TextureFormat::D24S8;
                view.DepthTexturePointer = nullptr;
                view.DepthTextureSize = {fullWidth, fullHeight, 1};

                view.DepthNearZ = DepthNearZ;
                view.DepthFarZ = DepthFarZ;

                view.IsFirstPersonObserver = false;
                view.RequiresAppClear = true;
            }

            env->ReleaseFloatArrayElements(ViewDataArray, values, JNI_ABORT);
        }

        std::unique_ptr<Session::Frame> GetNextFrame(bool& shouldEndSession, bool& shouldRestartSession)
        {
            if (!SystemImpl.XrContext->Initialized)
            {
                Initialize();
            }

            shouldEndSession = SessionEnded;
            shouldRestartSession = false;

            // Refresh view data for this frame.
            UpdateViews();

            return std::make_unique<Session::Frame>(*this);
        }

        void RequestEndSession()
        {
            SessionEnded = true;
        }

        void DrawFrame()
        {
            if (FirstFrameNotified)
            {
                return;
            }

            FirstFrameNotified = true;

            JNIEnv* env = GetEnvForCurrentThread();
            auto& jni = GetSceneCoreControllerJni(env);
            env->CallStaticVoidMethod(jni.clazz, jni.notifyFirstFrameSubmitted);
            (void)ClearIfJavaException(env, "notifyFirstFrameSubmitted");
        }
    };

    struct System::Session::Frame::Impl
    {
        explicit Impl(System::Session::Impl& sessionImpl)
            : SessionImpl{sessionImpl}
        {
        }

        System::Session::Impl& SessionImpl;
    };

    System::Session::Frame::Frame(System::Session::Impl& sessionImpl)
        : Views{sessionImpl.ActiveFrameViews}
        , InputSources{sessionImpl.InputSources}
        , FeaturePointCloud{sessionImpl.FeaturePointCloud}
        , EyeTrackerSpace{sessionImpl.EyeTrackerSpace}
        , UpdatedSceneObjects{sessionImpl.UpdatedSceneObjects}
        , RemovedSceneObjects{sessionImpl.RemovedSceneObjects}
        , UpdatedPlanes{sessionImpl.UpdatedPlanes}
        , RemovedPlanes{sessionImpl.RemovedPlanes}
        , UpdatedMeshes{sessionImpl.UpdatedMeshes}
        , RemovedMeshes{sessionImpl.RemovedMeshes}
        , UpdatedImageTrackingResults{sessionImpl.UpdatedImageTrackingResults}
        , CameraImageRGBA{sessionImpl.CameraImageRGBA}
        , CameraImageWidth{sessionImpl.CameraImageWidth}
        , CameraImageHeight{sessionImpl.CameraImageHeight}
        , IsTracking{sessionImpl.IsTracking()}
        , m_impl{std::make_unique<System::Session::Frame::Impl>(sessionImpl)}
    {
    }

    System::Session::Frame::~Frame() = default;

    void System::Session::Frame::GetHitTestResults(std::vector<HitResult>&, Ray, HitTestTrackableType) const
    {
        // Hit testing is not supported in SceneCore-backed immersive VR at this time.
    }

    Anchor System::Session::Frame::CreateAnchor(Pose, NativeTrackablePtr) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    Anchor System::Session::Frame::DeclareAnchor(NativeAnchorPtr) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    void System::Session::Frame::UpdateAnchor(xr::Anchor&) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    void System::Session::Frame::DeleteAnchor(xr::Anchor&) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    System::Session::Frame::SceneObject& System::Session::Frame::GetSceneObjectByID(System::Session::Frame::SceneObject::Identifier) const
    {
        throw std::runtime_error{"Scene objects are not supported on this platform."};
    }

    System::Session::Frame::Plane& System::Session::Frame::GetPlaneByID(System::Session::Frame::Plane::Identifier) const
    {
        throw std::runtime_error{"Plane detection is not supported on this platform."};
    }

    System::Session::Frame::Mesh& System::Session::Frame::GetMeshByID(System::Session::Frame::Mesh::Identifier) const
    {
        throw std::runtime_error{"Mesh detection is not supported on this platform."};
    }

    System::Session::Frame::ImageTrackingResult& System::Session::Frame::GetImageTrackingResultByID(System::Session::Frame::ImageTrackingResult::Identifier) const
    {
        throw std::runtime_error{"Image tracking is not supported on this platform."};
    }

    void System::Session::Frame::Render()
    {
        m_impl->SessionImpl.DrawFrame();
    }

    System::System(const char* appName)
        : m_impl{std::make_unique<System::Impl>(appName)}
    {
    }

    System::~System() = default;

    bool System::IsInitialized() const
    {
        return m_impl->IsInitialized();
    }

    bool System::TryInitialize()
    {
        return m_impl->TryInitialize();
    }

    arcana::task<bool, std::exception_ptr> System::IsSessionSupportedAsync(SessionType sessionType)
    {
        if (sessionType != SessionType::IMMERSIVE_VR)
        {
            return arcana::task_from_result<std::exception_ptr>(false);
        }

        try
        {
            JNIEnv* env = GetEnvForCurrentThread();
            auto& jni = GetSceneCoreControllerJni(env);
            // SceneCore-based XR support is available as soon as the Jetpack XR session + scene are initialized.
            // Full-space mode and tracking readiness can take additional time on the emulator; do not gate WebXR
            // session support on that state so JS can start the immersive session and drive the transition.
            const auto ready = env->CallStaticBooleanMethod(jni.clazz, jni.isSceneReady);
            if (ClearIfJavaException(env, "isSceneReady"))
            {
                return arcana::task_from_result<std::exception_ptr>(false);
            }
            return arcana::task_from_result<std::exception_ptr>(ready == JNI_TRUE);
        }
        catch (...)
        {
            return arcana::task_from_result<std::exception_ptr>(false);
        }
    }

    uintptr_t System::GetNativeXrContext()
    {
        return reinterpret_cast<uintptr_t>(m_impl->XrContext.get());
    }

    std::string System::GetNativeXrContextType()
    {
        return "SceneCore";
    }

    const RuntimeCapabilities& System::GetCapabilities() const
    {
        return m_impl->Capabilities;
    }

    arcana::task<std::shared_ptr<System::Session>, std::exception_ptr> System::Session::CreateAsync(
        System& system,
        void* graphicsDevice,
        void* commandQueue,
        std::function<void*()> windowProvider)
    {
        (void)graphicsDevice;
        (void)commandQueue;
        (void)windowProvider;
        return arcana::task_from_result<std::exception_ptr>(std::make_shared<System::Session>(system, graphicsDevice, commandQueue, std::move(windowProvider)));
    }

    System::Session::Session(System& system, void*, void*, std::function<void*()> /*windowProvider*/)
        : m_impl{std::make_unique<System::Session::Impl>(*system.m_impl)}
    {
    }

    System::Session::~Session() = default;

    std::unique_ptr<System::Session::Frame> System::Session::GetNextFrame(
        bool& shouldEndSession,
        bool& shouldRestartSession,
        std::function<arcana::task<void, std::exception_ptr>(void*)> /*deletedTextureAsyncCallback*/)
    {
        return m_impl->GetNextFrame(shouldEndSession, shouldRestartSession);
    }

    void System::Session::RequestEndSession()
    {
        m_impl->RequestEndSession();
    }

    void System::Session::SetDepthsNearFar(float depthNear, float depthFar)
    {
        m_impl->DepthNearZ = depthNear;
        m_impl->DepthFarZ = depthFar;
    }

    void System::Session::SetPlaneDetectionEnabled(bool) const
    {
        // Not supported.
    }

    bool System::Session::TrySetFeaturePointCloudEnabled(bool) const
    {
        return false;
    }

    bool System::Session::TrySetPreferredPlaneDetectorOptions(const GeometryDetectorOptions&)
    {
        return false;
    }

    bool System::Session::TrySetMeshDetectorEnabled(const bool)
    {
        return false;
    }

    bool System::Session::TrySetPreferredMeshDetectorOptions(const GeometryDetectorOptions&)
    {
        return false;
    }

    std::vector<ImageTrackingScore>* System::Session::GetImageTrackingScores() const
    {
        return nullptr;
    }

    void System::Session::CreateAugmentedImageDatabase(const std::vector<System::Session::ImageTrackingRequest>&) const
    {
        // Not supported.
    }
}
