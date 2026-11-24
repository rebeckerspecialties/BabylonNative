#include <XR.h>
#include "ProjectionExtent.h"

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
#include "CompositionLayers.h"
#include "SpaceWarpSwapchains.h"

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
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_R16G16B16A16_SFLOAT: // The application draws into an RGBA8 intermediate.
                return TextureFormat::RGBA8_SRGB;
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SRGB:
                return TextureFormat::BGRA8_SRGB;
            default:
                throw std::runtime_error{"The OpenXR runtime did not expose a WebGPU-compatible UNORM color swapchain format."};
            }
        }

        int64_t SelectColorFormat(const std::vector<int64_t>& formats, bool allowSrgb = true, bool preferFloat = false)
        {
            if (preferFloat && std::find(formats.begin(), formats.end(), VK_FORMAT_R16G16B16A16_SFLOAT) != formats.end())
            {
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            }
            constexpr std::array<int64_t, 4> preferredFormats{
                VK_FORMAT_R8G8B8A8_SRGB,
                VK_FORMAT_B8G8R8A8_SRGB,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_FORMAT_B8G8R8A8_UNORM};
            for (const auto preferred : preferredFormats)
            {
                if (!allowSrgb && (preferred == VK_FORMAT_R8G8B8A8_SRGB || preferred == VK_FORMAT_B8G8R8A8_SRGB))
                {
                    continue;
                }
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

            auto currentActivity = android::global::TryGetCurrentActivity();
            if (!currentActivity)
            {
                return false;
            }
            auto appContext = android::global::GetAppContext();

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
            __android_log_print(ANDROID_LOG_INFO, "BabylonNative",
                "OpenXR advertised graphics extensions: visibilityMask=%d foveation=%d foveationVulkan=%d swapchainUpdateState=%d",
                HasExtension(extensions, XR_KHR_VISIBILITY_MASK_EXTENSION_NAME),
                HasExtension(extensions, XR_FB_FOVEATION_EXTENSION_NAME),
                HasExtension(extensions, XR_FB_FOVEATION_VULKAN_EXTENSION_NAME),
                HasExtension(extensions, XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME));
            AppOwnedVulkan = HasExtension(extensions, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
            if (!HasExtension(extensions, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) ||
                (!AppOwnedVulkan && !HasExtension(extensions, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)))
            {
                return false;
            }

            auto androidCreateInfo = MakeXr<XrInstanceCreateInfoAndroidKHR>(XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR);
            androidCreateInfo.applicationVM = javaVm;
            androidCreateInfo.applicationActivity = static_cast<jobject>(*currentActivity);

            std::vector<const char*> enabledExtensions{
                XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                AppOwnedVulkan ? XR_KHR_VULKAN_ENABLE_EXTENSION_NAME : XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
            DisplayRefreshRateSupported = HasExtension(extensions, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
            const auto enable = [&](const char* name) {
                if (!HasExtension(extensions, name)) return false;
                enabledExtensions.push_back(name);
                return true;
            };
            Cylinder = enable(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
            const bool convertTime = enable(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME);
            AndroidSurface = enable(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
            const bool querySpaceWarp = enable(XR_FB_SPACE_WARP_EXTENSION_NAME);
            constexpr const char* densityExtensions[] = {XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,
                XR_FB_FOVEATION_EXTENSION_NAME, XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME, XR_FB_FOVEATION_VULKAN_EXTENSION_NAME};
            Foveation = std::all_of(std::begin(densityExtensions), std::end(densityExtensions),
                [&](const char* name) { return HasExtension(extensions, name); });
            if (Foveation)
            {
                enabledExtensions.insert(enabledExtensions.end(), std::begin(densityExtensions), std::end(densityExtensions));
            }
            Cube = enable(XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME);
            Equirect2 = enable(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME);
            Equirect = Equirect2 || enable(XR_KHR_COMPOSITION_LAYER_EQUIRECT_EXTENSION_NAME);
            if (DisplayRefreshRateSupported)
            {
                enabledExtensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
            }
            auto createInfo = MakeXr<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
            createInfo.next = &androidCreateInfo;
            std::strncpy(createInfo.applicationInfo.applicationName, AppName.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
            std::strncpy(createInfo.applicationInfo.engineName, "BabylonNative", XR_MAX_ENGINE_NAME_SIZE - 1);
            createInfo.applicationInfo.applicationVersion = 1;
            createInfo.applicationInfo.engineVersion = 1;
            createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
            createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
            createInfo.enabledExtensionNames = enabledExtensions.data();
            auto instanceResult = xrCreateInstance(&createInfo, &Instance);
            if (instanceResult == XR_ERROR_API_VERSION_UNSUPPORTED)
            {
                createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
                instanceResult = xrCreateInstance(&createInfo, &Instance);
            }
            CheckXr(instanceResult, "xrCreateInstance");

            auto systemInfo = MakeXr<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
            systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
            CheckXr(xrGetSystem(Instance, &systemInfo, &SystemId), "xrGetSystem");
            auto properties = MakeXr<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
            auto warp = MakeXr<XrSystemSpaceWarpPropertiesFB>(XR_TYPE_SYSTEM_SPACE_WARP_PROPERTIES_FB);
            if (querySpaceWarp)
            {
                properties.next = &warp;
            }
            if (convertTime)
            {
                CheckXr(xrGetInstanceProcAddr(Instance, "xrConvertTimeToTimespecTimeKHR",
                    reinterpret_cast<PFN_xrVoidFunction*>(&ToMonotonicTime)), "load OpenXR time conversion");
            }
            CheckXr(xrGetSystemProperties(Instance, SystemId, &properties), "xrGetSystemProperties");
            MaxLayers = properties.graphicsProperties.maxLayerCount;
            MotionWidth = warp.recommendedMotionVectorImageRectWidth;
            MotionHeight = warp.recommendedMotionVectorImageRectHeight;
            __android_log_print(ANDROID_LOG_INFO, "BabylonNative",
                "OpenXR mediaSurface=%d spaceWarpExtension=%d recommended motion image=%ux%u",
                AndroidSurface, querySpaceWarp, MotionWidth, MotionHeight);

            CheckXr(
                xrGetInstanceProcAddr(
                    Instance,
                    AppOwnedVulkan ? "xrGetVulkanGraphicsRequirementsKHR" : "xrGetVulkanGraphicsRequirements2KHR",
                    reinterpret_cast<PFN_xrVoidFunction*>(&GetVulkanGraphicsRequirements)),
                "xrGetInstanceProcAddr(xrGetVulkanGraphicsRequirements2KHR)");
            if (AppOwnedVulkan)
            {
                CheckXr(xrGetInstanceProcAddr(Instance, "xrGetVulkanGraphicsDeviceKHR",
                    reinterpret_cast<PFN_xrVoidFunction*>(&GetAppVulkanGraphicsDevice)), "xrGetVulkanGraphicsDeviceKHR");
                for (const auto name : {"xrGetVulkanInstanceExtensionsKHR", "xrGetVulkanDeviceExtensionsKHR"})
                {
                    PFN_xrGetVulkanInstanceExtensionsKHR getExtensions{};
                    CheckXr(xrGetInstanceProcAddr(Instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&getExtensions)), name);
                    uint32_t size{};
                    CheckXr(getExtensions(Instance, SystemId, 0, &size, nullptr), name);
                    std::vector<char> list(size + 1);
                    CheckXr(getExtensions(Instance, SystemId, size, &size, list.data()), name);
                    __android_log_print(ANDROID_LOG_INFO, "BabylonNative", "%s: %s", name, list.data());
                }
            }
            else
            {
                CheckXr(
                xrGetInstanceProcAddr(
                    Instance,
                    "xrGetVulkanGraphicsDevice2KHR",
                    reinterpret_cast<PFN_xrVoidFunction*>(&GetVulkanGraphicsDevice)),
                "xrGetInstanceProcAddr(xrGetVulkanGraphicsDevice2KHR)");
            }

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
        PFN_xrGetVulkanGraphicsDeviceKHR GetAppVulkanGraphicsDevice{};
        bool AppOwnedVulkan{};
        bool DisplayRefreshRateSupported{};
        PFN_xrConvertTimeToTimespecTimeKHR ToMonotonicTime{};
        bool Cylinder{}, Cube{}, Equirect{}, Equirect2{}, AndroidSurface{};
        uint32_t MaxLayers{};
        uint32_t MotionWidth{}, MotionHeight{};
        bool Foveation{};
    };

    struct System::Session::Impl
    {
        struct ViewSwapchain
        {
            XrSwapchain Handle{XR_NULL_HANDLE};
            std::vector<XrSwapchainImageVulkan2KHR> Images{};
            std::vector<XrSwapchainImageFoveationVulkanFB> DensityMaps{};
            uint32_t Width{};
            uint32_t Height{};
            uint32_t ImageIndex{};
            bool ImageAcquired{};
            ProjectionExtent MaxApplicationExtent{};
        };

        Impl(System::Impl& system, const VulkanGraphicsBinding& graphicsBinding)
            : System{system}
        {
            Layers.WaitForSubmittedWork = graphicsBinding.WaitForSubmittedWork;
            if (graphicsBinding.Instance == 0 ||
                graphicsBinding.PhysicalDevice == 0 ||
                graphicsBinding.Device == 0 ||
                graphicsBinding.Queue == 0 ||
                graphicsBinding.ApiVersion == 0 ||
                graphicsBinding.MaxTextureDimension2D == 0)
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
                0);
            const auto majorMinor = [](XrVersion version) { return XR_MAKE_VERSION(XR_VERSION_MAJOR(version), XR_VERSION_MINOR(version), 0); };
            if (xrApiVersion < majorMinor(graphicsRequirements.minApiVersionSupported))
            {
                throw std::runtime_error{
                    "NativeWebGPU Vulkan API version " +
                    std::to_string(VK_API_VERSION_MAJOR(graphicsBinding.ApiVersion)) + "." +
                    std::to_string(VK_API_VERSION_MINOR(graphicsBinding.ApiVersion)) + "." +
                    std::to_string(VK_API_VERSION_PATCH(graphicsBinding.ApiVersion)) +
                    " is below the OpenXR runtime minimum " +
                    std::to_string(XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported)) + "." +
                    std::to_string(XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported)) + "."};
            }
            if (xrApiVersion > majorMinor(graphicsRequirements.maxApiVersionSupported))
            {
                // The upper bound is the newest version tested, not a compatibility ceiling.
                __android_log_print(ANDROID_LOG_WARN, "BabylonNative",
                    "Vulkan %u.%u exceeds the OpenXR runtime's tested %u.%u; attempting session creation",
                    VK_API_VERSION_MAJOR(graphicsBinding.ApiVersion), VK_API_VERSION_MINOR(graphicsBinding.ApiVersion),
                    static_cast<unsigned>(XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported)),
                    static_cast<unsigned>(XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported)));
            }

            auto deviceGetInfo =
                MakeXr<XrVulkanGraphicsDeviceGetInfoKHR>(XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR);
            deviceGetInfo.systemId = System.SystemId;
            deviceGetInfo.vulkanInstance = vulkanInstance;
            VkPhysicalDevice runtimePhysicalDevice{VK_NULL_HANDLE};
            CheckXr(
                System.AppOwnedVulkan ? System.GetAppVulkanGraphicsDevice(System.Instance, System.SystemId, vulkanInstance, &runtimePhysicalDevice)
                                      : System.GetVulkanGraphicsDevice(System.Instance, &deviceGetInfo, &runtimePhysicalDevice),
                "OpenXR Vulkan physical device selection");
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
            if (System.DisplayRefreshRateSupported)
            {
                PFN_xrEnumerateDisplayRefreshRatesFB enumerateRates{};
                PFN_xrGetDisplayRefreshRateFB getRate{};
                PFN_xrRequestDisplayRefreshRateFB requestRate{};
                uint32_t count{};
                float current{};
                if (XR_SUCCEEDED(xrGetInstanceProcAddr(System.Instance, "xrEnumerateDisplayRefreshRatesFB", reinterpret_cast<PFN_xrVoidFunction*>(&enumerateRates))) &&
                    XR_SUCCEEDED(xrGetInstanceProcAddr(System.Instance, "xrGetDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&getRate))) &&
                    XR_SUCCEEDED(xrGetInstanceProcAddr(System.Instance, "xrRequestDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&requestRate))) &&
                    enumerateRates && getRate && requestRate && XR_SUCCEEDED(getRate(Session, &current)) && current > 0 &&
                    XR_SUCCEEDED(enumerateRates(Session, 0, &count, nullptr)) && count)
                {
                    std::vector<float> rates(count);
                    if (XR_SUCCEEDED(enumerateRates(Session, count, &count, rates.data())))
                    {
                        rates.resize(count);
                        DisplayRates = std::make_shared<DisplayRateControl>();
                        DisplayRates->Supported = std::move(rates);
                        DisplayRates->Nominal.store(current);
                        DisplayRates->Request = [requestRate, session = Session](float rate) {
                            return XR_SUCCEEDED(requestRate(session, rate));
                        };
                    }
                }
            }
            else
            {
                __android_log_print(ANDROID_LOG_INFO, "BabylonNative", "OpenXR display refresh rate selection unavailable");
            }

            auto spaceCreateInfo = MakeXr<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
            spaceCreateInfo.referenceSpaceType = REFERENCE_SPACE_TYPE;
            spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
            CheckXr(xrCreateReferenceSpace(Session, &spaceCreateInfo, &AppSpace), "xrCreateReferenceSpace");
            spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
            CheckXr(xrCreateReferenceSpace(Session,&spaceCreateInfo,&HeadSpace),"create viewer reference space");

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
            if (System.MotionWidth && System.MotionHeight)
            {
                const auto offered = [&formats](VkFormat format) {
                    return std::find(formats.begin(), formats.end(), format) != formats.end();
                };
                if (offered(VK_FORMAT_R16G16B16A16_SFLOAT))
                    for (const auto depth : {VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32,
                        VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT})
                        if (offered(depth)) WarpDepths.push_back(depth);
                __android_log_print(ANDROID_LOG_INFO, "BabylonNative",
                    "OpenXR advertised warp swapchain formats: RGBA16F=%d D16=%d D24S8=%d D32F=%d",
                    offered(VK_FORMAT_R16G16B16A16_SFLOAT), offered(VK_FORMAT_D16_UNORM),
                    offered(VK_FORMAT_D24_UNORM_S8_UINT), offered(VK_FORMAT_D32_SFLOAT));
            }
#ifdef BABYLON_NATIVE_OPENXR_PREFER_LINEAR_SWAPCHAIN
            ColorFormat = SelectColorFormat(formats, true, true);
#else
            ColorFormat = SelectColorFormat(formats);
#endif
            ColorTextureFormat = ToTextureFormat(ColorFormat);

            // The projection layer exposes one array image per frame, not one
            // independently acquired image per view.
            ViewSwapchains.resize(1);
            {
                auto& viewSwapchain = ViewSwapchains.front();
                viewSwapchain.MaxApplicationExtent = {graphicsBinding.MaxTextureDimension2D, graphicsBinding.MaxTextureDimension2D};
                for (const auto& config : ViewConfigurationViews)
                {
                    viewSwapchain.Width = std::max(viewSwapchain.Width, config.recommendedImageRectWidth);
                    viewSwapchain.Height = std::max(viewSwapchain.Height, config.recommendedImageRectHeight);
                    viewSwapchain.MaxApplicationExtent.Width = std::min(viewSwapchain.MaxApplicationExtent.Width, config.maxImageRectWidth);
                    viewSwapchain.MaxApplicationExtent.Height = std::min(viewSwapchain.MaxApplicationExtent.Height, config.maxImageRectHeight);
                }
                viewSwapchain.Width = std::min(viewSwapchain.Width, viewSwapchain.MaxApplicationExtent.Width);
                viewSwapchain.Height = std::min(viewSwapchain.Height, viewSwapchain.MaxApplicationExtent.Height);

                auto swapchainCreateInfo = MakeXr<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
                swapchainCreateInfo.usageFlags =
                    XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
                swapchainCreateInfo.format = ColorFormat;
                swapchainCreateInfo.sampleCount = 1;
                swapchainCreateInfo.width = viewSwapchain.Width;
                swapchainCreateInfo.height = viewSwapchain.Height;
                swapchainCreateInfo.faceCount = 1;
                swapchainCreateInfo.arraySize = viewCount;
                swapchainCreateInfo.mipCount = 1;
                // Babylon writes display-encoded color to its UNORM WebGPU
                // view. Tell OpenXR to decode those bytes as sRGB at composition.
                const bool srgb = ColorFormat == VK_FORMAT_R8G8B8A8_SRGB || ColorFormat == VK_FORMAT_B8G8R8A8_SRGB;
                if (srgb) swapchainCreateInfo.usageFlags |= XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
                auto foveation = MakeXr<XrSwapchainCreateInfoFoveationFB>(XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB);
                if (System.Foveation && graphicsBinding.SupportsNonSubsampledDensityMaps)
                {
                    foveation.flags = XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB;
                    swapchainCreateInfo.next = &foveation;
                }
                auto result = xrCreateSwapchain(Session, &swapchainCreateInfo, &viewSwapchain.Handle);
                if (swapchainCreateInfo.next && (result == XR_ERROR_FEATURE_UNSUPPORTED ||
                    result == XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED || result == XR_ERROR_LIMIT_REACHED))
                {
                    swapchainCreateInfo.next = nullptr;
                    result = xrCreateSwapchain(Session, &swapchainCreateInfo, &viewSwapchain.Handle);
                }
                if (ColorFormat == VK_FORMAT_R16G16B16A16_SFLOAT &&
                    (result == XR_ERROR_FEATURE_UNSUPPORTED || result == XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED))
                {
                    ColorFormat = SelectColorFormat(formats);
                    ColorTextureFormat = ToTextureFormat(ColorFormat);
                    swapchainCreateInfo.format = ColorFormat;
                    if (ColorFormat == VK_FORMAT_R8G8B8A8_SRGB || ColorFormat == VK_FORMAT_B8G8R8A8_SRGB)
                        swapchainCreateInfo.usageFlags |= XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
                    result = xrCreateSwapchain(Session, &swapchainCreateInfo, &viewSwapchain.Handle);
                }
                if ((swapchainCreateInfo.usageFlags & XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT) && (result == XR_ERROR_FEATURE_UNSUPPORTED || result == XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED))
                {
                    ColorFormat = SelectColorFormat(formats, false);
                    ColorTextureFormat = ToTextureFormat(ColorFormat);
                    swapchainCreateInfo.format = ColorFormat;
                    swapchainCreateInfo.usageFlags &= ~XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
                    result = xrCreateSwapchain(Session, &swapchainCreateInfo, &viewSwapchain.Handle);
                }
                CheckXr(result, "xrCreateSwapchain");
                __android_log_print(ANDROID_LOG_INFO, "BabylonNative",
                    "OpenXR Vulkan projection: %ux%u, %u slices, %u sample(s), format %d",
                    swapchainCreateInfo.width, swapchainCreateInfo.height, viewCount, swapchainCreateInfo.sampleCount, static_cast<int>(ColorFormat));

                uint32_t imageCount{};
                CheckXr(
                    xrEnumerateSwapchainImages(viewSwapchain.Handle, 0, &imageCount, nullptr),
                    "xrEnumerateSwapchainImages");
                viewSwapchain.Images.assign(
                    imageCount,
                    MakeXr<XrSwapchainImageVulkan2KHR>(XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR));
                if (swapchainCreateInfo.next)
                {
                    viewSwapchain.DensityMaps.assign(imageCount, MakeXr<XrSwapchainImageFoveationVulkanFB>(XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB));
                    for (uint32_t i = 0; i < imageCount; ++i)
                    {
                        viewSwapchain.Images[i].next = &viewSwapchain.DensityMaps[i];
                    }
                }
                CheckXr(
                    xrEnumerateSwapchainImages(
                        viewSwapchain.Handle,
                        imageCount,
                        &imageCount,
                        reinterpret_cast<XrSwapchainImageBaseHeader*>(viewSwapchain.Images.data())),
                    "xrEnumerateSwapchainImages");
                if (!viewSwapchain.DensityMaps.empty())
                {
                    const auto& maps = viewSwapchain.DensityMaps;
                    if (std::all_of(maps.begin(), maps.end(), [](const auto& map) { return map.image && map.width && map.height; }))
                    {
                        CheckXr(xrGetInstanceProcAddr(System.Instance, "xrCreateFoveationProfileFB", reinterpret_cast<PFN_xrVoidFunction*>(&CreateProfile)), "get create foveation profile");
                        CheckXr(xrGetInstanceProcAddr(System.Instance, "xrUpdateSwapchainFB", reinterpret_cast<PFN_xrVoidFunction*>(&UpdateSwapchain)), "get update swapchain");
                        CheckXr(xrGetInstanceProcAddr(System.Instance, "xrDestroyFoveationProfileFB", reinterpret_cast<PFN_xrVoidFunction*>(&DestroyProfile)), "get destroy foveation profile");
                        __android_log_print(ANDROID_LOG_INFO, "BabylonNative", "OpenXR supplied %u paired Vulkan density maps, first %ux%u",
                            static_cast<unsigned>(maps.size()), maps.front().width, maps.front().height);
                    }
                    else
                    {
                        for (auto& image : viewSwapchain.Images) image.next = nullptr;
                        viewSwapchain.DensityMaps.clear();
                    }
                }
            }

            LocatedViews.assign(viewCount, MakeXr<XrView>(XR_TYPE_VIEW));
            ActiveFrameViews.resize(viewCount);
            InitializeControllers();
        }

        XrPath Path(const std::string& name) const
        {
            XrPath path{};
            CheckXr(xrStringToPath(System.Instance, name.c_str(), &path), "xrStringToPath");
            return path;
        }

        void InitializeControllers()
        {
            const std::array<std::string, 2> hands{"/user/hand/left", "/user/hand/right"};
            HandPaths = {Path(hands[0]), Path(hands[1])};
            auto setInfo = MakeXr<XrActionSetCreateInfo>(XR_TYPE_ACTION_SET_CREATE_INFO);
            std::strcpy(setInfo.actionSetName, "controllers");
            std::strcpy(setInfo.localizedActionSetName, "Controllers");
            CheckXr(xrCreateActionSet(System.Instance, &setInfo, &ControllerActions), "xrCreateActionSet");
            const auto action = [&](XrActionType type, const char* name) {
                auto info = MakeXr<XrActionCreateInfo>(XR_TYPE_ACTION_CREATE_INFO);
                info.actionType = type;
                std::strncpy(info.actionName, name, sizeof(info.actionName) - 1);
                std::strncpy(info.localizedActionName, name, sizeof(info.localizedActionName) - 1);
                info.countSubactionPaths = static_cast<uint32_t>(HandPaths.size());
                info.subactionPaths = HandPaths.data();
                XrAction result{};
                CheckXr(xrCreateAction(ControllerActions, &info, &result), "xrCreateAction");
                return result;
            };
            Grip = action(XR_ACTION_TYPE_POSE_INPUT, "grip");
            Aim = action(XR_ACTION_TYPE_POSE_INPUT, "aim");
            Trigger = action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger");
            Squeeze = action(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze");
            Stick = action(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick");
            StickClick = action(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click");
            Primary = action(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary");
            Secondary = action(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary");
            TouchProfile = Path("/interaction_profiles/oculus/touch_controller");
            std::vector<XrActionSuggestedBinding> bindings;
            for (size_t i = 0; i < hands.size(); ++i)
            {
                const auto bind = [&](XrAction a, const char* suffix) { bindings.push_back({a, Path(hands[i] + "/input/" + suffix)}); };
                bind(Grip, "grip/pose");
                bind(Aim, "aim/pose");
                bind(Trigger, "trigger/value");
                bind(Squeeze, "squeeze/value");
                bind(Stick, "thumbstick");
                bind(StickClick, "thumbstick/click");
                bind(Primary, i ? "a/click" : "x/click");
                bind(Secondary, i ? "b/click" : "y/click");
            }
            auto suggestions = MakeXr<XrInteractionProfileSuggestedBinding>(XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);
            suggestions.interactionProfile = TouchProfile;
            suggestions.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
            suggestions.suggestedBindings = bindings.data();
            auto result = xrSuggestInteractionProfileBindings(System.Instance, &suggestions);
            if (result == XR_ERROR_PATH_UNSUPPORTED) return;
            CheckXr(result, "xrSuggestInteractionProfileBindings");
            auto attach = MakeXr<XrSessionActionSetsAttachInfo>(XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO);
            attach.countActionSets = 1;
            attach.actionSets = &ControllerActions;
            CheckXr(xrAttachSessionActionSets(Session, &attach), "xrAttachSessionActionSets");
            InputSources.resize(2);
            for (size_t i = 0; i < InputSources.size(); ++i)
            {
                auto space = MakeXr<XrActionSpaceCreateInfo>(XR_TYPE_ACTION_SPACE_CREATE_INFO);
                space.poseInActionSpace.orientation.w = 1.f;
                space.subactionPath = HandPaths[i];
                space.action = Grip;
                CheckXr(xrCreateActionSpace(Session, &space, &GripSpaces[i]), "xrCreateActionSpace(grip)");
                space.action = Aim;
                CheckXr(xrCreateActionSpace(Session, &space, &AimSpaces[i]), "xrCreateActionSpace(aim)");
                auto& src = InputSources[i];
                src.Handedness = i ? Session::Frame::InputSource::HandednessEnum::Right : Session::Frame::InputSource::HandednessEnum::Left;
                src.GamepadObject.Buttons.resize(6);
                src.GamepadObject.Axes.resize(4);
            }
        }

        void UpdateControllers()
        {
            if (InputSources.empty()) return;
            XrActiveActionSet active{ControllerActions, XR_NULL_PATH};
            auto sync = MakeXr<XrActionsSyncInfo>(XR_TYPE_ACTIONS_SYNC_INFO);
            sync.countActiveActionSets = 1;
            sync.activeActionSets = &active;
            const auto synced = xrSyncActions(Session, &sync);
            CheckXr(synced, "xrSyncActions");
            for (size_t i = 0; i < InputSources.size(); ++i)
            {
                auto& src = InputSources[i];
                src.TrackedThisFrame = src.GamepadTrackedThisFrame = false;
                if (synced == XR_SESSION_NOT_FOCUSED) continue;
                auto get = MakeXr<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
                get.subactionPath = HandPaths[i];
                const auto locate = [&](XrAction action, XrSpace space, Space& target) {
                    get.action = action;
                    auto state = MakeXr<XrActionStatePose>(XR_TYPE_ACTION_STATE_POSE);
                    CheckXr(xrGetActionStatePose(Session, &get, &state), "xrGetActionStatePose");
                    if (!state.isActive) return false;
                    auto location = MakeXr<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
                    CheckXr(xrLocateSpace(space, AppSpace, FrameState.predictedDisplayTime, &location), "xrLocateSpace(controller)");
                    constexpr auto valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
                    if ((location.locationFlags & valid) != valid) return false;
                    target.Pose = ToPose(location.pose);
                    return true;
                };
                if (!locate(Grip, GripSpaces[i], src.GripSpace) || !locate(Aim, AimSpaces[i], src.AimSpace)) continue;
                src.TrackedThisFrame = src.GamepadTrackedThisFrame = true;
                if (src.InteractionProfileName.empty()) {
                    auto profile = MakeXr<XrInteractionProfileState>(XR_TYPE_INTERACTION_PROFILE_STATE);
                    CheckXr(xrGetCurrentInteractionProfile(Session, HandPaths[i], &profile), "xrGetCurrentInteractionProfile");
                    if (profile.interactionProfile == TouchProfile) src.InteractionProfileName = "oculus-touch-v2";
                }
                const auto axis = [&](XrAction action) {
                    get.action = action;
                    auto state = MakeXr<XrActionStateFloat>(XR_TYPE_ACTION_STATE_FLOAT);
                    CheckXr(xrGetActionStateFloat(Session, &get, &state), "xrGetActionStateFloat");
                    return state.isActive ? state.currentState : 0.f;
                };
                const auto button = [&](XrAction action) {
                    get.action = action;
                    auto state = MakeXr<XrActionStateBoolean>(XR_TYPE_ACTION_STATE_BOOLEAN);
                    CheckXr(xrGetActionStateBoolean(Session, &get, &state), "xrGetActionStateBoolean");
                    return state.isActive && state.currentState;
                };
                auto& pad = src.GamepadObject;
                const auto trigger = axis(Trigger), squeeze = axis(Squeeze);
                pad.Buttons[0] = {trigger >= .5f, trigger > 0.f, trigger};
                pad.Buttons[1] = {squeeze >= .5f, squeeze > 0.f, squeeze};
                const std::array<bool, 3> clicks{button(StickClick), button(Primary), button(Secondary)};
                for (size_t n = 0; n < clicks.size(); ++n) pad.Buttons[n + 3] = {clicks[n], clicks[n], clicks[n] ? 1.f : 0.f};
                get.action = Stick;
                auto stick = MakeXr<XrActionStateVector2f>(XR_TYPE_ACTION_STATE_VECTOR2F);
                CheckXr(xrGetActionStateVector2f(Session, &get, &stick), "xrGetActionStateVector2f");
                pad.Axes[2] = stick.isActive ? stick.currentState.x : 0.f;
                pad.Axes[3] = stick.isActive ? -stick.currentState.y : 0.f;
            }
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
            Layers.Clear();
            WarpChains.clear();
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
            if (HeadSpace != XR_NULL_HANDLE) xrDestroySpace(HeadSpace);
            if (Session != XR_NULL_HANDLE)
            {
                xrDestroySession(Session);
            }
            if (ControllerActions != XR_NULL_HANDLE) xrDestroyActionSet(ControllerActions);
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
                    Layers.Visibility(SessionState == XR_SESSION_STATE_VISIBLE || SessionState == XR_SESSION_STATE_FOCUSED);
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
                else if (event.type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB && DisplayRates)
                {
                    const auto& changed = reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB&>(event);
                    DisplayRates->Nominal.store(changed.toDisplayRefreshRate);
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

        std::shared_ptr<DisplayRateControl> DisplayRates{};

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
            UpdateControllers();

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
                    const int level = RequestedFoveation.load();
                    if (CreateProfile && ActiveFoveation != level)
                    {
                        auto config = MakeXr<XrFoveationLevelProfileCreateInfoFB>(XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB);
                        config.level = static_cast<XrFoveationLevelFB>(level);
                        config.dynamic = XR_FOVEATION_DYNAMIC_DISABLED_FB;
                        auto info = MakeXr<XrFoveationProfileCreateInfoFB>(XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB);
                        info.next = &config;
                        XrFoveationProfileFB profile{};
                        CheckXr(CreateProfile(Session, &info, &profile), "create foveation profile");
                        auto state = MakeXr<XrSwapchainStateFoveationFB>(XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB);
                        state.profile = profile;
                        const auto result = UpdateSwapchain(viewSwapchain.Handle, reinterpret_cast<const XrSwapchainStateBaseHeaderFB*>(&state));
                        DestroyProfile(profile);
                        CheckXr(result, "apply projection foveation profile");
                        ActiveFoveation = level;
                    }
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
            HeadPose.reset();
            auto head = MakeXr<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
            CheckXr(xrLocateSpace(HeadSpace,AppSpace,FrameState.predictedDisplayTime,&head),"locate viewer reference space");
            constexpr auto valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if ((head.locationFlags & valid) == valid) HeadPose = Space{ToPose(head.pose)};
            const double projectionScale = ProjectionScale.load();
            for (uint32_t viewIndex = 0; viewIndex < locatedViewCount; ++viewIndex)
            {
                auto& activeView = ActiveFrameViews[viewIndex];
                const auto& locatedView = LocatedViews[viewIndex];
                const auto& viewSwapchain = ViewSwapchains.front();
                activeView.Space.Pose = ToPose(locatedView.pose);
                activeView.ProjectionMatrix = CreateProjectionMatrix(locatedView.fov, DepthNearZ, DepthFarZ);
                activeView.ColorTextureFormat = ColorTextureFormat;
                activeView.ColorTexturePointer = FrameShouldRender
                    ? reinterpret_cast<void*>(viewSwapchain.Images[viewSwapchain.ImageIndex].image)
                    : nullptr;
                const auto extent = ScaleProjectionExtent({viewSwapchain.Width, viewSwapchain.Height},
                    viewSwapchain.MaxApplicationExtent, projectionScale);
                activeView.ColorTextureSize = {extent.Width, extent.Height, static_cast<uint32_t>(ViewConfigurationViews.size())};
                activeView.SwapchainWidth = viewSwapchain.Width;
                activeView.SwapchainHeight = viewSwapchain.Height;
                activeView.SwapchainFormat = ColorFormat;
                if (FrameShouldRender && ActiveFoveation > 0 && !viewSwapchain.DensityMaps.empty() &&
                    extent.Width == viewSwapchain.Width && extent.Height == viewSwapchain.Height)
                {
                    const auto& map = viewSwapchain.DensityMaps[viewSwapchain.ImageIndex];
                    activeView.DensityMapImage = reinterpret_cast<uint64_t>(map.image);
                    activeView.DensityMapWidth = map.width;
                    activeView.DensityMapHeight = map.height;
                }
                else
                {
                    activeView.DensityMapImage = 0;
                    activeView.DensityMapWidth = activeView.DensityMapHeight = 0;
                }
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
            std::vector<XrCompositionLayerSpaceWarpInfoFB> warpViews;
            auto projectionLayer =
                MakeXr<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
            XrCompositionLayerProjection* projection{};
            if (renderLayer && FrameShouldRender)
            {
                projectionViews.resize(
                    ActiveFrameViews.size(),
                    MakeXr<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW));
                if (ActiveWarp && WarpSubmitted) warpViews.resize(projectionViews.size());
                for (uint32_t viewIndex = 0; viewIndex < projectionViews.size(); ++viewIndex)
                {
                    auto& projectionView = projectionViews[viewIndex];
                    const auto& viewSwapchain = ViewSwapchains.front();
                    projectionView.pose = LocatedViews[viewIndex].pose;
                    projectionView.fov = LocatedViews[viewIndex].fov;
                    projectionView.subImage.swapchain = viewSwapchain.Handle;
                    projectionView.subImage.imageRect = {
                        {0, 0},
                        {
                            static_cast<int32_t>(viewSwapchain.Width),
                            static_cast<int32_t>(viewSwapchain.Height),
                        }};
                    projectionView.subImage.imageArrayIndex = viewIndex;
                    if (!warpViews.empty())
                    {
                        warpViews[viewIndex] = ActiveWarp->View(viewIndex, WarpDelta, DepthNearZ, DepthFarZ);
                        projectionView.next = &warpViews[viewIndex];
                    }
                }
                projectionLayer.space = AppSpace;
                projectionLayer.viewCount = static_cast<uint32_t>(projectionViews.size());
                projectionLayer.views = projectionViews.data();
                projection = &projectionLayer;
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

            if (ActiveWarp)
            {
                ActiveWarp->Motion.Release();
                ActiveWarp->Depth.Release();
                ActiveWarp = nullptr;
                WarpSubmitted = false;
            }
            auto endInfo = MakeXr<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
            endInfo.displayTime = FrameState.predictedDisplayTime;
            endInfo.environmentBlendMode = System.BlendMode;
            const bool submittedProjection = Layers.Finish(Session, AppSpace, HeadSpace, endInfo, projection, renderLayer && FrameShouldRender, System.Equirect2, System.MaxLayers);
            if (submittedProjection && warpViews.size() == 2 && !LoggedWarpSubmission)
            {
                __android_log_print(ANDROID_LOG_INFO, "BabylonNative", "Submitted stereo motion and depth to OpenXR compositor");
                LoggedWarpSubmission = true;
            }
            if (submittedProjection && projectionViews.size() == 2 && !LoggedStereoSubmission)
            {
                __android_log_print(ANDROID_LOG_INFO, "BabylonNative", "Submitted stereo frame to OpenXR compositor");
                LoggedStereoSubmission = true;
            }
#ifndef NDEBUG
            if (projectionViews.size() == 2 && ++StereoFrames % 240 == 1)
            {
                const auto& left = projectionViews[0].pose;
                const auto& right = projectionViews[1].pose;
                __android_log_print(ANDROID_LOG_DEBUG, "BabylonNative", "Submitted stereo frame %u L=(%.3f,%.3f,%.3f) R=(%.3f,%.3f,%.3f) q=(%.3f,%.3f,%.3f,%.3f)",
                    StereoFrames, left.position.x, left.position.y, left.position.z, right.position.x, right.position.y, right.position.z,
                    left.orientation.x, left.orientation.y, left.orientation.z, left.orientation.w);
            }
#endif
            FrameBegun = false;
        }

        void RequestEndSession()
        {
            ExitRequested = true;
        }

        System::Impl& System;
        bool LoggedStereoSubmission{};
        bool LoggedWarpSubmission{};
#ifndef NDEBUG
        uint32_t StereoFrames{};
#endif
        XrSession Session{XR_NULL_HANDLE};
        XrSpace AppSpace{XR_NULL_HANDLE};
        XrSpace HeadSpace{XR_NULL_HANDLE};
        std::optional<Space> HeadPose;
        XrSessionState SessionState{XR_SESSION_STATE_UNKNOWN};
        XrFrameState FrameState{MakeXr<XrFrameState>(XR_TYPE_FRAME_STATE)};
        std::vector<XrViewConfigurationView> ViewConfigurationViews{};
        std::vector<ViewSwapchain> ViewSwapchains{};
        CompositionLayers Layers;
        std::vector<int64_t> WarpDepths;
        std::unordered_map<int64_t, std::unique_ptr<SpaceWarpSwapchains>> WarpChains;
        SpaceWarpSwapchains* ActiveWarp{};
        XrPosef WarpDelta{};
        bool WarpSubmitted{};
        std::atomic<double> ProjectionScale{1.0};
        std::atomic<int> RequestedFoveation{};
        int ActiveFoveation{};
        PFN_xrCreateFoveationProfileFB CreateProfile{};
        PFN_xrUpdateSwapchainFB UpdateSwapchain{};
        PFN_xrDestroyFoveationProfileFB DestroyProfile{};
        std::vector<XrView> LocatedViews{};
        std::vector<Session::Frame::View> ActiveFrameViews{};
        std::vector<Session::Frame::InputSource> InputSources{};
        XrActionSet ControllerActions{};
        XrAction Grip{}, Aim{}, Trigger{}, Squeeze{}, Stick{}, StickClick{}, Primary{}, Secondary{};
        XrPath TouchProfile{};
        std::array<XrPath, 2> HandPaths{};
        std::array<XrSpace, 2> GripSpaces{}, AimSpaces{};
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
            sessionType == SessionType::IMMERSIVE_VR);
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
        m_displayRateControl = m_impl->DisplayRates;
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

    void System::Session::SetProjectionScale(double scale)
    {
        m_impl->ProjectionScale.store(scale);
    }

    bool System::Session::SupportsLayer(LayerShape shape) const
    {
        return shape == LayerShape::Quad || (shape == LayerShape::Cube && m_impl->System.Cube) ||
            (shape == LayerShape::Cylinder && m_impl->System.Cylinder) || (shape == LayerShape::Equirect && m_impl->System.Equirect);
    }
    uint32_t System::Session::MaxLayerCount() const { return m_impl->System.MaxLayers; }
    bool System::Session::SupportsMediaLayers() const { return m_impl->System.AndroidSurface; }
    bool System::Session::SupportsFoveation(double scale) const
    {
        if (!m_impl->CreateProfile || m_impl->ViewSwapchains.empty()) return false;
        const auto& chain = m_impl->ViewSwapchains.front();
        const auto extent = ScaleProjectionExtent({chain.Width, chain.Height}, chain.MaxApplicationExtent, scale);
        return extent.Width == chain.Width && extent.Height == chain.Height;
    }

    void System::Session::SetFoveation(double amount)
    {
        m_impl->RequestedFoveation.store(amount <= 0 ? 0 : amount <= 1.0/3 ? 1 : amount <= 2.0/3 ? 2 : 3);
    }

    bool System::Session::SupportsWarpDepth(int64_t format) const
    {
        return std::find(m_impl->WarpDepths.begin(), m_impl->WarpDepths.end(), format) != m_impl->WarpDepths.end();
    }

    WarpImages System::Session::AcquireWarpImages(int64_t format)
    {
        if (!m_impl->FrameBegun || !m_impl->FrameShouldRender || !SupportsWarpDepth(format))
            throw std::runtime_error{"No renderable OpenXR frame with compatible space-warp attachments"};
        auto& chain = m_impl->WarpChains[format];
        const auto width = m_impl->System.MotionWidth;
        const auto height = m_impl->System.MotionHeight;
        const auto views = static_cast<uint32_t>(m_impl->ViewConfigurationViews.size());
        if (m_impl->ActiveWarp && m_impl->ActiveWarp != chain.get())
            throw std::runtime_error{"Cannot change space-warp attachments within an OpenXR frame"};
        if (!chain) chain = std::make_unique<SpaceWarpSwapchains>(m_impl->Session, static_cast<VkFormat>(format), width, height, views);
        m_impl->ActiveWarp = chain.get();
        return {{reinterpret_cast<uint64_t>(chain->Motion.Acquire()), VK_FORMAT_R16G16B16A16_SFLOAT},
            {reinterpret_cast<uint64_t>(chain->Depth.Acquire()), format}, width, height, views};
    }

    void System::Session::MarkWarpImagesSubmitted(const Pose& delta)
    {
        if (!m_impl->ActiveWarp) throw std::runtime_error{"No acquired space-warp images"};
        m_impl->WarpDelta = {{delta.Orientation.X, delta.Orientation.Y, delta.Orientation.Z, delta.Orientation.W},
            {delta.Position.X, delta.Position.Y, delta.Position.Z}};
        m_impl->WarpSubmitted = true;
    }
    uint64_t System::Session::CreateMediaLayer(LayerShape shape, uint32_t width, uint32_t height, LayerLayout layout, std::shared_ptr<MediaSurfaceProducer> source)
    {
        PFN_xrCreateSwapchainAndroidSurfaceKHR create{};
        CheckXr(xrGetInstanceProcAddr(m_impl->System.Instance, "xrCreateSwapchainAndroidSurfaceKHR", reinterpret_cast<PFN_xrVoidFunction*>(&create)), "get Android surface swapchain function");
        return m_impl->Layers.CreateMedia(m_impl->Session, create, shape, width, height, layout, std::move(source));
    }
    uint64_t System::Session::CreateLayer(LayerShape shape, uint32_t width, uint32_t height, LayerLayout layout)
    {
        if (!SupportsLayer(shape)) throw std::runtime_error{"Layer shape unavailable in this OpenXR runtime"};
        return m_impl->Layers.Create(m_impl->Session, shape, m_impl->ColorFormat, width, height, layout);
    }
    LayerImage System::Session::AcquireLayer(uint64_t id) { return {m_impl->Layers.Acquire(id), m_impl->ColorFormat}; }
    void System::Session::MarkLayerRendered(uint64_t id) { m_impl->Layers.MarkRendered(id); }
    void System::Session::RetireLayer(uint64_t id) { m_impl->Layers.Retire(id); }
    void System::Session::SetLayerOrder(std::vector<LayerPlacement> layers) { m_impl->Layers.SetOrder(std::move(layers)); }

    TextureFormat System::Session::GetColorTextureFormat() const
    {
        return m_impl->ColorTextureFormat;
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
        , ViewerSpace{sessionImpl.HeadPose}
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
        timespec display{}, now{};
        if (sessionImpl.System.ToMonotonicTime &&
            XR_SUCCEEDED(sessionImpl.System.ToMonotonicTime(sessionImpl.System.Instance, sessionImpl.FrameState.predictedDisplayTime, &display)) &&
            clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        {
            PredictedDisplayTime = std::chrono::steady_clock::now() + std::chrono::seconds(display.tv_sec - now.tv_sec)
                + std::chrono::nanoseconds(display.tv_nsec - now.tv_nsec);
        }
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
