#pragma once
#include <Babylon/ShaderCache.h>
#include <bgfx/bgfx.h>

#include <iostream>
#include <fstream>

namespace Babylon
{
    class ShaderCacheImpl
    {
    public:
        ShaderCacheImpl() = default;
        ~ShaderCacheImpl() = default;
        const ShaderCompiler::BgfxShaderInfo* GetShader(std::string_view vertexSource, std::string_view fragmentSource, bgfx::RendererType::Enum rendererType);
        void AddShader(std::string_view vertexSource, std::string_view fragmentSource, bgfx::RendererType::Enum rendererType, ShaderCompiler::BgfxShaderInfo shaderInfo);

        uint32_t Serialize(std::ofstream& stream);
        uint32_t Deserialize(std::ifstream& stream);

        static ShaderCacheImpl* GetImpl();

    private:
        struct ShaderHash
        {
            size_t vertexHash;
            size_t fragmentHash;
            uint16_t rendererType;
            bool operator < (const ShaderHash& other) const {
                if (vertexHash < other.vertexHash || (vertexHash == other.vertexHash && fragmentHash < other.fragmentHash))
                    return true;
                if (vertexHash == other.vertexHash && fragmentHash == other.fragmentHash && rendererType < other.rendererType)
                    return true;
                return false;
            }
        };
        std::map<ShaderHash, ShaderCompiler::BgfxShaderInfo> Cache{};
        static inline std::unique_ptr<ShaderCacheImpl> Instance{};
        friend void ShaderCache::Enabled(bool enabled);
    };
}
