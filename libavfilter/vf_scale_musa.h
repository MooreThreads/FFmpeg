/*
 * This file is part of FFmpeg.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef AVFILTER_SCALE_MUSA_H
#define AVFILTER_SCALE_MUSA_H

#if defined(__MUSA__) || defined(__MUSA_ARCH__)
#include <stdint.h>
#include <texture_types.h>
typedef musaTextureObject_t MUSAScaleTexObject;
typedef uint8_t *MUSAScaleDevicePtr;
#else
#include "libavutil/hwcontext_musa.h"
typedef MUtexObject MUSAScaleTexObject;
typedef MUdeviceptr MUSAScaleDevicePtr;
#endif

#define SCALE_MUSA_PARAM_DEFAULT 999999.0f

typedef struct {
    MUSAScaleTexObject src_tex[4];
    MUSAScaleDevicePtr dst[4];
    int dst_width;
    int dst_height;
    int dst_pitch;
    int src_left;
    int src_top;
    int src_width;
    int src_height;
    float param;
    int mpeg_range;
} MUSAScaleKernelParams;

#endif
