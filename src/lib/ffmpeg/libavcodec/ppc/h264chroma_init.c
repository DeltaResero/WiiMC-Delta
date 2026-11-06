/*
 * Copyright (c) 2004 Romain Dolbeau <romain@dolbeau.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/h264chroma.h"

#if HAVE_ALTIVEC
#define PUT_OP_U8_ALTIVEC(d, s, dst) d = s
#define AVG_OP_U8_ALTIVEC(d, s, dst) d = vec_avg(dst, s)

#define OP_U8_ALTIVEC                          PUT_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         put_h264_chroma_mc8_altivec
#define PREFIX_h264_chroma_mc8_num             altivec_put_h264_chroma_mc8_num
#include "h264chroma_template.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_h264_chroma_mc8_altivec
#undef PREFIX_h264_chroma_mc8_num

#define OP_U8_ALTIVEC                          AVG_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         avg_h264_chroma_mc8_altivec
#define PREFIX_h264_chroma_mc8_num             altivec_avg_h264_chroma_mc8_num
#include "h264chroma_template.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_h264_chroma_mc8_altivec
#undef PREFIX_h264_chroma_mc8_num
#endif /* HAVE_ALTIVEC */

#ifdef GEKKO
#include <stddef.h>
#include <stdint.h>

typedef void paired_mc(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h,
                       int a, int b, int c, int d);
paired_mc ff_ps_put8_2d, ff_ps_put4_2d, ff_ps_avg8_2d, ff_ps_avg4_2d;
paired_mc ff_ps_put8_1d, ff_ps_put4_1d, ff_ps_avg8_1d, ff_ps_avg4_1d;

#define MC_FUNC(op, w)                                                                  \
static void op ## _h264_chroma_mc ## w ## _paired(uint8_t *dst, const uint8_t *src,     \
                                                  ptrdiff_t stride, int h, int x, int y)\
{                                                                                       \
    const int A = (8 - x) * (8 - y), B = x * (8 - y), C = (8 - x) * y, D = x * y;       \
    if (D)                                                                              \
        ff_ps_ ## op ## w ## _2d(dst, src, stride, h, A, B, C, D);                      \
    else if (B + C)                                                                     \
        ff_ps_ ## op ## w ## _1d(dst, src, stride, h, 0, C ? stride : 1, A, B + C);     \
    else                                                                                \
        op ## _copy ## w(dst, src, stride, h);                                          \
}

static inline void put_copy8(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h)
{
    do {
        AV_WN32(dst,     AV_RN32(src));
        AV_WN32(dst + 4, AV_RN32(src + 4));
        dst += stride;
        src += stride;
    } while (--h);
}

static inline void put_copy4(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h)
{
    do {
        AV_WN32(dst, AV_RN32(src));
        dst += stride;
        src += stride;
    } while (--h);
}

static inline void avg_copy(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int w, int h)
{
    do {
        for (int i = 0; i < w; i++)
            dst[i] = (dst[i] + src[i] + 1) >> 1;
        dst += stride;
        src += stride;
    } while (--h);
}

static inline void avg_copy8(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h)
{
    avg_copy(dst, src, stride, 8, h);
}

static inline void avg_copy4(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h)
{
    avg_copy(dst, src, stride, 4, h);
}

MC_FUNC(put, 8)
MC_FUNC(put, 4)
MC_FUNC(avg, 8)
MC_FUNC(avg, 4)
#endif /* GEKKO */

av_cold void ff_h264chroma_init_ppc(H264ChromaContext *c, int bit_depth)
{
#ifdef GEKKO
    if (bit_depth <= 8) {
        c->put_h264_chroma_pixels_tab[0] = put_h264_chroma_mc8_paired;
        c->put_h264_chroma_pixels_tab[1] = put_h264_chroma_mc4_paired;
        c->avg_h264_chroma_pixels_tab[0] = avg_h264_chroma_mc8_paired;
        c->avg_h264_chroma_pixels_tab[1] = avg_h264_chroma_mc4_paired;
    }
#endif
#if HAVE_ALTIVEC
    const int high_bit_depth = bit_depth > 8;

    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    if (!high_bit_depth) {
        c->put_h264_chroma_pixels_tab[0] = put_h264_chroma_mc8_altivec;
        c->avg_h264_chroma_pixels_tab[0] = avg_h264_chroma_mc8_altivec;
    }
#endif /* HAVE_ALTIVEC */
}
