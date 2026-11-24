#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <arcana/threading/task.h>

namespace xr
{
    enum class TextureFormat
    {
        RGBA8_SRGB,
        BGRA8_SRGB,
        D24S8,
        D32FS8,
        D16
    };

    enum class SessionType
    {
        IMMERSIVE_VR,
        IMMERSIVE_AR,
        INLINE,
        INVALID
    };

    enum class PolygonFormat
    {
        XZ,
        XYZ
    };

    enum class HitTestTrackableType {
        NONE = 0,
        POINT = 1 << 0,
        PLANE = 1 << 1,
        MESH = 1 << 2,
    };

    enum class ImageTrackingScore
    {
        UNTRACKABLE,
        TRACKABLE,
    };

    enum class ImageTrackingState
    {
        UNTRACKED,
        TRACKED,
        EMULATED,
    };

    constexpr enum HitTestTrackableType operator |(const enum HitTestTrackableType selfValue, const enum HitTestTrackableType inValue)
    {
        return static_cast<const enum HitTestTrackableType>(std::underlying_type_t<HitTestTrackableType>(selfValue) | std::underlying_type_t<HitTestTrackableType>(inValue));
    }

    constexpr enum HitTestTrackableType operator &(const enum HitTestTrackableType selfValue, const enum HitTestTrackableType inValue)
    {
        return static_cast<const enum HitTestTrackableType>(std::underlying_type_t<HitTestTrackableType>(selfValue) & std::underlying_type_t<HitTestTrackableType>(inValue));
    }

    constexpr enum HitTestTrackableType& operator |=(enum HitTestTrackableType& selfValue, const enum HitTestTrackableType inValue)
    {
        selfValue = selfValue | inValue;
        return selfValue;
    }

    struct Size
    {
        size_t Width{};
        size_t Height{};
        size_t Depth{};
    };

    // Opaque Vulkan handles supplied by the embedding graphics backend.
    // Keeping Vulkan headers out of this public interface lets the existing
    // ARKit/Metal implementation remain unchanged on Apple platforms.
    struct VulkanGraphicsBinding
    {
        uintptr_t Instance{};
        uintptr_t PhysicalDevice{};
        uintptr_t Device{};
        uintptr_t Queue{};
        uint32_t ApiVersion{};
        uint32_t QueueFamilyIndex{};
        uint32_t QueueIndex{};
        uint32_t MaxTextureDimension2D{};
        bool (*WaitForSubmittedWork)(){};
        bool SupportsNonSubsampledDensityMaps{};
    };

    struct Vector3f
    {
        float X{0.f};
        float Y{0.f};
        float Z{0.f};
    };

    struct Vector4f
    {
        float X{0.f};
        float Y{0.f};
        float Z{0.f};
        float W{1.f};
    };

    struct Pose
    {
        Vector3f Position;
        Vector4f Orientation;
    };

    struct Space
    {
        xr::Pose Pose;
    };

#if defined(BABYLON_NATIVE_XR_OPENXR) && defined(__ANDROID__)
    enum class LayerShape { Projection, Quad, Cylinder, Equirect, Cube };
    enum class LayerLayout { Mono, Array, LeftRight, TopBottom };
    struct LayerPlacement
    {
        uint64_t Id{}; // Zero denotes the projection swapchain.
        Pose Transform{};
        float Width{1}, Height{1}, Radius{2}, Angle{1}, Aspect{2}, Upper{1.5707963f}, Lower{-1.5707963f};
        bool BlendAlpha{true};
        bool ViewerRelative{};
        static LayerPlacement Projection(bool blendAlpha)
        {
            LayerPlacement placement{};
            placement.BlendAlpha = blendAlpha;
            return placement;
        }
    };
    struct LayerImage { uint64_t Image; int64_t Format; uint64_t RightCubeImage{}; };
    struct WarpImages
    {
        LayerImage Motion;
        LayerImage Depth;
        uint32_t Width, Height, Views;
    };
    class MediaSurfaceProducer
    {
    public:
        virtual ~MediaSurfaceProducer() = default;
        virtual void SetSurface(void* surface) = 0;
    };
#endif

    using NativeTrackablePtr = void*;
    struct HitResult
    {
        xr::Pose Pose{};
        NativeTrackablePtr NativeTrackable{};
    };

    struct Ray
    {
        Vector3f Origin;
        Vector3f Direction;
    };

    using NativeAnchorPtr = void*;
    struct Anchor
    {
        xr::Space Space{};
        NativeAnchorPtr NativeAnchor{};
        bool IsValid{true};
    };

    struct FeaturePoint
    {
        using Identifier = size_t;

        float X{};
        float Y{};
        float Z{};
        float ConfidenceValue{};
        Identifier ID{};
    };

    struct FieldOfView
    {
        float AngleLeft;
        float AngleRight;
        float AngleUp;
        float AngleDown;
    };

    enum class DetectionBoundaryType
    {
        Box,
        Frustum,
        Sphere
    };

    struct Frustum
    {
        xr::Pose Pose{};
        FieldOfView FOV{};
        float FarDistance;
    };

    struct DetectionBoundary
    {
        DetectionBoundaryType Type{ DetectionBoundaryType::Sphere };
        std::variant<float, Frustum, Vector3f> Data{ 5.f };
    };

    struct GeometryDetectorOptions
    {
        xr::DetectionBoundary DetectionBoundary{};
        double UpdateInterval{10};
    };

    enum class SceneObjectType
    {
        Unknown,
        Background,
        Wall,
        Floor,
        Ceiling,
        Platform,
        Inferred,
        Undefined
    };

    const std::map<xr::SceneObjectType, std::string> SceneObjectTypeNames
    {
        {xr::SceneObjectType::Unknown, "unknown" },
        {xr::SceneObjectType::Background, "background" },
        {xr::SceneObjectType::Ceiling, "ceiling" },
        {xr::SceneObjectType::Floor, "floor" },
        {xr::SceneObjectType::Platform, "platform" },
        {xr::SceneObjectType::Wall, "wall" },
        {xr::SceneObjectType::Inferred, "inferred" }
    };

    class System
    {
    public:
        static constexpr float DEFAULT_DEPTH_NEAR_Z{ 0.5f };
        static constexpr float DEFAULT_DEPTH_FAR_Z{ 1000.f };
        static constexpr uint32_t DEFAULT_CONTROLLER_BUTTONS_COUNT{ 4 };
        static constexpr uint32_t DEFAULT_CONTROLLER_AXES_COUNT{ 4 };

        class Session
        {
            friend class System;
            struct Impl;

        public:
            struct ImageTrackingRequest
            {
                std::shared_ptr<const std::vector<uint8_t>> data{};
                uint32_t width{0};
                uint32_t height{0};
                uint32_t depth{0};
                uint32_t stride{0};
                float measuredWidthInMeters{0.0};
            };

            class Frame
            {
            public:
                struct JointSpace : Space
                {
                    float PoseRadius{};
                    bool PoseTracked{ false };
                };

                struct GamePad
                {
                    struct Button
                    {
                        bool Pressed{ false };
                        bool Touched{ false };
                        float Value{0};
                    };

                    std::vector<float> Axes{};
                    std::vector<Button> Buttons{};
                };

                struct View
                {
                    xr::Space Space{};
                    std::array<float, 16> ProjectionMatrix{};

                    TextureFormat ColorTextureFormat{};
                    void* ColorTexturePointer{};
                    Size ColorTextureSize;
#if defined(BABYLON_NATIVE_XR_OPENXR) && defined(__ANDROID__)
                    uint32_t SwapchainWidth{};
                    uint32_t SwapchainHeight{};
                    int64_t SwapchainFormat{};
                    uint64_t DensityMapImage{};
                    uint32_t DensityMapWidth{}, DensityMapHeight{};
#endif

                    TextureFormat DepthTextureFormat{};
                    void* DepthTexturePointer{};
                    Size DepthTextureSize;

                    float DepthNearZ{};
                    float DepthFarZ{};

                    bool IsFirstPersonObserver{ false };
                    bool RequiresAppClear{ false };
                };

                struct InputSource
                {
                    using Identifier = size_t;

                    enum class HandednessEnum
                    {
                        Left = 0,
                        Right = 1
                    };

                    const Identifier ID{ NEXT_ID++ };
                    bool TrackedThisFrame{};
                    bool JointsTrackedThisFrame{};
                    bool GamepadTrackedThisFrame{};
                    bool HandTrackedThisFrame{};
                    std::string InteractionProfileName{""};
                    GamePad GamepadObject{};
                    Space GripSpace{};
                    Space AimSpace{};
                    HandednessEnum Handedness{};
                    std::vector<JointSpace> HandJoints{};

                private:
                    static inline Identifier NEXT_ID{ 0 };
                };

                struct SceneObject
                {
                    using Identifier = int32_t;
                    const static Identifier INVALID_ID = -1;
                    Identifier ID{ NEXT_ID++ };
                    SceneObjectType Type{ SceneObjectType::Undefined };

                private:
                    static inline Identifier NEXT_ID{ 0 };
                };

                struct Plane
                {
                    using Identifier = size_t;
                    const Identifier ID{ NEXT_ID++ };
                    Pose Center{};
                    std::vector<float> Polygon{};
                    size_t PolygonSize{0};
                    xr::PolygonFormat PolygonFormat{};
                    SceneObject::Identifier ParentSceneObjectID{ SceneObject::INVALID_ID };

                private:
                    static inline Identifier NEXT_ID{ 0 };
                };

                struct Mesh
                {
                    using IndexType = uint32_t;
                    using Identifier = size_t;
                    const Identifier ID{ NEXT_ID++ };
                    std::vector<xr::Vector3f> Positions{};
                    std::vector<IndexType> Indices{};
                    bool HasNormals{ false };
                    std::vector<xr::Vector3f> Normals;
                    SceneObject::Identifier ParentSceneObjectID{ SceneObject::INVALID_ID };

                private:
                    static inline Identifier NEXT_ID{ 0 };
                };
                
                struct ImageTrackingResult
                {
                    using Identifier = size_t;
                    const Identifier ID{ NEXT_ID++ };
                    Space ImageSpace{};
                    uint32_t Index{0};
                    ImageTrackingState TrackingState{ImageTrackingState::UNTRACKED};
                    float MeasuredWidthInMeters{0};

                private:
                    static inline Identifier NEXT_ID{ 0 };
                };
                
                std::vector<View>& Views;
                std::optional<Space> ViewerSpace;
                std::optional<std::chrono::steady_clock::time_point> PredictedDisplayTime;
                std::vector<InputSource>& InputSources;
                std::vector<FeaturePoint>& FeaturePointCloud;

                std::optional<Space>& EyeTrackerSpace;

                std::vector<SceneObject::Identifier> UpdatedSceneObjects;
                std::vector<SceneObject::Identifier> RemovedSceneObjects;
                std::vector<Plane::Identifier> UpdatedPlanes;
                std::vector<Plane::Identifier> RemovedPlanes;
                std::vector<Mesh::Identifier> UpdatedMeshes;
                std::vector<Mesh::Identifier> RemovedMeshes;
                std::vector<ImageTrackingResult::Identifier> UpdatedImageTrackingResults;

                bool IsTracking;

                Frame(System::Session::Impl&);
                ~Frame();

                void GetHitTestResults(std::vector<HitResult>&, Ray, HitTestTrackableType) const;
                Anchor CreateAnchor(Pose, NativeAnchorPtr) const;
                Anchor DeclareAnchor(NativeAnchorPtr) const;
                void UpdateAnchor(Anchor&) const;
                void DeleteAnchor(Anchor&) const;
                SceneObject& GetSceneObjectByID(SceneObject::Identifier) const;
                Plane& GetPlaneByID(Plane::Identifier) const;
                Mesh& GetMeshByID(Mesh::Identifier) const;
                ImageTrackingResult& GetImageTrackingResultByID(ImageTrackingResult::Identifier) const;
                void Render();
            private:
                struct Impl;
                std::unique_ptr<Impl> m_impl{};
            };

            static arcana::task<std::shared_ptr<Session>, std::exception_ptr> CreateAsync(System& system, void* graphicsDevice, void* commandQueue, std::function<void*()> windowProvider);
            struct DisplayRateControl
            {
                std::vector<float> Supported;
                std::atomic<float> Nominal{};
                std::function<bool(float)> Request;
            };
            const DisplayRateControl* GetDisplayRateControl() const { return m_displayRateControl.get(); }
            ~Session();

            // Do not use, call CreateAsync instead. Kept public to keep compatibility with make_shared.
            // Move to private when changing to unique_ptr.
            Session(System& system, void* graphicsDevice, void* commandQueue, std::function<void*()> windowProvider);

            std::unique_ptr<Frame> GetNextFrame(bool& shouldEndSession, bool& shouldRestartSession, std::function<arcana::task<void, std::exception_ptr>(void*)> deletedTextureAsyncCallback = [](void*){ return arcana::task_from_result<std::exception_ptr>(); });
            void RequestEndSession();
            TextureFormat GetColorTextureFormat() const;
#if defined(BABYLON_NATIVE_XR_OPENXR) && defined(__ANDROID__)
            void SetProjectionScale(double scale);
            bool SupportsLayer(LayerShape shape) const;
            uint32_t MaxLayerCount() const;
            uint64_t CreateLayer(LayerShape shape, uint32_t width, uint32_t height, LayerLayout layout);
            uint64_t CreateMediaLayer(LayerShape shape, uint32_t width, uint32_t height, LayerLayout layout, std::shared_ptr<MediaSurfaceProducer> source);
            bool SupportsMediaLayers() const;
            bool SupportsWarpDepth(int64_t format) const;
            bool SupportsFoveation(double scale) const;
            void SetFoveation(double amount);
            WarpImages AcquireWarpImages(int64_t format);
            void MarkWarpImagesSubmitted(const Pose& delta);
            LayerImage AcquireLayer(uint64_t id);
            void MarkLayerRendered(uint64_t id);
            void RetireLayer(uint64_t id);
            void SetLayerOrder(std::vector<LayerPlacement> layers);
#endif
            void SetDepthsNearFar(float depthNear, float depthFar);
            void SetPlaneDetectionEnabled(bool enabled) const;
            bool TrySetFeaturePointCloudEnabled(bool enabled) const;

            bool TrySetPreferredPlaneDetectorOptions(const GeometryDetectorOptions& options);
            bool TrySetMeshDetectorEnabled(const bool enabled);
            bool TrySetPreferredMeshDetectorOptions(const GeometryDetectorOptions& options);

            std::vector<ImageTrackingScore>* GetImageTrackingScores() const;
            void CreateAugmentedImageDatabase(const std::vector<ImageTrackingRequest>&) const;
        private:
            std::shared_ptr<DisplayRateControl> m_displayRateControl{};
            std::unique_ptr<Impl> m_impl{};
        };

        System(const char* = "OpenXR Experience");
        ~System();

        bool IsInitialized() const;
        bool TryInitialize();
        static arcana::task<bool, std::exception_ptr> IsSessionSupportedAsync(SessionType);

        uintptr_t GetNativeXrContext();
        std::string GetNativeXrContextType();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl{};
    };
}
