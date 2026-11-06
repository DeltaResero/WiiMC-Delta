/*
 * H.264 luma 6-tap filters with paired singles for the Wii's CPU.
 * Loaded pairs stay in registers from one pixel or row to the next. Bit-exact with
 * the C versions: the sums fit a float exactly, the store's truncation after the
 * scale matches the shift, and it saturates as the clip does.
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

    .section .rodata
    .balign 32
f16_:   .float 16.0
f512_:  .float 512.0
f20_:   .float 20.0
f5_:    .float 5.0
f0_:    .float 0.0

    .text

// ---------- H.264 luma 6-tap, keeping loaded pairs in registers ----------
    .macro tapconst round
    lis     r12, f20_@ha
    lfs     f1, f20_@l(r12)
    lis     r12, f5_@ha
    lfs     f2, f5_@l(r12)
    lis     r12, \round@ha
    lfs     f3, \round@l(r12)
    .endm

// f10 = 20*(c+d) - 5*(b+e) + (a+f) + round
    .macro tap a, b, c, d, e, f
    ps_add  f10, \c, \d
    ps_add  f11, \b, \e
    ps_add  f12, \a, \f
    ps_madd f10, f10, f1, f3
    ps_add  f10, f10, f12
    ps_nmsub f10, f11, f2, f10
    .endm

    .macro hrow step
    psq_l   f4, -2(r4), 0, 6
    psq_l   f5, -1(r4), 0, 6
    psq_l   f6, 0(r4), 0, 6
    psq_l   f7, 1(r4), 0, 6
    psq_l   f8, 2(r4), 0, 6
    psq_l   f9, 3(r4), 0, 6
    tap     f4, f5, f6, f7, f8, f9
    psq_st  f10, 0(r3), 0, 6
    psq_l   f4, 4(r4), 0, 6
    psq_l   f5, 5(r4), 0, 6
    tap     f6, f7, f8, f9, f4, f5
    psq_st  f10, \step(r3), 0, 6
    psq_l   f6, 6(r4), 0, 6
    psq_l   f7, 7(r4), 0, 6
    tap     f8, f9, f4, f5, f6, f7
    psq_st  f10, \step*2(r3), 0, 6
    psq_l   f8, 8(r4), 0, 6
    psq_l   f9, 9(r4), 0, 6
    tap     f4, f5, f6, f7, f8, f9
    psq_st  f10, \step*3(r3), 0, 6
    .endm

// r3 dst, r4 src, r5 dstStride, r6 srcStride
    .globl ff_ps_qpel8_h
    .balign 32
ff_ps_qpel8_h:
    tapconst f16_
    lis     r12, 0x0004
    ori     r12, r12, 0x3B04
    mtspr   SPR_GQR6, r12
    li      r12, 8
    mtctr   r12
1:
    hrow 2
    add     r3, r3, r5
    add     r4, r4, r6
    bdnz    1b
    blr

// eight rows down one column pair: r9 source, r10 destination
    .macro vcolumn
    psq_l   f4, 0(r9), 0, 6
    psq_lux f5, r9, r6, 0, 6
    psq_lux f6, r9, r6, 0, 6
    psq_lux f7, r9, r6, 0, 6
    psq_lux f8, r9, r6, 0, 6
    psq_lux f9, r9, r6, 0, 6
    tap     f4, f5, f6, f7, f8, f9
    psq_st  f10, 0(r10), 0, 6
    psq_lux f4, r9, r6, 0, 6
    tap     f5, f6, f7, f8, f9, f4
    psq_stux f10, r10, r5, 0, 6
    psq_lux f5, r9, r6, 0, 6
    tap     f6, f7, f8, f9, f4, f5
    psq_stux f10, r10, r5, 0, 6
    psq_lux f6, r9, r6, 0, 6
    tap     f7, f8, f9, f4, f5, f6
    psq_stux f10, r10, r5, 0, 6
    psq_lux f7, r9, r6, 0, 6
    tap     f8, f9, f4, f5, f6, f7
    psq_stux f10, r10, r5, 0, 6
    psq_lux f8, r9, r6, 0, 6
    tap     f9, f4, f5, f6, f7, f8
    psq_stux f10, r10, r5, 0, 6
    psq_lux f9, r9, r6, 0, 6
    tap     f4, f5, f6, f7, f8, f9
    psq_stux f10, r10, r5, 0, 6
    psq_lux f4, r9, r6, 0, 6
    tap     f5, f6, f7, f8, f9, f4
    psq_stux f10, r10, r5, 0, 6
    .endm

// r3 dst, r4 src, r5 dstStride, r6 srcStride
    .globl ff_ps_qpel8_v
    .balign 32
ff_ps_qpel8_v:
    tapconst f16_
    lis     r12, 0x0004
    ori     r12, r12, 0x3B04
    mtspr   SPR_GQR6, r12
    slwi    r11, r6, 1
    subf    r4, r11, r4
    li      r12, 4
    mtctr   r12
1:
    mr      r9, r4
    mr      r10, r3
    vcolumn
    addi    r4, r4, 2
    addi    r3, r3, 2
    bdnz    1b
    blr

// r3 dst, r4 int16 tmp, r5 src, r6 dstStride, r7 tmpStride (elements), r8 srcStride
    .globl ff_ps_qpel8_hv
    .balign 32
ff_ps_qpel8_hv:
    mr      r0, r3
    mr      r3, r4
    mr      r4, r5
    mr      r5, r6
    slwi    r7, r7, 1           // tmp stride in bytes
    tapconst f0_
    lis     r12, 0x0004         // u8 in, s16 out
    ori     r12, r12, 0x0007
    mtspr   SPR_GQR6, r12
    slwi    r11, r8, 1
    subf    r4, r11, r4
    mr      r11, r3
    li      r12, 13
    mtctr   r12
1:
    hrow 4
    add     r3, r3, r7
    add     r4, r4, r8
    bdnz    1b
    lis     r12, f512_@ha
    lfs     f3, f512_@l(r12)
    lis     r12, 0x0007         // s16 in, u8 out divided by 1024
    ori     r12, r12, 0x3604
    mtspr   SPR_GQR6, r12
    mr      r6, r7
    mr      r3, r0
    mr      r4, r11
    li      r12, 4
    mtctr   r12
2:
    mr      r9, r4
    mr      r10, r3
    vcolumn
    addi    r4, r4, 4
    addi    r3, r3, 2
    bdnz    2b
    blr
