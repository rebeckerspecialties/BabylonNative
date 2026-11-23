#pragma once

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

namespace xr
{
    // Retaining only MTLTexture does not keep Core Video's backing image lease alive.
    // Capture this snapshot in the command-buffer completion handler instead.
    class CameraTextureSnapshot
    {
    public:
        CameraTextureSnapshot(CVMetalTextureRef luma = nullptr, CVMetalTextureRef chroma = nullptr)
            : m_luma{luma}, m_chroma{chroma}
        {
            if (m_luma) CFRetain(m_luma);
            if (m_chroma) CFRetain(m_chroma);
        }

        CameraTextureSnapshot(const CameraTextureSnapshot& other)
            : CameraTextureSnapshot{other.m_luma, other.m_chroma}
        {
        }

        CameraTextureSnapshot& operator=(const CameraTextureSnapshot&) = delete;

        ~CameraTextureSnapshot()
        {
            if (m_luma) CFRelease(m_luma);
            if (m_chroma) CFRelease(m_chroma);
        }

        id<MTLTexture> Luma() const { return m_luma ? CVMetalTextureGetTexture(m_luma) : nil; }
        id<MTLTexture> Chroma() const { return m_chroma ? CVMetalTextureGetTexture(m_chroma) : nil; }

    private:
        CVMetalTextureRef m_luma;
        CVMetalTextureRef m_chroma;
    };
}
