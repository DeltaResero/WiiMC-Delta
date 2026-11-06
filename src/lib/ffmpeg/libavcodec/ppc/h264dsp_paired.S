/*
 * H.264 weighted prediction with paired singles for the Wii's CPU.
 * Bit-exact with the C versions: the sums fit a float exactly, the store's
 * truncation after the scale matches the shift, and it saturates as the clip does.
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

#define r0 0
#define r1 1
#define r2 2
#define r3 3
#define r4 4
#define r5 5
#define r6 6
#define r7 7
#define r8 8
#define r9 9
#define r10 10
#define r11 11
#define r12 12
#define f0 0
#define f1 1
#define f2 2
#define f3 3
#define f4 4
#define f5 5
#define f6 6
#define f7 7
#define f8 8
#define f9 9
#define f10 10
#define f11 11
#define f12 12
#define f13 13

    .equ SPR_GQR6, 918
    .text

// weight: r3 block, r4 stride, r5 height, r6 log2_denom, f1 weight, f2 offset
    .macro set_shift
    subfic  r6, r6, 64
    clrlwi  r6, r6, 26
    slwi    r6, r6, 8
    oris    r6, r6, 4
    ori     r6, r6, 4
    mtspr   SPR_GQR6, r6
    .endm

    .macro wpair o
    psq_l   f3, \o(r3), 0, 6
    psq_l   f4, \o+2(r3), 0, 6
    ps_madd f3, f3, f1, f2
    ps_madd f4, f4, f1, f2
    psq_st  f3, \o(r3), 0, 6
    psq_st  f4, \o+2(r3), 0, 6
    .endm

    .macro weight name, quads
    .globl \name
    .balign 32
\name:
    set_shift
    ps_merge00 f1, f1, f1
    ps_merge00 f2, f2, f2
    mtctr   r5
1:
    wpair 0
    .if \quads >= 2
    wpair 4
    .endif
    .if \quads >= 4
    wpair 8
    wpair 12
    .endif
    add     r3, r3, r4
    bdnz    1b
    blr
    .endm

    weight ff_ps_weight16, 4
    weight ff_ps_weight8, 2
    weight ff_ps_weight4, 1

// biweight: r3 dst, r4 src, r5 stride, r6 height, r7 log2_denom+1, f1 weightd, f2 weights, f3 offset
    .macro bpair o
    psq_l   f4, \o(r3), 0, 6
    psq_l   f5, \o(r4), 0, 6
    psq_l   f6, \o+2(r3), 0, 6
    ps_madd f4, f4, f1, f3
    psq_l   f7, \o+2(r4), 0, 6
    ps_madd f6, f6, f1, f3
    ps_madd f4, f5, f2, f4
    ps_madd f6, f7, f2, f6
    psq_st  f4, \o(r3), 0, 6
    psq_st  f6, \o+2(r3), 0, 6
    .endm

    .macro biweight name, quads
    .globl \name
    .balign 32
\name:
    mr      r0, r6
    mr      r6, r7
    set_shift
    ps_merge00 f1, f1, f1
    ps_merge00 f2, f2, f2
    ps_merge00 f3, f3, f3
    mtctr   r0
1:
    bpair 0
    .if \quads >= 2
    bpair 4
    .endif
    .if \quads >= 4
    bpair 8
    bpair 12
    .endif
    add     r3, r3, r5
    add     r4, r4, r5
    bdnz    1b
    blr
    .endm

    biweight ff_ps_biweight16, 4
    biweight ff_ps_biweight8, 2
    biweight ff_ps_biweight4, 1
