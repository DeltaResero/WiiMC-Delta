/*
 * Lets the compiler keep CABAC's state in registers: writing the context byte
 * through *state would otherwise make it reload low and range from memory.
 * Gives the same bits as the generic get_cabac_inline().
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

#ifndef AVCODEC_PPC_CABAC_H
#define AVCODEC_PPC_CABAC_H

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/intmath.h"
#include "libavcodec/cabac.h"

#define get_cabac_inline get_cabac_inline
static av_always_inline int get_cabac_inline(CABACContext *c, uint8_t *const state)
{
    const uint8_t *const tables = ff_h264_cabac_tables;
    int s     = *state;
    int low   = c->low;
    int range = c->range;
    int lps   = tables[H264_LPS_RANGE_OFFSET + 2 * (range & 0xC0) + s];
    int scaled, mask, shift;

    range -= lps;
    scaled = range << (CABAC_BITS + 1);
    mask   = (scaled - low) >> 31;
    low   -= scaled & mask;
    range += (lps - range) & mask;

    s     ^= mask;
    *state = tables[H264_MLPS_STATE_OFFSET + 128 + s];

    shift  = tables[H264_NORM_SHIFT_OFFSET + range];
    range <<= shift;
    low   <<= shift;

    if (__builtin_expect(!(low & CABAC_MASK), 0)) {
        int i      = ff_ctz(low) - CABAC_BITS;
        unsigned x = -CABAC_MASK;
#if CABAC_BITS == 16
        x += (c->bytestream[0] << 9) + (c->bytestream[1] << 1);
#else
        x += c->bytestream[0] << 1;
#endif
        low += x << i;
#if !UNCHECKED_BITSTREAM_READER
        if (c->bytestream < c->bytestream_end)
#endif
            c->bytestream += CABAC_BITS / 8;
    }
    c->low   = low;
    c->range = range;
    return s & 1;
}

#endif /* AVCODEC_PPC_CABAC_H */
