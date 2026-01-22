#include <XR.h>
#include <XRHelpers.h>

#include <AndroidExtensions/Globals.h>
#include <AndroidExtensions/JavaWrappers.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <android/log.h>

#include <arcana/threading/task.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "Include/IXrContextOpenXR.h"

namespace
{
    constexpr char TAG[] = "BabylonNativeOpenXR";

    template <typename T>
    T MakeXrStruct(XrStructureType type)
    {
        T value{};
        value.type = type;
        value.next = nullptr;
        return value;
    }

    void LogXrResult(const char* what, XrResult result)
    {
        __android_log_print(ANDROID_LOG_WARN, TAG, "%s failed: %d", what, result);
    }

    void CheckXr(const char* what, XrResult result)
    {
        if (XR_FAILED(result))
        {
            LogXrResult(what, result);
            std::ostringstream message;
            message << what << " failed with XrResult=" << result;
            throw std::runtime_error{message.str()};
        }
    }

    xr::Pose ToPose(const XrPosef& xrPose)
    {
        xr::Pose pose{};
        pose.Position.X = xrPose.position.x;
        pose.Position.Y = xrPose.position.y;
        pose.Position.Z = xrPose.position.z;
        pose.Orientation.X = xrPose.orientation.x;
        pose.Orientation.Y = xrPose.orientation.y;
        pose.Orientation.Z = xrPose.orientation.z;
        pose.Orientation.W = xrPose.orientation.w;
        return pose;
    }

    void PopulateProjectionMatrix(const XrFovf& fov, float nearZ, float farZ, std::array<float, 16>& out)
    {
        const float tanLeft = std::tan(fov.angleLeft);
        const float tanRight = std::tan(fov.angleRight);
        const float tanDown = std::tan(fov.angleDown);
        const float tanUp = std::tan(fov.angleUp);

        const float tanWidth = tanRight - tanLeft;
        const float tanHeight = tanUp - tanDown;

        // Column-major projection matrix compatible with WebXR/OpenGL conventions.
        out = {};
        out[0] = 2.0f / tanWidth;
        out[5] = 2.0f / tanHeight;
        out[8] = (tanRight + tanLeft) / tanWidth;
        out[9] = (tanUp + tanDown) / tanHeight;
        out[10] = -(farZ + nearZ) / (farZ - nearZ);
        out[11] = -1.0f;
        out[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    }

    bool HasExtension(const std::vector<XrExtensionProperties>& available, const char* name)
    {
        for (const auto& ext : available)
        {
            if (std::strcmp(ext.extensionName, name) == 0)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<XrExtensionProperties> EnumerateInstanceExtensions()
    {
        uint32_t count{};
        CheckXr("xrEnumerateInstanceExtensionProperties(count)",
            xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr));

        std::vector<XrExtensionProperties> extensions(count);
        for (auto& ext : extensions)
        {
            ext = MakeXrStruct<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES);
        }
        CheckXr("xrEnumerateInstanceExtensionProperties(list)",
            xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()));
        return extensions;
    }

    void InitializeLoaderAndroid(JavaVM* vm, jobject activity)
    {
        PFN_xrInitializeLoaderKHR xrInitializeLoaderKHRFn{};
        const XrResult getProcResult =
            xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", reinterpret_cast<PFN_xrVoidFunction*>(&xrInitializeLoaderKHRFn));
        if (XR_FAILED(getProcResult) || xrInitializeLoaderKHRFn == nullptr)
        {
            // Some loaders expose this directly; failing to resolve isn't fatal on all runtimes.
            __android_log_print(ANDROID_LOG_INFO, TAG, "xrInitializeLoaderKHR not available (result=%d). Continuing.", getProcResult);
            return;
        }

        auto initInfo = MakeXrStruct<XrLoaderInitInfoAndroidKHR>(XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR);
        initInfo.applicationVM = vm;
        initInfo.applicationContext = activity;
        CheckXr("xrInitializeLoaderKHR", xrInitializeLoaderKHRFn(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&initInfo)));
    }

    int64_t ChooseSwapchainFormat(XrSession session)
    {
        uint32_t formatCount{};
        CheckXr("xrEnumerateSwapchainFormats(count)", xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr));
        std::vector<int64_t> formats(formatCount);
        CheckXr("xrEnumerateSwapchainFormats(list)", xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()));

        // Prefer sRGB if available.
        for (int64_t format : formats)
        {
            if (format == GL_SRGB8_ALPHA8)
            {
                return format;
            }
        }
        for (int64_t format : formats)
        {
            if (format == GL_RGBA8)
            {
                return format;
            }
        }

        // Fall back to the first reported format.
        if (!formats.empty())
        {
            return formats[0];
        }

        throw std::runtime_error{"OpenXR runtime reported no swapchain formats."};
    }
}

namespace xr
{
    struct XrContextOpenXR : public IXrContextOpenXR
    {
        bool Initialized{false};
        ::XrInstance Instance{XR_NULL_HANDLE};
        ::XrSystemId SystemId{XR_NULL_SYSTEM_ID};
        ::XrSession Session{XR_NULL_HANDLE};
        ::XrSpace AppSpace{XR_NULL_HANDLE};

        bool IsInitialized() const override
        {
            return Initialized;
        }

        ::XrInstance GetXrInstance() const override
        {
            return Instance;
        }

        ::XrSystemId GetXrSystemId() const override
        {
            return SystemId;
        }

        ::XrSession GetXrSession() const override
        {
            return Session;
        }

        ::XrSpace GetXrAppSpace() const override
        {
            return AppSpace;
        }

        virtual ~XrContextOpenXR() = default;
    };

    struct System::Impl
    {
        std::shared_ptr<XrContextOpenXR> XrContext{std::make_shared<XrContextOpenXR>()};
        RuntimeCapabilities Capabilities{};
        std::string ApplicationName{};
        XrViewConfigurationType ViewConfigType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};

        Impl(const std::string& applicationName)
            : ApplicationName{applicationName}
        {
            Capabilities.SupportsImmersiveVR = true;
            Capabilities.SupportsImmersiveAR = false;
            Capabilities.SupportsInline = false;

            Capabilities.SupportsVulkan = false;
            Capabilities.SupportsOpenGLES = true;

            Capabilities.SupportsAnchors = false;
            Capabilities.SupportsPlaneDetection = false;
            Capabilities.SupportsMeshDetection = false;
            Capabilities.SupportsHitTest = false;
            Capabilities.SupportsImageTracking = false;
            Capabilities.SupportsLightEstimation = false;
            Capabilities.SupportsPassthrough = false;

            Capabilities.SupportedColorFormats = {TextureFormat::RGBA8_SRGB, TextureFormat::BGRA8_SRGB};
            Capabilities.SupportedDepthFormats = {TextureFormat::D24S8, TextureFormat::D16};
        }

        ~Impl()
        {
            if (XrContext->AppSpace != XR_NULL_HANDLE)
            {
                xrDestroySpace(XrContext->AppSpace);
                XrContext->AppSpace = XR_NULL_HANDLE;
            }

            if (XrContext->Session != XR_NULL_HANDLE)
            {
                xrDestroySession(XrContext->Session);
                XrContext->Session = XR_NULL_HANDLE;
            }

            if (XrContext->Instance != XR_NULL_HANDLE)
            {
                xrDestroyInstance(XrContext->Instance);
                XrContext->Instance = XR_NULL_HANDLE;
            }
        }

        bool IsInitialized() const
        {
            return XrContext->Initialized;
        }

        bool TryInitialize()
        {
            if (XrContext->Initialized)
            {
                return true;
            }

            auto availableExtensions = EnumerateInstanceExtensions();

            if (!HasExtension(availableExtensions, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) ||
                !HasExtension(availableExtensions, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME))
            {
                __android_log_print(ANDROID_LOG_WARN, TAG, "OpenXR instance extensions missing required Android/GLES extensions.");
                return false;
            }

            const char* enabledExtensions[] = {
                XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
            };

            JNIEnv* env = android::global::GetEnvForCurrentThread();
            JavaVM* vm{};
            env->GetJavaVM(&vm);
            jobject activity = static_cast<jobject>(android::global::GetCurrentActivity());

            InitializeLoaderAndroid(vm, activity);

            auto androidCreateInfo = MakeXrStruct<XrInstanceCreateInfoAndroidKHR>(XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR);
            androidCreateInfo.applicationVM = vm;
            androidCreateInfo.applicationActivity = activity;

            auto createInfo = MakeXrStruct<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
            createInfo.next = &androidCreateInfo;

            std::snprintf(createInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "%s", ApplicationName.c_str());
            createInfo.applicationInfo.applicationVersion = 1;
            std::snprintf(createInfo.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "BabylonNative");
            createInfo.applicationInfo.engineVersion = 1;
            createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

            createInfo.enabledExtensionCount = static_cast<uint32_t>(sizeof(enabledExtensions) / sizeof(enabledExtensions[0]));
            createInfo.enabledExtensionNames = enabledExtensions;

            CheckXr("xrCreateInstance", xrCreateInstance(&createInfo, &XrContext->Instance));

            auto systemInfo = MakeXrStruct<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
            systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
            CheckXr("xrGetSystem", xrGetSystem(XrContext->Instance, &systemInfo, &XrContext->SystemId));

            XrContext->Initialized = true;
            __android_log_print(ANDROID_LOG_INFO, TAG, "OpenXR system initialized.");
            return true;
        }
    };

    namespace
    {
        struct Swapchain
        {
            XrSwapchain Handle{XR_NULL_HANDLE};
            int32_t Width{};
            int32_t Height{};
            int64_t Format{};
            std::vector<XrSwapchainImageOpenGLESKHR> Images{};
            uint32_t AcquiredIndex{};
            bool Acquired{false};

            void Destroy()
            {
                if (Handle != XR_NULL_HANDLE)
                {
                    xrDestroySwapchain(Handle);
                    Handle = XR_NULL_HANDLE;
                }
                Images.clear();
                Acquired = false;
                AcquiredIndex = 0;
            }
        };

        PFN_xrGetOpenGLESGraphicsRequirementsKHR GetXrGlesRequirementsFn(XrInstance instance)
        {
            PFN_xrGetOpenGLESGraphicsRequirementsKHR fn{};
            CheckXr("xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)",
                xrGetInstanceProcAddr(instance, "xrGetOpenGLESGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&fn)));
            return fn;
        }
    }

    struct System::Session::Impl
    {
        System::Impl& SystemImpl;
        std::shared_ptr<XrContextOpenXR> XrContext;

        XrSessionState SessionState{XR_SESSION_STATE_UNKNOWN};
        bool SessionRunning{false};
        bool SessionEnded{false};

        XrEnvironmentBlendMode EnvironmentBlendMode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};

        float DepthNearZ{0.1f};
        float DepthFarZ{1000.0f};

        std::vector<Frame::View> ActiveFrameViews{};
        std::vector<Frame::InputSource> InputSources{};
        std::vector<FeaturePoint> FeaturePointCloud{};
        std::optional<Space> EyeTrackerSpace{};

        std::vector<XrViewConfigurationView> ViewConfigurationViews{};
        std::vector<XrView> LocatedViews{};
        XrTime PredictedDisplayTime{};
        bool ShouldRender{false};
        bool TrackingValid{false};

        Swapchain ColorSwapchains[2]{};

        Impl(System::Impl& systemImpl, void* graphicsContext, std::function<void*()> /*windowProvider*/)
            : SystemImpl{systemImpl}
            , XrContext{systemImpl.XrContext}
            , Context{reinterpret_cast<EGLContext>(graphicsContext)}
        {
            ActiveFrameViews.resize(2);
        }

        ~Impl()
        {
            ColorSwapchains[0].Destroy();
            ColorSwapchains[1].Destroy();

            if (XrContext->AppSpace != XR_NULL_HANDLE)
            {
                xrDestroySpace(XrContext->AppSpace);
                XrContext->AppSpace = XR_NULL_HANDLE;
            }

            if (XrContext->Session != XR_NULL_HANDLE)
            {
                xrDestroySession(XrContext->Session);
                XrContext->Session = XR_NULL_HANDLE;
            }
        }

        EGLContext Context{EGL_NO_CONTEXT};
        EGLDisplay Display{EGL_NO_DISPLAY};
        EGLConfig Config{};

        void InitializeIfNeeded()
        {
            if (XrContext->Session != XR_NULL_HANDLE)
            {
                return;
            }

            if (!SystemImpl.TryInitialize())
            {
                throw std::runtime_error{"OpenXR System::TryInitialize failed."};
            }

            Display = eglGetCurrentDisplay();
            if (Display == EGL_NO_DISPLAY)
            {
                Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            }
            if (Display == EGL_NO_DISPLAY)
            {
                throw std::runtime_error{"OpenXR: No EGL display available."};
            }

            EGLSurface currentSurface = eglGetCurrentSurface(EGL_DRAW);
            if (currentSurface == EGL_NO_SURFACE)
            {
                throw std::runtime_error{"OpenXR: No current EGL surface."};
            }

            EGLint configId{};
            if (!eglQuerySurface(Display, currentSurface, EGL_CONFIG_ID, &configId))
            {
                throw std::runtime_error{"OpenXR: Failed to query EGL surface config id."};
            }

            EGLint numConfigs{};
            const EGLint attribs[] = {EGL_CONFIG_ID, configId, EGL_NONE};
            if (!eglChooseConfig(Display, attribs, &Config, 1, &numConfigs) || numConfigs == 0)
            {
                throw std::runtime_error{"OpenXR: Failed to choose EGL config for current surface."};
            }

            // Validate OpenGL ES requirements.
            auto requirements = MakeXrStruct<XrGraphicsRequirementsOpenGLESKHR>(XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR);
            {
                auto fn = GetXrGlesRequirementsFn(XrContext->Instance);
                CheckXr("xrGetOpenGLESGraphicsRequirementsKHR", fn(XrContext->Instance, XrContext->SystemId, &requirements));
            }

            auto graphicsBinding = MakeXrStruct<XrGraphicsBindingOpenGLESAndroidKHR>(XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR);
            graphicsBinding.display = Display;
            graphicsBinding.config = Config;
            graphicsBinding.context = Context != EGL_NO_CONTEXT ? Context : eglGetCurrentContext();

            auto sessionCreateInfo = MakeXrStruct<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
            sessionCreateInfo.next = &graphicsBinding;
            sessionCreateInfo.systemId = XrContext->SystemId;
            CheckXr("xrCreateSession", xrCreateSession(XrContext->Instance, &sessionCreateInfo, &XrContext->Session));

            auto spaceCreateInfo = MakeXrStruct<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
            spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            spaceCreateInfo.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};
            CheckXr("xrCreateReferenceSpace", xrCreateReferenceSpace(XrContext->Session, &spaceCreateInfo, &XrContext->AppSpace));

            // Query view configuration for stereo.
            uint32_t viewCount{};
            CheckXr("xrEnumerateViewConfigurationViews(count)",
                xrEnumerateViewConfigurationViews(XrContext->Instance, XrContext->SystemId, SystemImpl.ViewConfigType, 0, &viewCount, nullptr));
            ViewConfigurationViews.resize(viewCount);
            for (auto& view : ViewConfigurationViews)
            {
                view = MakeXrStruct<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW);
            }
            CheckXr("xrEnumerateViewConfigurationViews(list)",
                xrEnumerateViewConfigurationViews(XrContext->Instance, XrContext->SystemId, SystemImpl.ViewConfigType, viewCount, &viewCount, ViewConfigurationViews.data()));

            LocatedViews.resize(viewCount);
            for (auto& view : LocatedViews)
            {
                view = MakeXrStruct<XrView>(XR_TYPE_VIEW);
            }

            const int64_t swapchainFormat = ChooseSwapchainFormat(XrContext->Session);
            for (uint32_t viewIdx = 0; viewIdx < std::min<uint32_t>(viewCount, 2); ++viewIdx)
            {
                CreateColorSwapchain(viewIdx, swapchainFormat);
            }

            __android_log_print(ANDROID_LOG_INFO, TAG, "OpenXR session created.");
        }

        void CreateColorSwapchain(uint32_t viewIdx, int64_t format)
        {
            auto& viewConfig = ViewConfigurationViews.at(viewIdx);
            auto& swapchain = ColorSwapchains[viewIdx];

            swapchain.Format = format;
            swapchain.Width = static_cast<int32_t>(viewConfig.recommendedImageRectWidth);
            swapchain.Height = static_cast<int32_t>(viewConfig.recommendedImageRectHeight);

            auto swapchainCreateInfo = MakeXrStruct<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
            swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            swapchainCreateInfo.format = swapchain.Format;
            // Babylon Native currently creates non-MSAA render targets for XR. Keep the swapchain
            // compatible by explicitly requesting a single sample.
            swapchainCreateInfo.sampleCount = 1;
            swapchainCreateInfo.width = swapchain.Width;
            swapchainCreateInfo.height = swapchain.Height;
            swapchainCreateInfo.faceCount = 1;
            swapchainCreateInfo.arraySize = 1;
            swapchainCreateInfo.mipCount = 1;
            CheckXr("xrCreateSwapchain", xrCreateSwapchain(XrContext->Session, &swapchainCreateInfo, &swapchain.Handle));

            uint32_t imageCount{};
            CheckXr("xrEnumerateSwapchainImages(count)", xrEnumerateSwapchainImages(swapchain.Handle, 0, &imageCount, nullptr));
            swapchain.Images.resize(imageCount);
            for (auto& image : swapchain.Images)
            {
                image = MakeXrStruct<XrSwapchainImageOpenGLESKHR>(XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR);
            }
            CheckXr("xrEnumerateSwapchainImages(list)",
                xrEnumerateSwapchainImages(swapchain.Handle, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain.Images.data())));
        }

        void PollEvents()
        {
            auto eventBuffer = MakeXrStruct<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
            while (true)
            {
                eventBuffer = MakeXrStruct<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
                const XrResult result = xrPollEvent(XrContext->Instance, &eventBuffer);
                if (result == XR_EVENT_UNAVAILABLE)
                {
                    break;
                }
                CheckXr("xrPollEvent", result);

                switch (eventBuffer.type)
                {
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                {
                    const auto* stateChanged = reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventBuffer);
                    SessionState = stateChanged->state;

                    if (SessionState == XR_SESSION_STATE_READY && !SessionRunning)
                    {
                        auto beginInfo = MakeXrStruct<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
                        beginInfo.primaryViewConfigurationType = SystemImpl.ViewConfigType;
                        CheckXr("xrBeginSession", xrBeginSession(XrContext->Session, &beginInfo));
                        SessionRunning = true;
                    }
                    else if (SessionState == XR_SESSION_STATE_STOPPING && SessionRunning)
                    {
                        CheckXr("xrEndSession", xrEndSession(XrContext->Session));
                        SessionRunning = false;
                    }
                    else if (SessionState == XR_SESSION_STATE_EXITING || SessionState == XR_SESSION_STATE_LOSS_PENDING)
                    {
                        SessionEnded = true;
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        bool IsTracking() const
        {
            return SessionRunning && TrackingValid;
        }

        std::unique_ptr<Session::Frame> GetNextFrame(bool& shouldEndSession, bool& shouldRestartSession, std::function<arcana::task<void, std::exception_ptr>(void*)> /*deletedTextureAsyncCallback*/)
        {
            InitializeIfNeeded();
            PollEvents();

            shouldEndSession = SessionEnded;
            shouldRestartSession = false;

            ShouldRender = false;
            PredictedDisplayTime = {};
            TrackingValid = false;

            if (!SessionRunning)
            {
                ActiveFrameViews.clear();
                return std::make_unique<Session::Frame>(*this);
            }

            auto waitInfo = MakeXrStruct<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
            auto frameState = MakeXrStruct<XrFrameState>(XR_TYPE_FRAME_STATE);
            CheckXr("xrWaitFrame", xrWaitFrame(XrContext->Session, &waitInfo, &frameState));
            PredictedDisplayTime = frameState.predictedDisplayTime;
            ShouldRender = frameState.shouldRender == XR_TRUE;

            auto beginInfo = MakeXrStruct<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
            CheckXr("xrBeginFrame", xrBeginFrame(XrContext->Session, &beginInfo));

            if (!ShouldRender)
            {
                ActiveFrameViews.clear();
                return std::make_unique<Session::Frame>(*this);
            }

            auto locateInfo = MakeXrStruct<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
            locateInfo.viewConfigurationType = SystemImpl.ViewConfigType;
            locateInfo.displayTime = PredictedDisplayTime;
            locateInfo.space = XrContext->AppSpace;

            auto viewState = MakeXrStruct<XrViewState>(XR_TYPE_VIEW_STATE);
            uint32_t viewCountOutput{};
            CheckXr("xrLocateViews", xrLocateViews(XrContext->Session, &locateInfo, &viewState, static_cast<uint32_t>(LocatedViews.size()), &viewCountOutput, LocatedViews.data()));

            const bool poseValid =
                (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
                (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
            TrackingValid = poseValid;

            if (!poseValid)
            {
                ActiveFrameViews.clear();
            }
            else
            {
                const uint32_t supportedViewCount = std::min<uint32_t>(viewCountOutput, 2);
                ActiveFrameViews.resize(supportedViewCount);
                for (uint32_t viewIdx = 0; viewIdx < supportedViewCount; ++viewIdx)
                {
                    auto& dstView = ActiveFrameViews[viewIdx];
                    dstView.Space.Pose = ToPose(LocatedViews[viewIdx].pose);
                    dstView.DepthNearZ = DepthNearZ;
                    dstView.DepthFarZ = DepthFarZ;
                    PopulateProjectionMatrix(LocatedViews[viewIdx].fov, DepthNearZ, DepthFarZ, dstView.ProjectionMatrix);

                    // Provide color swapchain image pointer for bgfx overrideInternal (OpenGL texture name).
                    auto& swapchain = ColorSwapchains[viewIdx];
                    AcquireSwapchainImage(swapchain);
                    const GLuint image = swapchain.Images.at(swapchain.AcquiredIndex).image;

                    dstView.ColorTexturePointer = reinterpret_cast<void*>(static_cast<uintptr_t>(image));
                    dstView.ColorTextureFormat = TextureFormat::RGBA8_SRGB;
                    dstView.ColorTextureSize = {static_cast<size_t>(swapchain.Width), static_cast<size_t>(swapchain.Height), 1};

                    dstView.DepthTexturePointer = nullptr;
                    dstView.DepthTextureFormat = TextureFormat::D24S8;
                    dstView.DepthTextureSize = dstView.ColorTextureSize;

                    dstView.IsFirstPersonObserver = false;
                    dstView.RequiresAppClear = true;
                }
            }

            return std::make_unique<Session::Frame>(*this);
        }

        void AcquireSwapchainImage(Swapchain& swapchain)
        {
            if (swapchain.Acquired)
            {
                return;
            }

            auto acquireInfo = MakeXrStruct<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
            CheckXr("xrAcquireSwapchainImage", xrAcquireSwapchainImage(swapchain.Handle, &acquireInfo, &swapchain.AcquiredIndex));

            auto waitInfo = MakeXrStruct<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
            waitInfo.timeout = XR_INFINITE_DURATION;
            CheckXr("xrWaitSwapchainImage", xrWaitSwapchainImage(swapchain.Handle, &waitInfo));

            swapchain.Acquired = true;
        }

        void ReleaseSwapchainImage(Swapchain& swapchain)
        {
            if (!swapchain.Acquired)
            {
                return;
            }

            auto releaseInfo = MakeXrStruct<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
            CheckXr("xrReleaseSwapchainImage", xrReleaseSwapchainImage(swapchain.Handle, &releaseInfo));
            swapchain.Acquired = false;
            swapchain.AcquiredIndex = 0;
        }

        void EndFrame()
        {
            if (!SessionRunning)
            {
                return;
            }

            std::vector<XrCompositionLayerBaseHeader*> layers{};

            auto layer = MakeXrStruct<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
            std::vector<XrCompositionLayerProjectionView> projectionViews{};

            if (ShouldRender && !ActiveFrameViews.empty())
            {
                projectionViews.resize(ActiveFrameViews.size());
                for (uint32_t viewIdx = 0; viewIdx < static_cast<uint32_t>(ActiveFrameViews.size()); ++viewIdx)
                {
                    auto& view = projectionViews[viewIdx];
                    view = MakeXrStruct<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
                    view.pose = LocatedViews[viewIdx].pose;
                    view.fov = LocatedViews[viewIdx].fov;
                    view.subImage.swapchain = ColorSwapchains[viewIdx].Handle;
                    view.subImage.imageRect.offset = {0, 0};
                    view.subImage.imageRect.extent = {ColorSwapchains[viewIdx].Width, ColorSwapchains[viewIdx].Height};
                    view.subImage.imageArrayIndex = 0;
                }

                layer.space = XrContext->AppSpace;
                layer.viewCount = static_cast<uint32_t>(projectionViews.size());
                layer.views = projectionViews.data();
                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
            }

            // Release swapchain images (must happen before endFrame on most runtimes).
            ReleaseSwapchainImage(ColorSwapchains[0]);
            ReleaseSwapchainImage(ColorSwapchains[1]);

            auto endInfo = MakeXrStruct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
            endInfo.displayTime = PredictedDisplayTime;
            endInfo.environmentBlendMode = EnvironmentBlendMode;
            endInfo.layerCount = static_cast<uint32_t>(layers.size());
            endInfo.layers = layers.data();
            CheckXr("xrEndFrame", xrEndFrame(XrContext->Session, &endInfo));
        }
    };

    struct System::Session::Frame::Impl
    {
        explicit Impl(Session::Impl& sessionImpl)
            : SessionImpl{sessionImpl}
        {
        }

        Session::Impl& SessionImpl;
    };

    System::Session::Frame::Frame(Session::Impl& sessionImpl)
        : Views{sessionImpl.ActiveFrameViews}
        , InputSources{sessionImpl.InputSources}
        , FeaturePointCloud{sessionImpl.FeaturePointCloud}
        , EyeTrackerSpace{sessionImpl.EyeTrackerSpace}
        , UpdatedSceneObjects{}
        , RemovedSceneObjects{}
        , UpdatedPlanes{}
        , RemovedPlanes{}
        , UpdatedMeshes{}
        , RemovedMeshes{}
        , UpdatedImageTrackingResults{}
        , CameraImageRGBA{}
        , CameraImageWidth{0}
        , CameraImageHeight{0}
        , IsTracking{sessionImpl.IsTracking()}
        , m_impl{std::make_unique<Session::Frame::Impl>(sessionImpl)}
    {
    }

    System::Session::Frame::~Frame() = default;

    void System::Session::Frame::GetHitTestResults(std::vector<HitResult>&, Ray, HitTestTrackableType) const
    {
        // Hit tests not supported for VR-only OpenXR implementation.
    }

    Anchor System::Session::Frame::CreateAnchor(Pose, NativeTrackablePtr) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    Anchor System::Session::Frame::DeclareAnchor(NativeAnchorPtr) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    void System::Session::Frame::UpdateAnchor(Anchor&) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    void System::Session::Frame::DeleteAnchor(Anchor&) const
    {
        throw std::runtime_error{"Anchors are not supported on this platform."};
    }

    System::Session::Frame::SceneObject& System::Session::Frame::GetSceneObjectByID(System::Session::Frame::SceneObject::Identifier) const
    {
        throw std::runtime_error{"Scene object detection is not supported on this platform."};
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
        m_impl->SessionImpl.EndFrame();
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
        // For Android XR builds, only immersive VR is supported via OpenXR.
        return arcana::task_from_result<std::exception_ptr>(sessionType == SessionType::IMMERSIVE_VR);
    }

    uintptr_t System::GetNativeXrContext()
    {
        return reinterpret_cast<uintptr_t>(m_impl->XrContext.get());
    }

    std::string System::GetNativeXrContextType()
    {
        return "OpenXR";
    }

    const RuntimeCapabilities& System::GetCapabilities() const
    {
        return m_impl->Capabilities;
    }

    arcana::task<std::shared_ptr<System::Session>, std::exception_ptr> System::Session::CreateAsync(System& system, void* graphicsDevice, void* commandQueue, std::function<void*()> windowProvider)
    {
        (void)commandQueue;
        return arcana::task_from_result<std::exception_ptr>(
            std::make_shared<System::Session>(system, graphicsDevice, commandQueue, std::move(windowProvider)));
    }

    System::Session::Session(System& system, void* graphicsDevice, void*, std::function<void*()> windowProvider)
        : m_impl{std::make_unique<System::Session::Impl>(*system.m_impl, graphicsDevice, std::move(windowProvider))}
    {
    }

    System::Session::~Session() = default;

    std::unique_ptr<System::Session::Frame> System::Session::GetNextFrame(bool& shouldEndSession, bool& shouldRestartSession, std::function<arcana::task<void, std::exception_ptr>(void*)> deletedTextureAsyncCallback)
    {
        return m_impl->GetNextFrame(shouldEndSession, shouldRestartSession, deletedTextureAsyncCallback);
    }

    void System::Session::RequestEndSession()
    {
        if (m_impl->XrContext->Session != XR_NULL_HANDLE)
        {
            xrRequestExitSession(m_impl->XrContext->Session);
        }
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
