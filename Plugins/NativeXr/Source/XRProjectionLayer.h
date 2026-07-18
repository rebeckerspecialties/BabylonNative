#pragma once

#include <napi/napi.h>

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
        static Napi::Object New(Napi::Env env, const Napi::Object& session, const Napi::Object& init);

        explicit XRProjectionLayer(const Napi::CallbackInfo& info);

        bool BelongsTo(const Plugins::XRSession& session) const;
        const std::string& ColorFormat() const;
        const std::string& DepthStencilFormat() const;
        uint32_t TextureUsage() const;

    private:
        Napi::ObjectReference m_sessionReference{};
        std::string m_colorFormat{};
        std::string m_depthStencilFormat{};
        uint32_t m_textureUsage{};
        float m_scaleFactor{1.0f};
        bool m_ignoreDepthValues{};

        Plugins::XRSession& GetSession() const;

        Napi::Value GetTextureWidth(const Napi::CallbackInfo& info);
        Napi::Value GetTextureHeight(const Napi::CallbackInfo& info);
        Napi::Value GetTextureArrayLength(const Napi::CallbackInfo& info);
        Napi::Value GetIgnoreDepthValues(const Napi::CallbackInfo& info);
        Napi::Value GetLayout(const Napi::CallbackInfo& info);
    };
}
