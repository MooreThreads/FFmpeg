/*
 * Simple IDCT
 *
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * simpleidct in C.
 */

#include "libavutil/intreadwrite.h"
#include "mathops.h"
#include "simple_idct.h"

#define IN_IDCT_DEPTH 16

#define BIT_DEPTH 8
#include "simple_idct_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "simple_idct_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 12
#include "simple_idct_template.c"
#undef BIT_DEPTH
#undef IN_IDCT_DEPTH

#define IN_IDCT_DEPTH 32
#define BIT_DEPTH 10
#include "simple_idct_template.c"
#undef BIT_DEPTH
#undef IN_IDCT_DEPTH

/* 2x4x8 idct */

#define CN_SHIFT 12
#define C_FIX(x) ((int)((x) * (1 << CN_SHIFT) + 0.5))
#define C1 C_FIX(0.6532814824)
#define C2 C_FIX(0.2705980501)

/* row idct is multiple by 16 * sqrt(2.0), col idct4 is normalized,
   and the butterfly must be multiplied by 0.5 * sqrt(2.0) */
#define C_SHIFT (4+1+12)

static inline void idct4col_put(uint8_t *dest, ptrdiff_t line_size, const int16_t *col)
{
    int c0, c1, c2, c3, a0, a1, a2, a3;

    a0 = col[8*0];
    a1 = col[8*2];
    a2 = col[8*4];
    a3 = col[8*6];
    c0 = ((a0 + a2) * (1 << CN_SHIFT - 1)) + (1 << (C_SHIFT - 1));
    c2 = ((a0 - a2) * (1 << CN_SHIFT - 1)) + (1 << (C_SHIFT - 1));
    c1 = a1 * C1 + a3 * C2;
    c3 = a1 * C2 - a3 * C1;
    dest[0] = av_clip_uint8((c0 + c1) >> C_SHIFT);
    dest += line_size;
    dest[0] = av_clip_uint8((c2 + c3) >> C_SHIFT);
    dest += line_size;
    dest[0] = av_clip_uint8((c2 - c3) >> C_SHIFT);
    dest += line_size;
    dest[0] = av_clip_uint8((c0 - c1) >> C_SHIFT);
}

#define BF(k) \
{\
    int a0, a1;\
    a0 = ptr[k];\
    a1 = ptr[8 + k];\
    ptr[k] = a0 + a1;\
    ptr[8 + k] = a0 - a1;\
}

/* only used by DV codec. The input must be interlaced. 128 is added
   to the pixels before clamping to avoid systematic error
   (1024*sqrt(2)) offset would be needed otherwise. */
/* XXX: I think a 1.0/sqrt(2) normalization should be needed to
   compensate the extra butterfly stage - I don't have the full DV
   specification */
void ff_simple_idct248_put(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;
    int16_t *ptr;

    /* butterfly */
    ptr = block;
    for(i=0;i<4;i++) {
        BF(0);
        BF(1);
        BF(2);
        BF(3);
        BF(4);
        BF(5);
        BF(6);
        BF(7);
        ptr += 2 * 8;
    }

    /* IDCT8 on each line */
    for(i=0; i<8; i++) {
        idctRowCondDC_int16_8bit(block + i*8, 0);
    }

    /* IDCT4 and store */
    for(i=0;i<8;i++) {
        idct4col_put(dest + i, 2 * line_size, block + i);
        idct4col_put(dest + line_size + i, 2 * line_size, block + 8 + i);
    }
}

/* 8x4 & 4x8 WMV2 IDCT */
#undef CN_SHIFT
#undef C_SHIFT
#undef C_FIX
#undef C1
#undef C2
#define CN_SHIFT 12
#define C_FIX(x) ((int)((x) * M_SQRT2 * (1 << CN_SHIFT) + 0.5))
#define C1 C_FIX(0.6532814824)
#define C2 C_FIX(0.2705980501)
#define C3 C_FIX(0.5)
#define C_SHIFT (4+1+12)
static inline void idct4col_add(uint8_t *dest, ptrdiff_t line_size, const int16_t *col)
{
    int c0, c1, c2, c3, a0, a1, a2, a3;

    a0 = col[8*0];
    a1 = col[8*1];
    a2 = col[8*2];
    a3 = col[8*3];
    c0 = (a0 + a2)*C3 + (1 << (C_SHIFT - 1));
    c2 = (a0 - a2)*C3 + (1 << (C_SHIFT - 1));
    c1 = a1 * C1 + a3 * C2;
    c3 = a1 * C2 - a3 * C1;
    dest[0] = av_clip_uint8(dest[0] + ((c0 + c1) >> C_SHIFT));
    dest += line_size;
    dest[0] = av_clip_uint8(dest[0] + ((c2 + c3) >> C_SHIFT));
    dest += line_size;
    dest[0] = av_clip_uint8(dest[0] + ((c2 - c3) >> C_SHIFT));
    dest += line_size;
    dest[0] = av_clip_uint8(dest[0] + ((c0 - c1) >> C_SHIFT));
}

#define RN_SHIFT 15
#define R_FIX(x) ((int)((x) * M_SQRT2 * (1 << RN_SHIFT) + 0.5))
#define R1 R_FIX(0.6532814824)
#define R2 R_FIX(0.2705980501)
#define R3 R_FIX(0.5)
#define R_SHIFT 11
static inline void idct4row(int16_t *row)
{
    unsigned c0, c1, c2, c3;
    int a0, a1, a2, a3;

    a0 = row[0];
    a1 = row[1];
    a2 = row[2];
    a3 = row[3];
    c0 = (a0 + a2)*R3 + (1 << (R_SHIFT - 1));
    c2 = (a0 - a2)*R3 + (1 << (R_SHIFT - 1));
    c1 = a1 * R1 + a3 * R2;
    c3 = a1 * R2 - a3 * R1;
    row[0]= (c0 + c1) >> R_SHIFT;
    row[1]= (c2 + c3) >> R_SHIFT;
    row[2]= (c2 - c3) >> R_SHIFT;
    row[3]= (c0 - c1) >> R_SHIFT;
}

void ff_simple_idct84_add(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;

    /* IDCT8 on each line */
    for(i=0; i<4; i++) {
        idctRowCondDC_int16_8bit(block + i*8, 0);
    }

    /* IDCT4 and store */
    for(i=0;i<8;i++) {
        idct4col_add(dest + i, line_size, block + i);
    }
}

void ff_simple_idct48_add(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;

    /* IDCT4 on each line */
    for(i=0; i<8; i++) {
        idct4row(block + i*8);
    }

    /* IDCT8 and store */
    for(i=0; i<4; i++){
        idctSparseColAdd_int16_8bit(dest + i, line_size, block + i);
    }
}

void ff_simple_idct44_add(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;

    /* IDCT4 on each line */
    for(i=0; i<4; i++) {
        idct4row(block + i*8);
    }

    /* IDCT4 and store */
    for(i=0; i<4; i++){
        idct4col_add(dest + i, line_size, block + i);
    }
}
#define BIT18_PREC
#define MAX(a, b)       (((a) > (b)) ? (a) : (b))
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#define CLIP255(a)      (MIN(255, MAX(0, (a))))
#define CLIP16(a)  ( ((a) < -32768) ? -32768 : ((a) > 32767 ? 32767 : (a)) )

static void idct1d(int out[], int in[])
{
    int i, j, tmp2, tmp3;
    int X[20], acc[8];
    int Ax[8], Bx[8], Cx[8], Dx[8], Ex[8], Fx[8], Gx[8];

    for (i = 0; i < 8; i++) {
        tmp2 = (in[i]<<2) + in[i];
        tmp3 = (in[i]<<3) + in[i];
        X[0] =  in[i]<<5;
        for (j = 1; j < 19; j++)
            X[j] = X[j - 1]>>1;
        if ((i&1) == 0) {
#ifdef BIT18_PREC
            Ax[i] = (tmp2<<1) + (tmp2>>2) + X[9] + X[15];
            Cx[i] = X[1] - (tmp2>>2) + X[10] + X[15] - X[18];
            Fx[i] = X[3] + X[4] + X[8] - X[14];
#else
            Ax[i] = (tmp2<<1) + (tmp2>>2) + X[9];
            Bx[i] = X[1] - (tmp2>>2) + X[10];
            Cx[i] = X[3] + X[4] + X[8] - X[14];
#endif
        }
        else {
#ifdef BIT18_PREC
            Bx[i] = X[1] - (tmp2>>4) + (tmp2>>10);
            Dx[i] = X[2] + tmp2 + (tmp2>>4) - (tmp3>>10) - X[17];
            Ex[i] = tmp3 - X[8] + X[11] - X[15] - X[16];
            Gx[i] = X[4] + (tmp3>>3) - X[13] + X[17];
#else
            Dx[i] = X[1] - (tmp2>>4) + X[13];
            Ex[i] = X[2] + tmp2 + (tmp2>>4) - X[12];
            Fx[i] = tmp3 - X[8] + X[11];
            Gx[i] = X[4] + (tmp3>>3) - X[13];
#endif
        }
    }

#ifdef BIT18_PREC
    acc[0] = Ax[0] + Cx[2] + Ax[4] + Fx[6];
    acc[1] = Ax[0] + Fx[2] - Ax[4] - Cx[6];
    acc[2] = Ax[0] - Fx[2] - Ax[4] + Cx[6];
    acc[3] = Ax[0] - Cx[2] + Ax[4] - Fx[6];

    acc[4] = Bx[1] + Dx[3] + Ex[5] + Gx[7];
    acc[5] = Dx[1] - Gx[3] - Bx[5] - Ex[7];
    acc[6] = Ex[1] - Bx[3] + Gx[5] + Dx[7];
    acc[7] = Gx[1] - Ex[3] + Dx[5] - Bx[7];
#else
    acc[0] = Ax[0] + Bx[2] + Ax[4] + Cx[6];
    acc[1] = Ax[0] + Cx[2] - Ax[4] - Bx[6];
    acc[2] = Ax[0] - Cx[2] - Ax[4] + Bx[6];
    acc[3] = Ax[0] - Bx[2] + Ax[4] - Cx[6];

    acc[4] = Dx[1] + Ex[3] + Fx[5] + Gx[7];
    acc[5] = Ex[1] - Gx[3] - Dx[5] - Fx[7];
    acc[6] = Fx[1] - Dx[3] + Gx[5] + Ex[7];
    acc[7] = Gx[1] - Fx[3] + Ex[5] - Dx[7];
#endif


    out[0] = acc[0] + acc[4];
    out[1] = acc[1] + acc[5];
    out[2] = acc[2] + acc[6];
    out[3] = acc[3] + acc[7];
    out[4] = acc[3] - acc[7];
    out[5] = acc[2] - acc[6];
    out[6] = acc[1] - acc[5];
    out[7] = acc[0] - acc[4];
}

static void IdctBlk(int16_t block[])
{
    int  xx[64];
    int  yy[64];
    int  temp;
    int  i;
    // IDCT ROW x 8
    for (i=0; i<64; i++)
#ifdef BIT18_PREC
        xx[i] = block[i/8 + (i&7)*8] << 6;
#else
        xx[i] = block[i/8 + (i&7)*8] << 4;
#endif

    for (i=0; i<64; i+=8)
        idct1d(yy + i, xx + i);

    for (i=0; i<64; i++) {
        temp = yy[i];
        if (temp > 0 || (temp&0xf) != 0)
            temp = (temp >> 5) + ((temp >> 4) & 1);
        else
            temp = (temp >> 5);

#ifdef BIT18_PREC
        if (temp < -131072)
            yy[i] = -131072;
        else if (temp > 131071)  // signed 2^18
            yy[i] = 131071;
#else
        if (temp < -32768)
            yy[i] = -32768;
        else if (temp > 32767)  // signed 2^16
            yy[i] = 32767;
#endif
        else
            yy[i] = temp;
    }

    // IDCT COL x 8
    for (i=0; i<64; i++)
        xx[i] = yy[i/8 + (i&7)*8];

    for (i=0; i<64; i+=8)
        idct1d(yy + i, xx + i);

    for (i=0; i<64; i++) {
        temp = yy[i];
#ifdef BIT18_PREC
        if (temp > 0 || (temp& 0x3ff) != 0)
            temp = (temp >> 11) + ((temp >> 10) & 1);
        else
            temp = (temp >> 11);
#else
        if (temp > 0 || (temp& 0xff) != 0)
            temp = (temp >> 9) + ((temp >> 8) & 1);
        else
            temp = (temp >> 9);
#endif

        if (temp < -256)
            block[i] = -256;
        else if (temp > 255)
            block[i] = 255;
        else
            block[i] = temp;
    }
}

void cnm_mp2_idct_put(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int  i;

    IdctBlk(block);
    for (i = 0; i < 8; i++) {
        dest[0] = CLIP255(block[0]+128);
        dest[1] = CLIP255(block[1]+128);
        dest[2] = CLIP255(block[2]+128);
        dest[3] = CLIP255(block[3]+128);
        dest[4] = CLIP255(block[4]+128);
        dest[5] = CLIP255(block[5]+128);
        dest[6] = CLIP255(block[6]+128);
        dest[7] = CLIP255(block[7]+128);

        dest += line_size;
        block  += 8;
    }
}

void cnm_mp4_idct_put(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int  i;

    IdctBlk(block);
    for (i = 0; i < 8; i++) {
        dest[0] = CLIP255(block[0]);
        dest[1] = CLIP255(block[1]);
        dest[2] = CLIP255(block[2]);
        dest[3] = CLIP255(block[3]);
        dest[4] = CLIP255(block[4]);
        dest[5] = CLIP255(block[5]);
        dest[6] = CLIP255(block[6]);
        dest[7] = CLIP255(block[7]);

        dest += line_size;
        block  += 8;
    }
}

static void InvDivxDct1d (short *out, short *in, int seq)
{
    int i, j;
    int temp[8];
    int acc[8];
    int pat1x0, pat2x0, pat3x0, X0[16], pat1x1, pat2x1, pat3x1, X1[16];
    int x0[4], x1[4];

    int round;
    int c1d[4], c2d[4], c3d[4], c4d[4], c5d[4], c6d[4], c7d[4];

    x0[0] = in[4];
    x0[1] = in[2];
    x0[2] = in[6];
    x0[3] = in[0];
    x1[0] = in[3];
    x1[1] = in[5];
    x1[2] = in[1];
    x1[3] = in[7];

    //----------------------------------------
    // Perform IDCT now.
    //----------------------------------------
    for (i = 0; i < 4; i++) {
        pat1x0 = (x0[i]<<1) + x0[i];
        pat2x0 = (x0[i]<<2) + x0[i];
        pat3x0 = (x0[i]<<3) + x0[i];
        pat1x1 = (x1[i]<<1) + x1[i];
        pat2x1 = (x1[i]<<2) + x1[i];
        pat3x1 = (x1[i]<<3) + x1[i];

        X0[0] = (x0[i]);
        X1[0] = (x1[i]);
        for (j = 1; j < 16; j++) {
            X0[j] = (X0[j-1]<<1);
            X1[j] = (X1[j-1]<<1);
        }
        if (seq == 0 || seq == 4) {
            round = (seq == 0) ? 0x10000 : 0x0000;

                c6d[i] = X0[14] + (pat2x0<<10) - (pat1x0<<5)-X0[0];
                c4d[i] = X0[14];
                c2d[i] = X0[13] + (pat2x0<<7)  + X0[5] + pat1x0;

                c7d[i] = X1[14] + (pat1x1<<11) + (pat1x1<<6) + pat2x1;
                c5d[i] = X1[14] + (pat2x1<<9) + (pat2x1<<6) + X1[1];
                c3d[i] = (pat1x1<<12) + (pat3x1<<6) + pat3x1;
                c1d[i] = X1[12] + (pat1x1<<7) + (pat2x1<<3);

        }
        else if (seq == 1 || seq == 7) {
            round = (seq == 1) ? 0x0e0d : 0x0200;

                c6d[i] = X0[15] - (pat1x0<<10) - X0[2];
                c4d[i] = X0[14] + (pat1x0<<11) + (pat1x0<<6) + pat2x0;
                c2d[i] = (pat1x0<<12) + X0[3] + pat1x0;

                c7d[i] = X1[15] - (pat2x1<<8) + X1[5] + X1[0];
                c5d[i] = (pat1x1<<13) + X1[11] + (pat1x1<<5) + X1[1];
                c3d[i] = X1[14] + (pat1x1<<9) - X1[6] - X1[0];
                c1d[i] = (pat1x1<<11) + X1[7] - X1[1];

        }
        else if (seq == 2 || seq == 6) {
            round = (seq == 2) ? 0x08d4 : 0x0200;

                c6d[i] = (pat1x0<<13) + (pat1x0<<10) + (pat2x0<<6) + X0[0];
                c4d[i] = X0[14] + (pat2x0<<10) - (pat1x0<<5) - X0[0];
                c2d[i] = X0[13] + (pat1x0<<10) + (pat2x0<<6) + X0[0];

                c7d[i] = X1[15] - (pat1x1<<10) - X1[2];
                c5d[i] = (pat1x1<<13) + (pat3x1<<6) + (pat2x1<<2);
                c3d[i] = X1[14] + (pat1x1<<7) + (pat1x1<<4) + pat1x1;
                c1d[i] = (pat1x1<<11) - X1[8] + (pat3x1<<1);

        }
        else if (seq == 3 || seq == 5) {
            round = (seq == 3) ? 0x04b3 : 0x0078;

                c6d[i] = (pat1x0<<13) + (pat3x0<<6) + (pat2x0<<2);
                c4d[i] = X0[14] + (pat2x0<<9) + (pat2x0<<6) + X0[1];
                c2d[i] = (pat2x0<<11) + (pat2x0<<5) + X0[4] + (pat2x0<<1);

                c7d[i] = (pat1x1<<13) + X1[11] + (pat1x1<<5) + X1[1];
                c5d[i] = X1[14] + (pat1x1<<11) + X1[7] - X1[1];
                c3d[i] = (pat2x1<<11) + (pat3x1<<9) + (pat3x1<<5) + X1[0];
                c1d[i] = (pat2x1<<10) + (pat1x1<<6) + pat1x1;

        }

    }

    acc[0] = ( c4d[0] + c6d[1] + c2d[2] + c4d[3] );
    acc[1] = (-c4d[0] + c2d[1] - c6d[2] + c4d[3] );
    acc[2] = (-c4d[0] - c2d[1] + c6d[2] + c4d[3] );
    acc[3] = (+c4d[0] - c6d[1] - c2d[2] + c4d[3] );

    acc[4] = (+c5d[0] + c3d[1] + c7d[2] + c1d[3] );
    acc[5] = (-c1d[0] - c7d[1] + c5d[2] - c3d[3] );
    acc[6] = (-c7d[0] + c1d[1] + c3d[2] + c5d[3] );
    acc[7] = (-c3d[0] + c5d[1] + c1d[2] - c7d[3] );


    temp[0] = acc[0] + acc[4] + round;
    temp[1] = acc[1] + acc[5] + round;
    temp[2] = acc[2] + acc[6] + round;
    temp[3] = acc[3] + acc[7] + round;

    temp[4] = acc[3] - acc[7] + round;
    temp[5] = acc[2] - acc[6] + round;
    temp[6] = acc[1] - acc[5] + round;
    temp[7] = acc[0] - acc[4] + round;


    // Clipping
    for (i=0; i<8; i++) {
        temp[i] = (temp[i] >>11);
        if ( temp[i] >= 0 )
            out[i] = temp[i] <= 0x00007fff ? temp[i] : 0x00007fff;
        else
            out[i] = temp[i] >  0xffff8000 ? temp[i] : 0xffff8000;
    }

}
static void InvDivxDct2d (short *out, short *in)
{
    int i, j;
    int pat2_1, pat2_2;
    int temp[8];
    int acc[32];
    int pat1x0, pat2x0, X0[16], pat1x1, pat2x1, X1[16];
    int x0[4], x1[4];

    int c1d[4], c2d[4], c3d[4], c4d[4], c5d[4], c6d[4];
    int t0d16, t0d18;

    x0[0] = in[0];
    x0[1] = in[3];
    x0[2] = in[2];
    x0[3] = in[1];
    x1[0] = in[4];
    x1[1] = in[5];
    x1[2] = in[6];
    x1[3] = in[7];

    //----------------------------------------
    // Perform IDCT now.
    //----------------------------------------

    for (i = 0; i < 4; i++) {
        pat1x0 = (x0[i]<<1) + x0[i];
        pat2x0 = (x0[i]<<2) + x0[i];
        pat1x1 = (x1[i]<<1) + x1[i];
        pat2x1 = (x1[i]<<2) + x1[i];

        X0[0] = (x0[i]);
        X1[0] = (x1[i]);
        for (j = 1; j < 16; j++) {
            X0[j] = (X0[j-1]<<1);
            X1[j] = (X1[j-1]<<1);
        }
        c1d[i] = (((pat1x0<<12) + (pat2x0<<7) + (pat1x0<<5) + (pat1x0<<2))>>16);
        c2d[i] = (((pat1x0<<13) + (pat2x0<<9) + (pat2x0<<1))>>16);
        c3d[i] = ((-( (pat2x0<<12) + (pat2x0<<8) - X0[4] + X0[1] ))>>16);

        c4d[i] = (((pat1x1<<12) + (pat2x1<<7) + (pat1x1<<5) + (pat1x1<<2))>>16);
        c5d[i] = (((pat1x1<<13) + (pat2x1<<9) + (pat2x1<<1))>>16);
        c6d[i] = ((-( (pat2x1<<12) + (pat2x1<<8) - X1[4] + X1[1] ))>>16);

    }

    acc[ 0] = CLIP16( x0[0] + x1[0]);
    acc[ 2] = CLIP16( x0[0] - x1[0]);
    acc[ 4] = CLIP16(c5d[2] + x0[2]);
    acc[ 6] = CLIP16(c2d[2] - x1[2]);

    acc[ 1] = CLIP16(c4d[3] + x0[3]);
    acc[ 3] = CLIP16(c1d[3] - x1[3]);
    acc[ 5] = CLIP16(c6d[1] + x0[1]);
    acc[ 7] = CLIP16(c3d[1] + x0[1]);

    acc[ 8] = CLIP16(acc[5] + x1[1]);
    acc[ 9] = CLIP16(acc[7] - x1[1]);

    acc[10] = CLIP16(acc[0] + acc[4]);
    acc[12] = CLIP16(acc[0] - acc[4]);
    acc[14] = CLIP16(acc[2] + acc[6]);
    acc[16] = CLIP16(acc[2] - acc[6]);

    acc[11] = CLIP16(acc[1] + acc[8]);
    acc[13] = CLIP16(acc[1] - acc[8]);
    acc[15] = CLIP16(acc[3] + acc[9]);
    acc[17] = CLIP16(acc[3] - acc[9]);

    acc[18] = CLIP16(acc[13] + acc[15]);
    pat2_1  = (acc[18]<<2) + acc[18];
    t0d16   = (pat2_1<<12) + (pat2_1<<9) + (acc[18]<<7) + (acc[18]<<1);
    acc[19] = (((t0d16) >>16) <<1);

    acc[20] = CLIP16(acc[13] - acc[15]);
    pat2_2  = (acc[20]<<2) + acc[20];
    t0d18   = (pat2_2<<12) + (pat2_2<<9) + (acc[20]<<7) + (acc[20]<<1);
    acc[21] = (((t0d18) >>16) <<1);


    temp[0] = CLIP16( acc[10] + acc[11] );
    temp[1] = CLIP16( acc[14] + acc[19] );
    temp[2] = CLIP16( acc[16] + acc[21] );
    temp[3] = CLIP16( acc[12] + acc[17] );
    temp[4] = CLIP16( acc[12] - acc[17] );
    temp[5] = CLIP16( acc[16] - acc[21] );
    temp[6] = CLIP16( acc[14] - acc[19] );
    temp[7] = CLIP16( acc[10] - acc[11] );

    for(j=0; j<8; j++) out[j] = (short) (temp[j] >> 6);
}

static void idct_generic(short *block)
{
    short  xx[64], yy[64];
    int  i;

    for (i=0; i<64; i++)
        xx[i] = block[i];

       for (i=0; i<64; i+=8)
        InvDivxDct1d (yy+i, xx+i, i/8);

    for (i=0; i<64; i++)
        xx[i] = yy[(i%8)*8 + (i/8)];

       for (i=0; i<64; i+=8)
        InvDivxDct2d (yy+i, xx+i);

    for (i=0; i<64; i++)
        block[i] = (int) yy[(i%8)*8 + (i/8)];
}

void cnm_divx_idct_put(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int  i;

    idct_generic(block);

    for (i = 0; i < 8; i++) {
        dest[0] = CLIP255(block[0]);
        dest[1] = CLIP255(block[1]);
        dest[2] = CLIP255(block[2]);
        dest[3] = CLIP255(block[3]);
        dest[4] = CLIP255(block[4]);
        dest[5] = CLIP255(block[5]);
        dest[6] = CLIP255(block[6]);
        dest[7] = CLIP255(block[7]);

        dest += line_size;
        block  += 8;
    }
}
void cnm_divx_idct_add(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;

    idct_generic(block);

    for (i = 0; i < 8; i++) {
        dest[0] = CLIP255(dest[0]+block[0]);
        dest[1] = CLIP255(dest[1]+block[1]);
        dest[2] = CLIP255(dest[2]+block[2]);
        dest[3] = CLIP255(dest[3]+block[3]);
        dest[4] = CLIP255(dest[4]+block[4]);
        dest[5] = CLIP255(dest[5]+block[5]);
        dest[6] = CLIP255(dest[6]+block[6]);
        dest[7] = CLIP255(dest[7]+block[7]);

        dest += line_size;
        block  += 8;
    }
}

void cnm_idct_add(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;

    IdctBlk(block);
    for (i = 0; i < 8; i++) {
        dest[0] = CLIP255(dest[0]+block[0]);
        dest[1] = CLIP255(dest[1]+block[1]);
        dest[2] = CLIP255(dest[2]+block[2]);
        dest[3] = CLIP255(dest[3]+block[3]);
        dest[4] = CLIP255(dest[4]+block[4]);
        dest[5] = CLIP255(dest[5]+block[5]);
        dest[6] = CLIP255(dest[6]+block[6]);
        dest[7] = CLIP255(dest[7]+block[7]);

        dest += line_size;
        block  += 8;
    }
}
