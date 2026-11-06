/*
 * H.264 chroma motion compensation with paired singles for the Wii's CPU.
 * Multiplying in the FPU takes one cycle a pair, against several per pixel with
 * mullw. Bit-exact with the C versions: the sums fit a float exactly, and the
 * store's truncation after dividing by 64 or 128 is the same as the shift.
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
coef_tab:
    .float 0.0
    .float 1.0
    .float 2.0
    .float 3.0
    .float 4.0
    .float 5.0
    .float 6.0
    .float 7.0
    .float 8.0
    .float 9.0
    .float 10.0
    .float 11.0
    .float 12.0
    .float 13.0
    .float 14.0
    .float 15.0
    .float 16.0
    .float 17.0
    .float 18.0
    .float 19.0
    .float 20.0
    .float 21.0
    .float 22.0
    .float 23.0
    .float 24.0
    .float 25.0
    .float 26.0
    .float 27.0
    .float 28.0
    .float 29.0
    .float 30.0
    .float 31.0
    .float 32.0
    .float 33.0
    .float 34.0
    .float 35.0
    .float 36.0
    .float 37.0
    .float 38.0
    .float 39.0
    .float 40.0
    .float 41.0
    .float 42.0
    .float 43.0
    .float 44.0
    .float 45.0
    .float 46.0
    .float 47.0
    .float 48.0
    .float 49.0
    .float 50.0
    .float 51.0
    .float 52.0
    .float 53.0
    .float 54.0
    .float 55.0
    .float 56.0
    .float 57.0
    .float 58.0
    .float 59.0
    .float 60.0
    .float 61.0
    .float 62.0
    .float 63.0
    .float 64.0
f32_:   .float 32.0
f96_:   .float 96.0
f64_:   .float 64.0

    .text
// r3 dst, r4 src, r5 stride, r6 h, r7 A, r8 B, r9 C, r10 D (all 0..64)
    .macro setup avg
    lis     r11, coef_tab@ha
    addi    r11, r11, coef_tab@l
    slwi    r7, r7, 2
    slwi    r8, r8, 2
    slwi    r9, r9, 2
    slwi    r10, r10, 2
    lfsx    f1, r11, r7
    lfsx    f2, r11, r8
    lfsx    f3, r11, r9
    lfsx    f4, r11, r10
    .if \avg
    lis     r12, 0x0004
    ori     r12, r12, 0x3904   // u8 in, out u8 divided by 128
    mtspr   SPR_GQR6, r12
    lis     r12, f96_@ha
    lfs     f5, f96_@l(r12)
    lis     r12, f64_@ha
    lfs     f13, f64_@l(r12)
    .else
    lis     r12, 0x0004
    ori     r12, r12, 0x3A04   // u8 in, out u8 divided by 64
    mtspr   SPR_GQR6, r12
    lis     r12, f32_@ha
    lfs     f5, f32_@l(r12)
    .endif
    add     r12, r4, r5
    mtctr   r6
    .endm

// two neighbouring pixels at offset \o
    .macro pair2d o, avg
    psq_l   f6, \o(r4), 0, 6
    psq_l   f7, \o+1(r4), 0, 6
    psq_l   f8, \o(r12), 0, 6
    psq_l   f9, \o+1(r12), 0, 6
    ps_madd f6, f6, f1, f5
    ps_madd f6, f7, f2, f6
    ps_madd f6, f8, f3, f6
    ps_madd f6, f9, f4, f6
    .if \avg
    psq_l   f7, \o(r3), 0, 6
    ps_madd f6, f7, f13, f6
    .endif
    psq_st  f6, \o(r3), 0, 6
    .endm

    .macro chroma2d name, pairs, avg
    .globl \name
    .balign 32
\name:
    setup \avg
1:
    pair2d 0, \avg
    pair2d 2, \avg
    .if \pairs == 4
    pair2d 4, \avg
    pair2d 6, \avg
    .endif
    add     r3, r3, r5
    mr      r4, r12
    add     r12, r12, r5
    bdnz    1b
    blr
    .endm

    chroma2d ff_ps_put4_2d, 2, 0
    chroma2d ff_ps_avg8_2d, 4, 1
    chroma2d ff_ps_avg4_2d, 2, 1

// put8 runs two pairs interleaved, which keeps the FPU's pipeline busier and
// is faster on real memory (f0, f10, f11, f12 are the second pair)
    .macro pair2i o
    psq_l   f6, \o(r4), 0, 6
    psq_l   f0, \o+2(r4), 0, 6
    psq_l   f7, \o+1(r4), 0, 6
    ps_madd f6, f6, f1, f5
    psq_l   f10, \o+3(r4), 0, 6
    ps_madd f0, f0, f1, f5
    psq_l   f8, \o(r12), 0, 6
    ps_madd f6, f7, f2, f6
    psq_l   f11, \o+2(r12), 0, 6
    ps_madd f0, f10, f2, f0
    psq_l   f9, \o+1(r12), 0, 6
    ps_madd f6, f8, f3, f6
    psq_l   f12, \o+3(r12), 0, 6
    ps_madd f0, f11, f3, f0
    ps_madd f6, f9, f4, f6
    ps_madd f0, f12, f4, f0
    psq_st  f6, \o(r3), 0, 6
    psq_st  f0, \o+2(r3), 0, 6
    .endm

    .globl ff_ps_put8_2d
    .balign 32
ff_ps_put8_2d:
    setup 0
1:
    pair2i 0
    pair2i 4
    add     r3, r3, r5
    mr      r4, r12
    add     r12, r12, r5
    bdnz    1b
    blr

    .macro pair1d o, avg
    psq_l   f6, \o(r4), 0, 6
    psq_l   f7, \o(r12), 0, 6
    ps_madd f6, f6, f3, f5
    ps_madd f6, f7, f4, f6
    .if \avg
    psq_l   f7, \o(r3), 0, 6
    ps_madd f6, f7, f13, f6
    .endif
    psq_st  f6, \o(r3), 0, 6
    .endm

// 1D: r9 A, r10 E, r8 step (bytes to the second tap); r7 unused here
    .macro chroma1d name, pairs, avg
    .globl \name
    .balign 32
\name:
    mr      r0, r8
    li      r8, 0
    li      r7, 0
    setup \avg
    add     r12, r4, r0
1:
    pair1d 0, \avg
    pair1d 2, \avg
    .if \pairs == 4
    pair1d 4, \avg
    pair1d 6, \avg
    .endif
    add     r3, r3, r5
    add     r4, r4, r5
    add     r12, r12, r5
    bdnz    1b
    blr
    .endm

    chroma1d ff_ps_put8_1d, 4, 0
    chroma1d ff_ps_put4_1d, 2, 0
    chroma1d ff_ps_avg8_1d, 4, 1
    chroma1d ff_ps_avg4_1d, 2, 1
