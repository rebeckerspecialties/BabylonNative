#include "../../../Dependencies/xr/Source/OpenXR/PassthroughLayer.h"
#include <gtest/gtest.h>
#include <cstring>
#include <type_traits>
#include <vector>

namespace
{
    struct PassthroughTest : testing::Test
    {
        static inline std::vector<int> Calls;
        static inline XrResult FeatureResult, LayerResult;
        void SetUp() override { Calls.clear(); FeatureResult = LayerResult = XR_SUCCESS; }
        template<class T> static T Handle(uintptr_t n)
        {
            if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(n);
            else return static_cast<T>(n);
        }
        static XrResult XRAPI_CALL Create(XrSession, const XrPassthroughCreateInfoFB* info, XrPassthroughFB* feature)
        {
            Calls.push_back(1);
            EXPECT_EQ(info->flags, XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB);
            *feature = Handle<XrPassthroughFB>(1); // Outputs are not usable on failure.
            return FeatureResult;
        }
        static XrResult XRAPI_CALL CreateLayer(XrSession, const XrPassthroughLayerCreateInfoFB* info, XrPassthroughLayerFB* layer)
        {
            Calls.push_back(2);
            EXPECT_EQ(info->passthrough, Handle<XrPassthroughFB>(1));
            EXPECT_EQ(info->purpose, XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB);
            *layer = Handle<XrPassthroughLayerFB>(2);
            return LayerResult;
        }
        static XrResult XRAPI_CALL DestroyLayer(XrPassthroughLayerFB) { Calls.push_back(3); return XR_SUCCESS; }
        static XrResult XRAPI_CALL Destroy(XrPassthroughFB) { Calls.push_back(4); return XR_SUCCESS; }
        static XrResult XRAPI_CALL Lookup(XrInstance, const char* name, PFN_xrVoidFunction* proc)
        {
            if (!std::strcmp(name, "xrCreatePassthroughFB")) *proc = reinterpret_cast<PFN_xrVoidFunction>(Create);
            else if (!std::strcmp(name, "xrCreatePassthroughLayerFB")) *proc = reinterpret_cast<PFN_xrVoidFunction>(CreateLayer);
            else if (!std::strcmp(name, "xrDestroyPassthroughLayerFB")) *proc = reinterpret_cast<PFN_xrVoidFunction>(DestroyLayer);
            else if (!std::strcmp(name, "xrDestroyPassthroughFB")) *proc = reinterpret_cast<PFN_xrVoidFunction>(Destroy);
            else return XR_ERROR_FUNCTION_UNSUPPORTED;
            return XR_SUCCESS;
        }
    };

    TEST_F(PassthroughTest, InactiveSessionDoesNotAcquireOrReleasePassthrough)
    {
        { xr::PassthroughLayer unused; EXPECT_FALSE(unused.Handle()); }
        EXPECT_TRUE(Calls.empty());
    }

    TEST_F(PassthroughTest, RepeatedStartsAndStopsPreserveChildBeforeParentOrder)
    {
        {
            xr::PassthroughLayer layer;
            layer.Open({}, {}, Lookup);
            layer.Open({}, {}, Lookup);
            layer.Reset();
            layer.Reset();
            layer.Open({}, {}, Lookup);
        }
        EXPECT_EQ(Calls, (std::vector<int>{1, 2, 3, 4, 1, 2, 3, 4}));
    }

    TEST_F(PassthroughTest, PermissionFailureDoesNotCreateOrReleaseAChild)
    {
        FeatureResult = XR_ERROR_NOT_PERMITTED_PASSTHROUGH_FB;
        xr::PassthroughLayer layer;
        EXPECT_THROW(layer.Open({}, {}, Lookup), std::runtime_error);
        EXPECT_FALSE(layer.Handle());
        EXPECT_EQ(Calls, (std::vector<int>{1}));
    }

    TEST_F(PassthroughTest, PartialCreationReleasesParentAndCanRetry)
    {
        xr::PassthroughLayer layer;
        LayerResult = XR_ERROR_RUNTIME_FAILURE;
        EXPECT_THROW(layer.Open({}, {}, Lookup), std::runtime_error);
        EXPECT_FALSE(layer.Handle());
        LayerResult = XR_SUCCESS;
        layer.Open({}, {}, Lookup);
        layer.Reset();
        EXPECT_EQ(Calls, (std::vector<int>{1, 2, 4, 1, 2, 3, 4}));
    }
}
