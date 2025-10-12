#include <XR.h>
#include <XRHelpers.h>

#include <assert.h>
#include <optional>
#include <sstream>
#include <chrono>
#include <arcana/threading/task.h>
#include <arcana/threading/dispatcher.h>
#include <thread>
#include <vector>
#include <array>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <AndroidExtensions/Globals.h>
#include <AndroidExtensions/JavaWrappers.h>
#include <android/native_window.h>
#include <android/log.h>

// OpenXR headers
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <gsl/gsl>

#include "Include/IXrContextOpenXR.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "BabylonNativeXR", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "BabylonNativeXR", __VA_ARGS__)

using namespace android;
using namespace android::global;

namespace xr
{
    struct XrContextOpenXR : public IXrContextOpenXR {
        bool Initialized{false};
        XrInstance Instance{XR_NULL_HANDLE};
        XrSession Session{XR_NULL_HANDLE};
        XrSpace Space{XR_NULL_HANDLE};
        XrSystemId SystemId{XR_NULL_SYSTEM_ID};

        bool IsInitialized() const override
        {
            return Initialized;
        }

        XrInstance GetXrInstance() const override
        {
            return Instance;
        }

        XrSession GetXrSession() const override
        {
            return Session;
        }

        XrSpace GetXrSpace() const override
        {
            return Space;
        }

        XrSystemId GetXrSystemId() const override
        {
            return SystemId;
        }

        virtual ~XrContextOpenXR() = default;
    };

    struct System::Impl
    {
        std::shared_ptr<XrContextOpenXR> XrContext{std::make_shared<XrContextOpenXR>()};

        Impl(const std::string& applicationName)
            : m_applicationName{applicationName}
        {
            LOGI("OpenXR System::Impl created for app: %s", applicationName.c_str());
        }

        bool IsInitialized() const
        {
            return XrContext->Initialized;
        }

        bool TryInitialize() {
            if (XrContext->Initialized) {
                return true;
            }

            LOGI("Initializing OpenXR...");

            // Create OpenXR instance
            XrApplicationInfo appInfo{};
            strncpy(appInfo.applicationName, m_applicationName.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
            appInfo.applicationVersion = 1;
            strncpy(appInfo.engineName, "BabylonNative", XR_MAX_ENGINE_NAME_SIZE - 1);
            appInfo.engineVersion = 1;
            appInfo.apiVersion = XR_CURRENT_API_VERSION;

            std::vector<const char*> extensions = {
                XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
            };

            // Get JavaVM from JNIEnv
            JavaVM* javaVM = nullptr;
            JNIEnv* env = GetEnvForCurrentThread();
            if (env && env->GetJavaVM(&javaVM) != JNI_OK) {
                LOGE("Failed to get JavaVM from JNIEnv");
                return false;
            }

            XrInstanceCreateInfoAndroidKHR androidInfo{};
            androidInfo.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
            androidInfo.applicationVM = javaVM;
            androidInfo.applicationActivity = GetCurrentActivity();

            XrInstanceCreateInfo createInfo{};
            createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
            createInfo.next = &androidInfo;
            createInfo.createFlags = 0;
            createInfo.applicationInfo = appInfo;
            createInfo.enabledApiLayerCount = 0;
            createInfo.enabledApiLayerNames = nullptr;
            createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            createInfo.enabledExtensionNames = extensions.data();

            XrResult result = xrCreateInstance(&createInfo, &XrContext->Instance);
            if (result != XR_SUCCESS) {
                LOGE("Failed to create OpenXR instance: %d", result);
                return false;
            }

            LOGI("OpenXR instance created successfully");

            // Get system
            XrSystemGetInfo systemInfo{};
            systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
            systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

            result = xrGetSystem(XrContext->Instance, &systemInfo, &XrContext->SystemId);
            if (result != XR_SUCCESS) {
                LOGE("Failed to get OpenXR system: %d", result);
                xrDestroyInstance(XrContext->Instance);
                XrContext->Instance = XR_NULL_HANDLE;
                return false;
            }

            LOGI("OpenXR system obtained, ID: %llu", (unsigned long long)XrContext->SystemId);
            XrContext->Initialized = true;
            return true;
        }

    private:
        std::string m_applicationName;
    };

    struct System::Session::Impl
    {
        const System::Impl& SystemImpl;
        std::shared_ptr<XrContextOpenXR> xrContext;
        std::vector<Frame::View> ActiveFrameViews{};
        std::vector<Frame::InputSource> InputSources{};
        std::vector<FeaturePoint> FeaturePointCloud{};
        std::optional<Space> EyeTrackerSpace{};
        float DepthNearZ{ DEFAULT_DEPTH_NEAR_Z };
        float DepthFarZ{ DEFAULT_DEPTH_FAR_Z };

        Impl(System::Impl& systemImpl, void* /*graphicsContext*/, std::function<void*()> /*windowProvider*/)
            : SystemImpl{ systemImpl }
            , xrContext{systemImpl.XrContext}
        {
            LOGI("OpenXR Session::Impl created");
        }

        ~Impl()
        {
            if (xrContext->Session != XR_NULL_HANDLE) {
                xrDestroySession(xrContext->Session);
                xrContext->Session = XR_NULL_HANDLE;
            }

            if (xrContext->Space != XR_NULL_HANDLE) {
                xrDestroySpace(xrContext->Space);
                xrContext->Space = XR_NULL_HANDLE;
            }
        }

        void Initialize()
        {
            if (!xrContext->Initialized) {
                LOGE("Cannot initialize session - system not initialized");
                return;
            }

            // TODO: Create OpenXR session
            LOGI("OpenXR session initialization - TODO");
        }

        std::unique_ptr<Session::Frame> GetNextFrame(bool& shouldEndSession, bool& shouldRestartSession, std::function<arcana::task<void, std::exception_ptr>(void*)> /*deletedTextureAsyncCallback*/)
        {
            if (!xrContext->Initialized) {
                Initialize();
            }

            shouldEndSession = false;
            shouldRestartSession = false;

            // Set up a dummy view for now
            if (ActiveFrameViews.empty()) {
                ActiveFrameViews.push_back({});
                ActiveFrameViews[0].ColorTextureSize = {1920, 1080};
                ActiveFrameViews[0].DepthNearZ = DepthNearZ;
                ActiveFrameViews[0].DepthFarZ = DepthFarZ;
            }

            return std::make_unique<Session::Frame>(*this);
        }

        void RequestEndSession()
        {
            // TODO: Implement session end
            LOGI("OpenXR session end requested");
        }

        void DrawFrame()
        {
            // TODO: Implement frame rendering
        }

        void GetHitTestResults(std::vector<HitResult>& /*filteredResults*/, xr::Ray /*offsetRay*/, xr::HitTestTrackableType /*validHitTestTypes*/)
        {
            // OpenXR doesn't have built-in hit testing like ARCore
            // This would need to be implemented differently
        }
    };

    struct System::Session::Frame::Impl
    {
        Impl(Session::Impl& sessionImpl)
            : sessionImpl{sessionImpl}
        {
        }

        Session::Impl& sessionImpl;
    };

    System::Session::Frame::Frame(Session::Impl& sessionImpl)
        : Views{ sessionImpl.ActiveFrameViews }
        , InputSources{ sessionImpl.InputSources }
        , FeaturePointCloud{ sessionImpl.FeaturePointCloud }
        , EyeTrackerSpace{ sessionImpl.EyeTrackerSpace }
        , IsTracking{true}
        , m_impl{ std::make_unique<Session::Frame::Impl>(sessionImpl) }
    {
    }

    void System::Session::Frame::GetHitTestResults(std::vector<HitResult>& filteredResults, xr::Ray offsetRay, xr::HitTestTrackableType trackableTypes) const
    {
        m_impl->sessionImpl.GetHitTestResults(filteredResults, offsetRay, trackableTypes);
    }

    Anchor System::Session::Frame::CreateAnchor(Pose pose, NativeTrackablePtr /*trackable*/) const
    {
        // TODO: Implement OpenXR anchors
        return {{pose}, nullptr};
    }

    Anchor System::Session::Frame::DeclareAnchor(NativeAnchorPtr /*anchor*/) const
    {
        // TODO: Implement OpenXR anchors
        return {{}, nullptr};
    }

    void System::Session::Frame::UpdateAnchor(xr::Anchor& /*anchor*/) const
    {
        // TODO: Implement OpenXR anchors
    }

    void System::Session::Frame::DeleteAnchor(xr::Anchor& /*anchor*/) const
    {
        // TODO: Implement OpenXR anchors
    }

    System::Session::Frame::SceneObject& System::Session::Frame::GetSceneObjectByID(System::Session::Frame::SceneObject::Identifier) const
    {
        throw std::runtime_error{"Scene object detection is not supported in OpenXR."};
    }

    System::Session::Frame::Plane& System::Session::Frame::GetPlaneByID(System::Session::Frame::Plane::Identifier) const
    {
        throw std::runtime_error{"Plane detection is not supported in OpenXR."};
    }

    System::Session::Frame::Mesh& System::Session::Frame::GetMeshByID(System::Session::Frame::Mesh::Identifier) const
    {
        throw std::runtime_error{"Mesh detection is not supported in OpenXR."};
    }

    System::Session::Frame::ImageTrackingResult& System::Session::Frame::GetImageTrackingResultByID(System::Session::Frame::ImageTrackingResult::Identifier) const
    {
        throw std::runtime_error{"Image tracking is not supported in OpenXR."};
    }

    System::Session::Frame::~Frame() {}

    void System::Session::Frame::Render() {
        m_impl->sessionImpl.DrawFrame();
    }

    System::System(const char* appName)
        : m_impl{ std::make_unique<System::Impl>(appName) }
    {
        LOGI("OpenXR System created");
    }

    System::~System() {}

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
        // OpenXR supports VR sessions
        if (sessionType == SessionType::IMMERSIVE_VR)
        {
            // Check if OpenXR runtime is available
            // This is a non-static context, so we need to create a temporary system or access the existing one
            LOGI("OpenXR immersive-vr support check - assuming supported");
            return arcana::task_from_result<std::exception_ptr>(true);
        }

        // AR and inline sessions are not supported via OpenXR
        return arcana::task_from_result<std::exception_ptr>(false);
    }

    uintptr_t System::GetNativeXrContext()
    {
        return reinterpret_cast<uintptr_t>(m_impl->XrContext.get());
    }

    std::string System::GetNativeXrContextType()
    {
        return "OpenXR";
    }

    arcana::task<std::shared_ptr<System::Session>, std::exception_ptr> System::Session::CreateAsync(System& system, void* graphicsDevice, void* commandQueue, std::function<void*()> windowProvider)
    {
        LOGI("Creating OpenXR session...");
        return arcana::task_from_result<std::exception_ptr>(
            std::make_shared<System::Session>(system, graphicsDevice, commandQueue, windowProvider)
        );
    }

    System::Session::Session(System& system, void* graphicsDevice, void*, std::function<void*()> windowProvider)
        : m_impl{ std::make_unique<System::Session::Impl>(*system.m_impl, graphicsDevice, std::move(windowProvider)) }
    {
        LOGI("OpenXR Session created");
    }

    System::Session::~Session()
    {
    }

    std::unique_ptr<System::Session::Frame> System::Session::GetNextFrame(bool& shouldEndSession, bool& shouldRestartSession, std::function<arcana::task<void, std::exception_ptr>(void*)> deletedTextureAsyncCallback)
    {
        return m_impl->GetNextFrame(shouldEndSession, shouldRestartSession, deletedTextureAsyncCallback);
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

    void System::Session::SetPlaneDetectionEnabled(bool /*enabled*/) const
    {
        // Not supported in OpenXR
    }

    bool System::Session::TrySetFeaturePointCloudEnabled(bool /*enabled*/) const
    {
        // Not supported in OpenXR
        return false;
    }

    bool System::Session::TrySetPreferredPlaneDetectorOptions(const xr::GeometryDetectorOptions&)
    {
        // Not supported in OpenXR
        return false;
    }

    bool System::Session::TrySetMeshDetectorEnabled(const bool)
    {
        // Not supported in OpenXR
        return false;
    }

    bool System::Session::TrySetPreferredMeshDetectorOptions(const xr::GeometryDetectorOptions&)
    {
        // Not supported in OpenXR
        return false;
    }

    std::vector<ImageTrackingScore>* System::Session::GetImageTrackingScores() const
    {
        // Not supported in OpenXR
        return nullptr;
    }

    void System::Session::CreateAugmentedImageDatabase(const std::vector<System::Session::ImageTrackingRequest>& /*requests*/) const
    {
        // Not supported in OpenXR
    }
}
