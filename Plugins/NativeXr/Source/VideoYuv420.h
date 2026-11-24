#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace Babylon::VideoYuv420
{
    struct Matrix
    {
        int16_t y, offset, rv, gu, gv, bu;
    };

    inline Matrix ColorMatrix(bool hd, bool full)
    {
        if (hd) return full ? Matrix{256, 0, 403, 48, 120, 475} : Matrix{298, 16, 459, 55, 136, 541};
        return full ? Matrix{256, 0, 359, 88, 183, 454} : Matrix{298, 16, 409, 100, 208, 516};
    }

    inline void Row(uint8_t* dst, const uint8_t* y, const uint8_t* u, const uint8_t* v,
        int left, int right, int yStep, int uStep, int vStep, const Matrix& m)
    {
        int x = left;
        const auto pixel = [&](int i) {
            const int luma = m.y * (y[std::size_t(i) * yStep] - m.offset);
            const int cb = u[std::size_t(i / 2) * uStep] - 128, cr = v[std::size_t(i / 2) * vStep] - 128;
            const auto channel = [](int n) { return static_cast<uint8_t>(std::clamp((n + 128) >> 8, 0, 255)); };
            *dst++ = channel(luma + m.rv * cr);
            *dst++ = channel(luma - m.gu * cb - m.gv * cr);
            *dst++ = channel(luma + m.bu * cb);
            *dst++ = 255;
        };
#if defined(__aarch64__)
        if (yStep == 1 && (uStep == 1 || uStep == 2) && (vStep == 1 || vStep == 2))
        {
            if ((x & 1) && x < right) pixel(x++);
            // Strided loads also read the byte after the eighth chroma sample.
            // Android's last plane row need not include that padding byte.
            for (; right - x > 16; x += 16)
            {
                const auto load = [](const uint8_t* p, int step) { return step == 1 ? vld1_u8(p) : vld2_u8(p).val[0]; };
                const auto center = [](uint8x8_t a, int16_t offset) {
                    return vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(a)), vdupq_n_s16(offset));
                };
                const auto cb = center(load(u + std::size_t(x / 2) * uStep, uStep), 128);
                const auto cr = center(load(v + std::size_t(x / 2) * vStep, vStep), 128);
                const auto convert = [&](int offset, int16x8_t cb8, int16x8_t cr8) {
                    const auto yy = center(vld1_u8(y + x + offset), m.offset);
                    const auto four = [&](int16x4_t l, int16x4_t b, int16x4_t r) {
                        const auto base = vmull_n_s16(l, m.y);
                        return uint16x4x3_t{{vqrshrun_n_s32(vmlal_n_s16(base, r, m.rv), 8),
                            vqrshrun_n_s32(vmlsl_n_s16(vmlsl_n_s16(base, b, m.gu), r, m.gv), 8),
                            vqrshrun_n_s32(vmlal_n_s16(base, b, m.bu), 8)}};
                    };
                    const auto lo = four(vget_low_s16(yy), vget_low_s16(cb8), vget_low_s16(cr8));
                    const auto hi = four(vget_high_s16(yy), vget_high_s16(cb8), vget_high_s16(cr8));
                    uint8x8x4_t rgba;
                    for (int c = 0; c < 3; ++c) rgba.val[c] = vqmovn_u16(vcombine_u16(lo.val[c], hi.val[c]));
                    rgba.val[3] = vdup_n_u8(255);
                    vst4_u8(dst, rgba);
                    dst += 32;
                };
                convert(0, vzip1q_s16(cb, cb), vzip1q_s16(cr, cr));
                convert(8, vzip2q_s16(cb, cb), vzip2q_s16(cr, cr));
            }
        }
#endif
        for (; x < right; ++x) pixel(x);
    }
}
