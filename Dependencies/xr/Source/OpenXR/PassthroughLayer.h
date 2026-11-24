#pragma once

#include "OpenXRHelpers.h"

namespace xr
{
    // Reconstruction belongs to the compositor. No camera stream, image import
    // or application-visible camera permission is needed.
    class PassthroughLayer
    {
    public:
        PassthroughLayer() = default;
        PassthroughLayer(const PassthroughLayer&) = delete;
        PassthroughLayer& operator=(const PassthroughLayer&) = delete;
        ~PassthroughLayer() { Reset(); }

        void Open(XrInstance instance, XrSession session, PFN_xrGetInstanceProcAddr lookup)
        {
            if (m_layer) return;
            PFN_xrCreatePassthroughFB create{};
            PFN_xrCreatePassthroughLayerFB createLayer{};
            const auto load = [&](auto& proc, const char* name) {
                CheckXr(lookup(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&proc)), name);
                if (!proc) throw std::runtime_error{name};
            };
            load(create, "xrCreatePassthroughFB");
            load(createLayer, "xrCreatePassthroughLayerFB");
            load(m_destroy, "xrDestroyPassthroughFB");
            load(m_destroyLayer, "xrDestroyPassthroughLayerFB");
            auto feature = MakeXr<XrPassthroughCreateInfoFB>(XR_TYPE_PASSTHROUGH_CREATE_INFO_FB);
            feature.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
            XrPassthroughFB created{};
            CheckXr(create(session, &feature, &created), "xrCreatePassthroughFB");
            m_feature = created;
            auto layer = MakeXr<XrPassthroughLayerCreateInfoFB>(XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB);
            layer.passthrough = m_feature;
            layer.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
            layer.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
            XrPassthroughLayerFB child{};
            const auto result = createLayer(session, &layer, &child);
            if (XR_FAILED(result))
            {
                Reset();
                CheckXr(result, "xrCreatePassthroughLayerFB");
            }
            m_layer = child;
        }

        XrPassthroughLayerFB Handle() const { return m_layer; }
        void Reset()
        {
            if (m_layer) m_destroyLayer(m_layer);
            if (m_feature) m_destroy(m_feature);
            m_layer = XR_NULL_HANDLE;
            m_feature = XR_NULL_HANDLE;
        }
    private:
        XrPassthroughFB m_feature{};
        XrPassthroughLayerFB m_layer{};
        PFN_xrDestroyPassthroughFB m_destroy{};
        PFN_xrDestroyPassthroughLayerFB m_destroyLayer{};
    };
}
