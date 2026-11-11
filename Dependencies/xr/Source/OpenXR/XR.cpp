#include <XR.h>

#include <AndroidExtensions/Globals.h>

#ifndef XR_USE_PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID
#endif
#ifndef XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_GRAPHICS_API_VULKAN
#endif
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

namespace xr
{
    namespace
    {
        constexpr XrViewConfigurationType VIEW_CONFIGURATION_TYPE{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
        constexpr XrReferenceSpaceType REFERENCE_SPACE_TYPE{XR_REFERENCE_SPACE_TYPE_LOCAL};

        template<typename T>
        T MakeXr(XrStructureType type)
        {
            T value{};
            value.type = type;
            return value;
        }

        void CheckXr(XrResult result, const char* operation)
        {
            if (XR_FAILED(result))
            {
                throw std::runtime_error{std::string{operation} + " failed with OpenXR result " + std::to_string(result)};
            }
        }

        void LogOpenXrError(const char* context, const char* detail)
        {
            __android_log_print(
                ANDROID_LOG_ERROR,
                "BabylonNative",
                "NativeXR OpenXR %s: %s",
                context,
                detail);
        }

        void LogOpenXrInfo(const char* context, int64_t value)
        {
            __android_log_print(
                ANDROID_LOG_INFO,
                "BabylonNative",
                "NativeXR OpenXR %s: %lld",
                context,
                static_cast<long long>(value));
        }

        bool HasExtension(const std::vector<XrExtensionProperties>& extensions, const char* name)
        {
            return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        }

        Pose ToPose(const XrPosef& pose)
        {
            return Pose{
                {pose.position.x, pose.position.y, pose.position.z},
                {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}};
        }

        std::array<float, 16> CreateProjectionMatrix(const XrFovf& fov, float nearZ, float farZ)
        {
            const auto tanLeft = std::tan(fov.angleLeft);
            const auto tanRight = std::tan(fov.angleRight);
            const auto tanDown = std::tan(fov.angleDown);
            const auto tanUp = std::tan(fov.angleUp);
            const auto tanWidth = tanRight - tanLeft;
            const auto tanHeight = tanUp - tanDown;

            std::array<float, 16> matrix{};
            matrix[0] = 2.0f / tanWidth;
            matrix[5] = 2.0f / tanHeight;
            matrix[8] = (tanRight + tanLeft) / tanWidth;
            matrix[9] = (tanUp + tanDown) / tanHeight;
            matrix[10] = -farZ / (farZ - nearZ);
            matrix[11] = -1.0f;
            matrix[14] = -(farZ * nearZ) / (farZ - nearZ);
            return matrix;
        }

        TextureFormat ToTextureFormat(int64_t format)
        {
            switch (format)
            {
            case VK_FORMAT_R8G8B8A8_UNORM:
                return TextureFormat::RGBA8_SRGB;
            case VK_FORMAT_B8G8R8A8_UNORM:
                return TextureFormat::BGRA8_SRGB;
            default:
                throw std::runtime_error{"The OpenXR runtime did not expose a WebGPU-compatible UNORM color swapchain format."};
            }
        }

        int64_t SelectColorFormat(const std::vector<int64_t>& formats)
        {
            constexpr std::array<int64_t, 2> preferredFormats{
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_FORMAT_B8G8R8A8_UNORM};
            for (const auto preferred : preferredFormats)
            {
                if (std::find(formats.begin(), formats.end(), preferred) != formats.end())
                {
                    return preferred;
                }
            }

            throw std::runtime_error{"The OpenXR runtime does not support RGBA8_UNORM or BGRA8_UNORM swapchains required by NativeWebGPU."};
        }
    }

    struct System::Impl
    {
        explicit Impl(const char* appName)
            : AppName{appName == nullptr ? "Babylon Native" : appName}
        {
        }

        ~Impl()
        {
            if (Instance != XR_NULL_HANDLE)
            {
                xrDestroyInstance(Instance);
            }
        }

        bool TryInitialize()
        {
            if (Instance != XR_NULL_HANDLE)
            {
                return true;
            }

            auto* env = android::global::GetEnvForCurrentThread();
            JavaVM* javaVm{};
            if (env == nullptr || env->GetJavaVM(&javaVm) != JNI_OK || javaVm == nullptr)
            {
                return false;
            }

            auto appContext = android::global::GetAppContext();
            auto currentActivity = android::global::GetCurrentActivity();
            if (static_cast<jobject>(appContext) == nullptr || static_cast<jobject>(currentActivity) == nullptr)
            {
                return false;
            }

            PFN_xrInitializeLoaderKHR initializeLoader{};
            if (XR_SUCCEEDED(xrGetInstanceProcAddr(
                    XR_NULL_HANDLE,
                    "xrInitializeLoaderKHR",
                    reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader))) &&
                initializeLoader != nullptr)
            {
                auto loaderInfo = MakeXr<XrLoaderInitInfoAndroidKHR>(XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR);
                loaderInfo.applicationVM = javaVm;
                loaderInfo.applicationContext = static_cast<jobject>(appContext);
                CheckXr(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)), "xrInitializeLoaderKHR");
            }

            uint32_t extensionCount{};
            CheckXr(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr), "xrEnumerateInstanceExtensionProperties");
            std::vector<XrExtensionProperties> extensions(
                extensionCount,
                MakeXr<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES));
            CheckXr(
                xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data()),
                "xrEnumerateInstanceExtensionProperties");
            if (!HasExtension(extensions, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) ||
                !HasExtension(extensions, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))
            {
                return false;
            }

            auto androidCreateInfo = MakeXr<XrInstanceCreateInfoAndroidKHR>(XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR);
            androidCreateInfo.applicationVM = javaVm;
            androidCreateInfo.applicationActivity = static_cast<jobject>(currentActivity);

            const std::array<const char*, 2> enabledExtensions{
                XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
            auto createInfo = MakeXr<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
            createInfo.next = &androidCreateInfo;
            std::strncpy(createInfo.applicationInfo.applicationName, AppName.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
            std::strncpy(createInfo.applicationInfo.engineName, "BabylonNative", XR_MAX_ENGINE_NAME_SIZE - 1);
            createInfo.applicationInfo.applicationVersion = 1;
            createInfo.applicationInfo.engineVersion = 1;
            createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
            createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
            createInfo.enabledExtensionNames = enabledExtensions.data();
            CheckXr(xrCreateInstance(&createInfo, &Instance), "xrCreateInstance");

            auto systemInfo = MakeXr<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
            systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
            CheckXr(xrGetSystem(Instance, &systemInfo, &SystemId), "xrGetSystem");

            CheckXr(
                xrGetInstanceProcAddr(
                    Instance,
                    "xrGetVulkanGraphicsRequirements2KHR",
                    reinterpret_cast<PFN_xrVoidFunction*>(&GetVulkanGraphicsRequirements)),
                "xrGetInstanceProcAddr(xrGetVulkanGraphicsRequirements2KHR)");
            CheckXr(
                xrGetInstanceProcAddr(
                    Instance,
                    "xrGetVulkanGraphicsDevice2KHR",
                    reinterpret_cast<PFN_xrVoidFunction*>(&GetVulkanGraphicsDevice)),
                "xrGetInstanceProcAddr(xrGetVulkanGraphicsDevice2KHR)");

            uint32_t blendModeCount{};
            CheckXr(
                xrEnumerateEnvironmentBlendModes(
                    Instance,
                    SystemId,
                    VIEW_CONFIGURATION_TYPE,
                    0,
                    &blendModeCount,
                    nullptr),
                "xrEnumerateEnvironmentBlendModes");
            std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
            CheckXr(
                xrEnumerateEnvironmentBlendModes(
                    Instance,
                    SystemId,
                    VIEW_CONFIGURATION_TYPE,
                    blendModeCount,
                    &blendModeCount,
                    blendModes.data()),
                "xrEnumerateEnvironmentBlendModes");
            if (std::find(blendModes.begin(), blendModes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != blendModes.end())
            {
                BlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            }
            else if (!blendModes.empty())
            {
                BlendMode = blendModes.front();
            }

            return true;
        }

        std::string AppName;
        XrInstance Instance{XR_NULL_HANDLE};
        XrSystemId SystemId{XR_NULL_SYSTEM_ID};
        XrEnvironmentBlendMode BlendMode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
        PFN_xrGetVulkanGraphicsRequirements2KHR GetVulkanGraphicsRequirements{};
        PFN_xrGetVulkanGraphicsDevice2KHR GetVulkanGraphicsDevice{};
    };

    struct System::Session::Impl
    {
        struct ViewSwapchain
        {
            XrSwapchain Handle{XR_NULL_HANDLE};
            std::vector<XrSwapchainImageVulkan2KHR> Images{};
            uint32_t Width{};
            uint32_t Height{};
            uint32_t ImageIndex{};
            bool ImageAcquired{};
        };

        Impl(System::Impl& system, const VulkanGraphicsBinding& graphicsBinding)
            : System{system}
        {
            if (graphicsBinding.Instance == 0 ||
                graphicsBinding.PhysicalDevice == 0 ||
                graphicsBinding.Device == 0 ||
                graphicsBinding.Queue == 0 ||
                graphicsBinding.ApiVersion == 0)
            {
                throw std::runtime_error{"NativeXR received an incomplete Vulkan graphics binding."};
            }

            const auto vulkanInstance = reinterpret_cast<VkInstance>(graphicsBinding.Instance);
            const auto vulkanPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(graphicsBinding.PhysicalDevice);
            const auto vulkanDevice = reinterpret_cast<VkDevice>(graphicsBinding.Device);
            auto graphicsRequirements =
                MakeXr<XrGraphicsRequirementsVulkan2KHR>(XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR);
            CheckXr(
                System.GetVulkanGraphicsRequirements(System.Instance, System.SystemId, &graphicsRequirements),
                "xrGetVulkanGraphicsRequirements2KHR");
            const auto xrApiVersion = XR_MAKE_VERSION(
                VK_API_VERSION_MAJOR(graphicsBinding.ApiVersion),
                VK_API_VERSION_MINOR(graphicsBinding.ApiVersion),
                VK_API_VERSION_PATCH(graphicsBinding.ApiVersion));
            if (xrApiVersion < graphicsRequirements.minApiVersionSupported ||
                xrApiVersion > graphicsRequirements.maxApiVersionSupported)
            {
                throw std::runtime_error{
                    "NativeWebGPU Vulkan API version " +
                    std::to_string(VK_API_VERSION_MAJOR(graphicsBinding.ApiVersion)) + "." +
                    std::to_string(VK_API_VERSION_MINOR(graphicsBinding.ApiVersion)) + "." +
                    std::to_string(VK_API_VERSION_PATCH(graphicsBinding.ApiVersion)) +
                    " is outside the OpenXR runtime range [" +
                    std::to_string(XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported)) + "." +
                    std::to_string(XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported)) + "." +
                    std::to_string(XR_VERSION_PATCH(graphicsRequirements.minApiVersionSupported)) + ", " +
                    std::to_string(XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported)) + "." +
                    std::to_string(XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported)) + "." +
                    std::to_string(XR_VERSION_PATCH(graphicsRequirements.maxApiVersionSupported)) + "]."};
            }

            auto deviceGetInfo =
                MakeXr<XrVulkanGraphicsDeviceGetInfoKHR>(XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR);
            deviceGetInfo.systemId = System.SystemId;
            deviceGetInfo.vulkanInstance = vulkanInstance;
            VkPhysicalDevice runtimePhysicalDevice{VK_NULL_HANDLE};
            CheckXr(
                System.GetVulkanGraphicsDevice(System.Instance, &deviceGetInfo, &runtimePhysicalDevice),
                "xrGetVulkanGraphicsDevice2KHR");
            if (runtimePhysicalDevice != vulkanPhysicalDevice)
            {
                throw std::runtime_error{"NativeWebGPU selected a Vulkan physical device that does not match the OpenXR runtime."};
            }

            auto xrGraphicsBinding =
                MakeXr<XrGraphicsBindingVulkan2KHR>(XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR);
            xrGraphicsBinding.instance = vulkanInstance;
            xrGraphicsBinding.physicalDevice = vulkanPhysicalDevice;
            xrGraphicsBinding.device = vulkanDevice;
            xrGraphicsBinding.queueFamilyIndex = graphicsBinding.QueueFamilyIndex;
            xrGraphicsBinding.queueIndex = graphicsBinding.QueueIndex;

            auto sessionCreateInfo = MakeXr<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
            sessionCreateInfo.next = &xrGraphicsBinding;
            sessionCreateInfo.systemId = System.SystemId;
            CheckXr(xrCreateSession(System.Instance, &sessionCreateInfo, &Session), "xrCreateSession");

            auto spaceCreateInfo = MakeXr<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
            spaceCreateInfo.referenceSpaceType = REFERENCE_SPACE_TYPE;
            spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
            CheckXr(xrCreateReferenceSpace(Session, &spaceCreateInfo, &AppSpace), "xrCreateReferenceSpace");

            uint32_t viewCount{};
            CheckXr(
                xrEnumerateViewConfigurationViews(
                    System.Instance,
                    System.SystemId,
                    VIEW_CONFIGURATION_TYPE,
                    0,
                    &viewCount,
                    nullptr),
                "xrEnumerateViewConfigurationViews");
            ViewConfigurationViews.assign(
                viewCount,
                MakeXr<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW));
            CheckXr(
                xrEnumerateViewConfigurationViews(
                    System.Instance,
                    System.SystemId,
                    VIEW_CONFIGURATION_TYPE,
                    viewCount,
                    &viewCount,
                    ViewConfigurationViews.data()),
                "xrEnumerateViewConfigurationViews");
            if (viewCount == 0)
            {
                throw std::runtime_error{"The OpenXR runtime exposed no primary stereo views."};
            }

            uint32_t formatCount{};
            CheckXr(xrEnumerateSwapchainFormats(Session, 0, &formatCount, nullptr), "xrEnumerateSwapchainFormats");
            std::vector<int64_t> formats(formatCount);
            CheckXr(
                xrEnumerateSwapchainFormats(Session, formatCount, &formatCount, formats.data()),
                "xrEnumerateSwapchainFormats");
            ColorFormat = SelectColorFormat(formats);
            ColorTextureFormat = ToTextureFormat(ColorFormat);

            ViewSwapchains.resize(viewCount);
            for (uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
            {
                const auto& viewConfiguration = ViewConfigurationViews[viewIndex];
                auto& viewSwapchain = ViewSwapchains[viewIndex];
                viewSwapchain.Width = viewConfiguration.recommendedImageRectWidth;
                viewSwapchain.Height = viewConfiguration.recommendedImageRectHeight;

                auto swapchainCreateInfo = MakeXr<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
                swapchainCreateInfo.usageFlags =
                    XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
                swapchainCreateInfo.format = ColorFormat;
                swapchainCreateInfo.sampleCount = 1;
                swapchainCreateInfo.width = viewSwapchain.Width;
                swapchainCreateInfo.height = viewSwapchain.Height;
                swapchainCreateInfo.faceCount = 1;
                swapchainCreateInfo.arraySize = 1;
                swapchainCreateInfo.mipCount = 1;
                CheckXr(
                    xrCreateSwapchain(Session, &swapchainCreateInfo, &viewSwapchain.Handle),
                    "xrCreateSwapchain");

                uint32_t imageCount{};
                CheckXr(
                    xrEnumerateSwapchainImages(viewSwapchain.Handle, 0, &imageCount, nullptr),
                    "xrEnumerateSwapchainImages");
                viewSwapchain.Images.assign(
                    imageCount,
                    MakeXr<XrSwapchainImageVulkan2KHR>(XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR));
                CheckXr(
                    xrEnumerateSwapchainImages(
                        viewSwapchain.Handle,
                        imageCount,
                        &imageCount,
                        reinterpret_cast<XrSwapchainImageBaseHeader*>(viewSwapchain.Images.data())),
                    "xrEnumerateSwapchainImages");
            }

            LocatedViews.assign(viewCount, MakeXr<XrView>(XR_TYPE_VIEW));
            ActiveFrameViews.resize(viewCount);
        }

        ~Impl()
        {
            if (FrameBegun)
            {
                try
                {
                    EndFrame(false);
                }
                catch (...)
                {
                }
            }
            for (auto& viewSwapchain : ViewSwapchains)
            {
                if (viewSwapchain.Handle != XR_NULL_HANDLE)
                {
                    xrDestroySwapchain(viewSwapchain.Handle);
                }
            }
            if (AppSpace != XR_NULL_HANDLE)
            {
                xrDestroySpace(AppSpace);
            }
            if (Session != XR_NULL_HANDLE)
            {
                xrDestroySession(Session);
            }
        }

        void PollEvents()
        {
            auto event = MakeXr<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
            while (xrPollEvent(System.Instance, &event) == XR_SUCCESS)
            {
                if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
                {
                    const auto& stateChanged = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                    SessionState = stateChanged.state;
                    LogOpenXrInfo("session state", SessionState);
                    if (SessionState == XR_SESSION_STATE_READY && !SessionRunning)
                    {
                        auto beginInfo = MakeXr<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
                        beginInfo.primaryViewConfigurationType = VIEW_CONFIGURATION_TYPE;
                        CheckXr(xrBeginSession(Session, &beginInfo), "xrBeginSession");
                        SessionRunning = true;
                    }
                    else if (SessionState == XR_SESSION_STATE_STOPPING && SessionRunning)
                    {
                        CheckXr(xrEndSession(Session), "xrEndSession");
                        SessionRunning = false;
                    }
                    else if (SessionState == XR_SESSION_STATE_EXITING ||
                             SessionState == XR_SESSION_STATE_LOSS_PENDING)
                    {
                        SessionRunning = false;
                        SessionEnded = true;
                    }
                }
                else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
                {
                    SessionRunning = false;
                    SessionEnded = true;
                }

                event = MakeXr<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
            }
        }

        void WaitUntilRunning()
        {
            LogOpenXrInfo("waiting for READY", SessionState);
            constexpr auto timeout = std::chrono::seconds{10};
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (!SessionRunning && !SessionEnded && std::chrono::steady_clock::now() < deadline)
            {
                PollEvents();
                if (!SessionRunning)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds{2});
                }
            }
            if (!SessionRunning && !SessionEnded)
            {
                throw std::runtime_error{"Timed out waiting for the OpenXR session to become ready."};
            }
            LogOpenXrInfo("READY wait completed", SessionState);
        }

        std::unique_ptr<Session::Frame> GetNextFrame(
            bool& shouldEndSession,
            bool& shouldRestartSession,
            std::function<arcana::task<void, std::exception_ptr>(void*)>)
        {
            shouldRestartSession = false;
            PollEvents();

            if (ExitRequested && !ExitRequestSent && SessionRunning)
            {
                CheckXr(xrRequestExitSession(Session), "xrRequestExitSession");
                ExitRequestSent = true;
            }

            if (!SessionRunning && !SessionEnded)
            {
                WaitUntilRunning();
            }

            if (ExitRequested && !SessionEnded)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
                while (SessionRunning && std::chrono::steady_clock::now() < deadline)
                {
                    PollEvents();
                    if (SessionRunning)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds{2});
                    }
                }
                SessionEnded = !SessionRunning;
            }

            shouldEndSession = SessionEnded;
            if (SessionEnded)
            {
                ActiveFrameViews.clear();
                return std::make_unique<Session::Frame>(*this);
            }

            auto waitInfo = MakeXr<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
            CheckXr(xrWaitFrame(Session, &waitInfo, &FrameState), "xrWaitFrame");
            auto beginInfo = MakeXr<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
            CheckXr(xrBeginFrame(Session, &beginInfo), "xrBeginFrame");
            FrameBegun = true;
            FrameShouldRender = FrameState.shouldRender == XR_TRUE;

            auto locateInfo = MakeXr<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
            locateInfo.viewConfigurationType = VIEW_CONFIGURATION_TYPE;
            locateInfo.displayTime = FrameState.predictedDisplayTime;
            locateInfo.space = AppSpace;
            auto viewState = MakeXr<XrViewState>(XR_TYPE_VIEW_STATE);
            uint32_t locatedViewCount{};
            CheckXr(
                xrLocateViews(
                    Session,
                    &locateInfo,
                    &viewState,
                    static_cast<uint32_t>(LocatedViews.size()),
                    &locatedViewCount,
                    LocatedViews.data()),
                "xrLocateViews");
            FrameShouldRender =
                FrameShouldRender &&
                (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0 &&
                (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;

            if (FrameShouldRender)
            {
                for (auto& viewSwapchain : ViewSwapchains)
                {
                    auto acquireInfo =
                        MakeXr<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
                    CheckXr(
                        xrAcquireSwapchainImage(
                            viewSwapchain.Handle,
                            &acquireInfo,
                            &viewSwapchain.ImageIndex),
                        "xrAcquireSwapchainImage");
                    auto imageWaitInfo =
                        MakeXr<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
                    imageWaitInfo.timeout = XR_INFINITE_DURATION;
                    CheckXr(
                        xrWaitSwapchainImage(viewSwapchain.Handle, &imageWaitInfo),
                        "xrWaitSwapchainImage");
                    viewSwapchain.ImageAcquired = true;
                }
            }

            ActiveFrameViews.resize(locatedViewCount);
            for (uint32_t viewIndex = 0; viewIndex < locatedViewCount; ++viewIndex)
            {
                auto& activeView = ActiveFrameViews[viewIndex];
                const auto& locatedView = LocatedViews[viewIndex];
                const auto& viewSwapchain = ViewSwapchains[viewIndex];
                activeView.Space.Pose = ToPose(locatedView.pose);
                activeView.ProjectionMatrix = CreateProjectionMatrix(locatedView.fov, DepthNearZ, DepthFarZ);
                activeView.ColorTextureFormat = ColorTextureFormat;
                activeView.ColorTexturePointer = FrameShouldRender
                    ? reinterpret_cast<void*>(viewSwapchain.Images[viewSwapchain.ImageIndex].image)
                    : nullptr;
                activeView.ColorTextureSize = {viewSwapchain.Width, viewSwapchain.Height, 1};
                activeView.DepthTextureFormat = TextureFormat::D24S8;
                activeView.DepthTexturePointer = nullptr;
                activeView.DepthTextureSize = activeView.ColorTextureSize;
                activeView.DepthNearZ = DepthNearZ;
                activeView.DepthFarZ = DepthFarZ;
                activeView.RequiresAppClear = true;
            }

            return std::make_unique<Session::Frame>(*this);
        }

        void EndFrame(bool renderLayer)
        {
            if (!FrameBegun)
            {
                return;
            }

            std::vector<XrCompositionLayerProjectionView> projectionViews;
            auto projectionLayer =
                MakeXr<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
            const XrCompositionLayerBaseHeader* layerHeader{};
            if (renderLayer && FrameShouldRender)
            {
                projectionViews.resize(
                    ActiveFrameViews.size(),
                    MakeXr<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW));
                for (uint32_t viewIndex = 0; viewIndex < projectionViews.size(); ++viewIndex)
                {
                    auto& projectionView = projectionViews[viewIndex];
                    const auto& viewSwapchain = ViewSwapchains[viewIndex];
                    projectionView.pose = LocatedViews[viewIndex].pose;
                    projectionView.fov = LocatedViews[viewIndex].fov;
                    projectionView.subImage.swapchain = viewSwapchain.Handle;
                    projectionView.subImage.imageRect = {
                        {0, 0},
                        {
                            static_cast<int32_t>(viewSwapchain.Width),
                            static_cast<int32_t>(viewSwapchain.Height),
                        }};
                    projectionView.subImage.imageArrayIndex = 0;
                }
                projectionLayer.space = AppSpace;
                projectionLayer.viewCount = static_cast<uint32_t>(projectionViews.size());
                projectionLayer.views = projectionViews.data();
                layerHeader = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer);
            }

            for (auto& viewSwapchain : ViewSwapchains)
            {
                if (viewSwapchain.ImageAcquired)
                {
                    auto releaseInfo =
                        MakeXr<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
                    CheckXr(
                        xrReleaseSwapchainImage(viewSwapchain.Handle, &releaseInfo),
                        "xrReleaseSwapchainImage");
                    viewSwapchain.ImageAcquired = false;
                }
            }

            auto endInfo = MakeXr<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
            endInfo.displayTime = FrameState.predictedDisplayTime;
            endInfo.environmentBlendMode = System.BlendMode;
            endInfo.layerCount = layerHeader == nullptr ? 0 : 1;
            endInfo.layers = layerHeader == nullptr ? nullptr : &layerHeader;
            CheckXr(xrEndFrame(Session, &endInfo), "xrEndFrame");
            FrameBegun = false;
        }

        void RequestEndSession()
        {
            ExitRequested = true;
        }

        System::Impl& System;
        XrSession Session{XR_NULL_HANDLE};
        XrSpace AppSpace{XR_NULL_HANDLE};
        XrSessionState SessionState{XR_SESSION_STATE_UNKNOWN};
        XrFrameState FrameState{MakeXr<XrFrameState>(XR_TYPE_FRAME_STATE)};
        std::vector<XrViewConfigurationView> ViewConfigurationViews{};
        std::vector<ViewSwapchain> ViewSwapchains{};
        std::vector<XrView> LocatedViews{};
        std::vector<Session::Frame::View> ActiveFrameViews{};
        std::vector<Session::Frame::InputSource> InputSources{};
        std::vector<FeaturePoint> FeaturePointCloud{};
        std::optional<Space> EyeTrackerSpace{};
        std::vector<ImageTrackingScore> ImageTrackingScores{};
        int64_t ColorFormat{};
        TextureFormat ColorTextureFormat{TextureFormat::RGBA8_SRGB};
        float DepthNearZ{System::DEFAULT_DEPTH_NEAR_Z};
        float DepthFarZ{System::DEFAULT_DEPTH_FAR_Z};
        bool SessionRunning{};
        bool SessionEnded{};
        bool FrameBegun{};
        bool FrameShouldRender{};
        bool ExitRequested{};
        bool ExitRequestSent{};
    };

    struct System::Session::Frame::Impl
    {
        explicit Impl(Session::Impl& sessionImpl)
            : SessionImpl{sessionImpl}
        {
        }

        Session::Impl& SessionImpl;
    };

    System::System(const char* appName)
        : m_impl{std::make_unique<System::Impl>(appName)}
    {
    }

    System::~System() = default;

    bool System::IsInitialized() const
    {
        return m_impl->Instance != XR_NULL_HANDLE;
    }

    bool System::TryInitialize()
    {
        try
        {
            return m_impl->TryInitialize();
        }
        catch (const std::exception& error)
        {
            LogOpenXrError("initialization failed", error.what());
            return false;
        }
        catch (...)
        {
            LogOpenXrError("initialization failed", "unknown exception");
            return false;
        }
    }

    arcana::task<bool, std::exception_ptr> System::IsSessionSupportedAsync(SessionType sessionType)
    {
        return arcana::task_from_result<std::exception_ptr>(
            sessionType == SessionType::IMMERSIVE_VR || sessionType == SessionType::IMMERSIVE_AR);
    }

    uintptr_t System::GetNativeXrContext()
    {
        return reinterpret_cast<uintptr_t>(m_impl->Instance);
    }

    std::string System::GetNativeXrContextType()
    {
        return "OpenXR";
    }

    arcana::task<std::shared_ptr<System::Session>, std::exception_ptr> System::Session::CreateAsync(
        System& system,
        void* graphicsDevice,
        void*,
        std::function<void*()>)
    {
        try
        {
            if (graphicsDevice == nullptr)
            {
                throw std::runtime_error{"NativeXR received a null Vulkan graphics binding."};
            }
            auto session = std::make_shared<System::Session>(
                system,
                graphicsDevice,
                nullptr,
                std::function<void*()>{});
            return arcana::task_from_result<std::exception_ptr>(std::move(session));
        }
        catch (...)
        {
            try
            {
                throw;
            }
            catch (const std::exception& error)
            {
                LogOpenXrError("session creation failed", error.what());
            }
            catch (...)
            {
                LogOpenXrError("session creation failed", "unknown exception");
            }
            return arcana::task_from_error<std::shared_ptr<System::Session>>(std::current_exception());
        }
    }

    System::Session::Session(
        System& system,
        void* graphicsDevice,
        void*,
        std::function<void*()>)
        : m_impl{std::make_unique<System::Session::Impl>(
              *system.m_impl,
              *static_cast<const VulkanGraphicsBinding*>(graphicsDevice))}
    {
    }

    System::Session::~Session() = default;

    std::unique_ptr<System::Session::Frame> System::Session::GetNextFrame(
        bool& shouldEndSession,
        bool& shouldRestartSession,
        std::function<arcana::task<void, std::exception_ptr>(void*)> deletedTextureAsyncCallback)
    {
        return m_impl->GetNextFrame(
            shouldEndSession,
            shouldRestartSession,
            std::move(deletedTextureAsyncCallback));
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
    }

    bool System::Session::TrySetFeaturePointCloudEnabled(bool) const
    {
        return false;
    }

    bool System::Session::TrySetPreferredPlaneDetectorOptions(const GeometryDetectorOptions&)
    {
        return false;
    }

    bool System::Session::TrySetMeshDetectorEnabled(bool)
    {
        return false;
    }

    bool System::Session::TrySetPreferredMeshDetectorOptions(const GeometryDetectorOptions&)
    {
        return false;
    }

    std::vector<ImageTrackingScore>* System::Session::GetImageTrackingScores() const
    {
        return &m_impl->ImageTrackingScores;
    }

    void System::Session::CreateAugmentedImageDatabase(const std::vector<ImageTrackingRequest>&) const
    {
    }

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
        , IsTracking{sessionImpl.FrameShouldRender}
        , m_impl{std::make_unique<System::Session::Frame::Impl>(sessionImpl)}
    {
    }

    System::Session::Frame::~Frame() = default;

    void System::Session::Frame::Render()
    {
        m_impl->SessionImpl.EndFrame(true);
    }

    void System::Session::Frame::GetHitTestResults(
        std::vector<HitResult>& results,
        Ray,
        HitTestTrackableType) const
    {
        results.clear();
    }

    Anchor System::Session::Frame::CreateAnchor(Pose, NativeTrackablePtr) const
    {
        return Anchor{{}, nullptr, false};
    }

    Anchor System::Session::Frame::DeclareAnchor(NativeAnchorPtr) const
    {
        return Anchor{{}, nullptr, false};
    }

    void System::Session::Frame::UpdateAnchor(Anchor& anchor) const
    {
        anchor.IsValid = false;
    }

    void System::Session::Frame::DeleteAnchor(Anchor& anchor) const
    {
        anchor.IsValid = false;
    }

    System::Session::Frame::SceneObject& System::Session::Frame::GetSceneObjectByID(SceneObject::Identifier) const
    {
        throw std::runtime_error{"OpenXR scene object detection is not implemented."};
    }

    System::Session::Frame::Plane& System::Session::Frame::GetPlaneByID(Plane::Identifier) const
    {
        throw std::runtime_error{"OpenXR plane detection is not implemented."};
    }

    System::Session::Frame::Mesh& System::Session::Frame::GetMeshByID(Mesh::Identifier) const
    {
        throw std::runtime_error{"OpenXR mesh detection is not implemented."};
    }

    System::Session::Frame::ImageTrackingResult& System::Session::Frame::GetImageTrackingResultByID(
        ImageTrackingResult::Identifier) const
    {
        throw std::runtime_error{"OpenXR image tracking is not implemented."};
    }
}
