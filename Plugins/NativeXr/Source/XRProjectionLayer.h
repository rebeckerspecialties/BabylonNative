#pragma once

#include <napi/napi.h>
#include <XR.h>

#include <cstdint>
#include <string>

namespace Babylon
{
    namespace Plugins
    {
        class XRSession;
    }

    class XRProjectionLayer final : public Napi::ObjectWrap<XRProjectionLayer>
    {
    public:
        static void Initialize(Napi::Env env);
        static Napi::Object New(Napi::Env env, const Napi::Object& session, const Napi::Object& init, const Napi::Object& device);

        explicit XRProjectionLayer(const Napi::CallbackInfo& info);

        bool BelongsTo(const Plugins::XRSession& session) const;
        bool UsesDevice(const Napi::Object& device) const;
        const std::string& ColorFormat() const;
        const std::string& DepthStencilFormat() const;
        uint32_t TextureUsage() const;
        double ScaleFactor() const { return m_scaleFactor; }
        double Foveation() const { return m_foveation; }
        bool BlendsSourceAlpha() const { return m_blendTextureSourceAlpha; }
        xr::Pose DeltaPose() const;

    private:
        Napi::ObjectReference m_sessionReference{};
        Napi::ObjectReference m_device{};
        Napi::ObjectReference m_deltaPose{};
        std::string m_colorFormat{};
        std::string m_depthStencilFormat{};
        uint32_t m_textureUsage{};
        double m_scaleFactor{1.0};
        double m_foveation{};
        bool m_ignoreDepthValues{};
        bool m_blendTextureSourceAlpha{true};

        Plugins::XRSession& GetSession() const;

        Napi::Value GetTextureWidth(const Napi::CallbackInfo& info);
        Napi::Value GetTextureHeight(const Napi::CallbackInfo& info);
        Napi::Value GetTextureArrayLength(const Napi::CallbackInfo& info);
        Napi::Value GetIgnoreDepthValues(const Napi::CallbackInfo& info);
        Napi::Value GetBlendTextureSourceAlpha(const Napi::CallbackInfo& info);
        void SetBlendTextureSourceAlpha(const Napi::CallbackInfo&, const Napi::Value& value);
        Napi::Value GetLayout(const Napi::CallbackInfo& info);
        Napi::Value GetFixedFoveation(const Napi::CallbackInfo& info);
        void SetFixedFoveation(const Napi::CallbackInfo& info, const Napi::Value& value);
        Napi::Value GetDeltaPose(const Napi::CallbackInfo& info);
        void SetDeltaPose(const Napi::CallbackInfo& info, const Napi::Value& value);
    };
}
