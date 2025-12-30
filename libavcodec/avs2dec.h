/*
 * Chinese AVS2-Video (GY/T 299.1-2016 or IEEE 1857.4-2018) decoder.
 * Copyright (c) 2022 JianfengZheng <jianfeng.zheng@mthreads.com>
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
 * Chinese AVS2-Video (GY/T 299.1-2016 or IEEE 1857.4-2018) decoder
 * @author JianfengZheng <jianfeng.zheng@mthreads.com>
 */

#ifndef AVCODEC_AVS2DEC_H
#define AVCODEC_AVS2DEC_H

#include "avcodec.h"
#include "internal.h"
#include "bytestream.h"
#include "avs2.h"

int ff_avs2_decode_ext(AVS2Context *h, GetByteContext* bs, int b_seq_ext);
int ff_avs2_decode_user_data(AVS2Context *h, GetByteContext* bs);
int ff_avs2_decode_extradata(AVS2Context *h, const uint8_t *data, int size,
                             AVS2SeqHeader *seq);
int ff_avs2_decode_seq_header(AVS2Context *h, GetByteContext* bs, 
                             AVS2SeqHeader *seq);
int ff_avs2_decode_pic_header(AVS2Context *h, uint32_t stc, 
                              GetByteContext* bs, AVS2PicHeader *pic);
int ff_avs2_decode_slice_header(AVS2Context *h, uint32_t stc, GetByteContext *bs);

AVS2Frame* ff_avs2_dpb_get_frame_by_doi(AVS2Context *h, int doi);

#endif /* AVCODEC_AVS2DEC_H */
