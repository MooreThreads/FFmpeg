/*
 * Chinese AVS2-Video (GY/T 299.1-2016 or IEEE 1857.4-2018) decoder.
 *
 * Copyright (c) 2022 JianfengZheng <jianfeng.zheng@mthreads.com>
 * Copyright (C) 2022 Zhao Zhili, <zhilizhao@tencent.com>
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
 * Chinese AVS2-Video (GY/T 299.1-2016 or IEEE 1857.4-2018) definitions
 * @author JianfengZheng <jianfeng.zheng@mthreads.com>
 */

#include "internal.h"
#include "startcode.h"
// #include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "avs2.h"

const AVRational ff_avs2_frame_rate_tab[16] = {
    { 0    , 0   }, // forbid
    { 24000, 1001},
    { 24   , 1   },
    { 25   , 1   },
    { 30000, 1001},
    { 30   , 1   },
    { 50   , 1   },
    { 60000, 1001},
    { 60   , 1   },
    { 100  , 1   },
    { 120  , 1   },
    { 200  , 1   },
    { 240  , 1   },
    { 300  , 1   },
    { 0    , 0   }, // reserved
    { 0    , 0   }  // reserved
};

static AVS2LevelLimit const *ff_avs2_get_level_limits(int level)
{
    static const AVS2LevelLimit level_limits[] = {
        /*                 level,   w,    h, fr,  slc,        sr,         br,       bbv */
        {   AVS2_LEVEL_FORBIDDEN,   0,    0,  0,    0,         0,          0,         0 },

        {   AVS2_LEVEL_2_0_15,    352,  288,  15,  16,    1520640,   1500000,   1507328 },
        {   AVS2_LEVEL_2_0_15,    352,  288,  30,  16,    3041280,   2000000,   2015232 },
        {   AVS2_LEVEL_2_0_15,    352,  288,  60,  16,    6082560,   2500000,   2506752 },

        {   AVS2_LEVEL_4_0_30,    720,  576,  30,  32,   12441600,   6000000,   6012928 },
        {   AVS2_LEVEL_4_0_60,    720,  576,  60,  32,   24883200,  10000000,  10010624 },

        {   AVS2_LEVEL_6_0_30,   2048, 1152,  30,  64,   66846720,  12000000,  12009472 },
        {   AVS2_LEVEL_6_2_30,   2048, 1152,  30,  64,   66846720,  30000000,  30015488 },
        {   AVS2_LEVEL_6_0_60,   2048, 1152,  60,  64,  133693440,  20000000,  20004864 },
        {   AVS2_LEVEL_6_2_60,   2048, 1152,  60,  64,  133693440,  50000000,  50003968 },
        {   AVS2_LEVEL_6_0_120,  2048, 1152, 120,  64,  267386880,  25000000,  25001984 },
        {   AVS2_LEVEL_6_2_120,  2048, 1152, 120,  64,  267386880, 100000000, 100007936 },

        {   AVS2_LEVEL_8_0_30,   4096, 2304,  30, 128,  283115520,  25000000,  25001984 },
        {   AVS2_LEVEL_8_2_30,   4096, 2304,  30, 128,  283115520, 100000000, 100007936 },
        {   AVS2_LEVEL_8_0_60,   4096, 2304,  60, 128,  566231040,  40000000,  40009728 },
        {   AVS2_LEVEL_8_2_60,   4096, 2304,  60, 128,  566231040, 160000000, 160006144 },
        {   AVS2_LEVEL_8_0_120,  4096, 2304, 120, 128, 1132462080,  60000000,  60014592 },
        {   AVS2_LEVEL_8_2_120,  4096, 2304, 120, 128, 1132462080, 240000000, 240009216 },

        {   AVS2_LEVEL_10_0_30,  8192, 4608,  30, 256, 1069547520,  60000000,  60014592 },
        {   AVS2_LEVEL_10_2_30,  8192, 4608,  30, 256, 1069547520, 240000000, 240009216 },
        {   AVS2_LEVEL_10_0_60,  8192, 4608,  60, 256, 2139095040, 120000000, 120012800 },
        {   AVS2_LEVEL_10_2_60,  8192, 4608,  60, 256, 2139095040, 480000000, 480002048 },
        {   AVS2_LEVEL_10_0_120, 8192, 4608, 120, 256, 4278190080, 240000000, 240009216 },
        {   AVS2_LEVEL_10_2_120, 8192, 4608, 120, 256, 4278190080, 800000000, 800014336 },  
    };
    int nb_limits = FF_ARRAY_ELEMS(level_limits);
    for (int i = 0; i < nb_limits; i++) {
        if (level == level_limits[i].level) {
            return &level_limits[i];
        }
    }
    return NULL;
}

void ff_avs_get_cu_align_size(AVS2SeqHeader *seq, int *w, int *h)
{
    int mini_size = AVS2_MINI_SIZE;
    int align_w = (seq->width + mini_size - 1) / mini_size * mini_size;
    int align_h = (seq->height + mini_size - 1) / mini_size * mini_size;
    if (w) *w = align_w;
    if (h) *h = align_h;
}

int ff_avs2_get_max_dpb_size(AVS2SeqHeader *seq)
{
    int ret = 16;
    int aw, ah;
    ff_avs_get_cu_align_size(seq, &aw, & ah);

    if (seq->level_id <= AVS2_LEVEL_4_0_60) {
        return 15;
    } else if (seq->level_id <= AVS2_LEVEL_6_2_120) {
        ret = 13369344 / (aw * ah);
    } else if (seq->level_id <= AVS2_LEVEL_8_2_120) {
        ret = 56623104 / (aw * ah);
    } else if (seq->level_id <= AVS2_LEVEL_10_2_120) {
        ret = 213909504 / (aw * ah);
    }

    return (ret < 16 ? ret : 16) - 1;
}

static const AVS2WQMatrix avs2_default_wqm = {
    .m44 = {
        64, 64, 64, 68,
        64, 64, 68, 72,
        64, 68, 76, 80,
        72, 76, 84, 96
    },
    .m88 = {
        64,  64,  64,  64,  68,  68,  72,  76,
        64,  64,  64,  68,  72,  76,  84,  92,
        64,  64,  68,  72,  76,  80,  88,  100,
        64,  68,  72,  80,  84,  92,  100, 112,
        68,  72,  80,  84,  92,  104, 112, 128,
        76,  80,  84,  92,  104, 116, 132, 152,
        96,  100, 104, 116, 124, 140, 164, 188,
        104, 108, 116, 128, 152, 172, 192, 216  
    }
};

void ff_avs2_set_default_wqm(AVS2WQMatrix* wqm) {
    memcpy(wqm, &avs2_default_wqm, sizeof(AVS2WQMatrix));
}

void ff_avs2_set_default_seq_header(AVS2SeqHeader *seq)
{
    memset(seq, 0, sizeof(AVS2SeqHeader));

    ff_avs2_set_default_wqm(&seq->wqm);
}

void ff_avs2_set_default_pic_header(AVS2SeqHeader *seq, AVS2PicHeader *pic, int b_intra)
{
    memset(pic, 0, sizeof(AVS2PicHeader));
    pic->b_intra = b_intra;

    pic->b_picture_structure = AVS2_FIELD_INTERLEAVED;
    pic->b_random_access = 1;

    ff_avs2_set_default_wqm(&pic->wqm);
}

/* frame rate code 2 rational value */
AVRational ff_avs2_frame_rate_c2q(int fr_code)
{
    switch (fr_code)
    {
    case AVS2_FR_23_976  : return av_make_q(24000, 1001);
    case AVS2_FR_24      : return av_make_q(24, 1);
    case AVS2_FR_25      : return av_make_q(25, 1);
    case AVS2_FR_29_970  : return av_make_q(30000, 1001);
    case AVS2_FR_30      : return av_make_q(30, 1);
    case AVS2_FR_50      : return av_make_q(50, 1);
    case AVS2_FR_59_940  : return av_make_q(60000, 1001);
    case AVS2_FR_60      : return av_make_q(60, 1);
    case AVS2_FR_100     : return av_make_q(100, 1);
    case AVS2_FR_120     : return av_make_q(120, 1);
    case AVS2_FR_200     : return av_make_q(200, 1);
    case AVS2_FR_240     : return av_make_q(240, 1);
    case AVS2_FR_300     : return av_make_q(300, 1);
    default:
        return av_make_q(0, 1);
    }
}

AVRational ff_avs2_get_sar(AVS2SeqHeader* seq)
{
    AVRational sar = av_make_q(1, 1);
    switch (seq->aspect_ratio_code)
    {
    case AVS2_DAR_4_3:
        sar = av_make_q(4 * seq->height, 3 * seq->width);
        break;
    case AVS2_DAR_16_9:
        sar = av_make_q(16 * seq->height, 9 * seq->width);
        break;
    case AVS2_DAR_221_100:
        sar = av_make_q(221 * seq->height, 100 * seq->width);
        break;
    default:
        break;
    }
    av_reduce(&sar.den, &sar.num, sar.den, sar.num, 1 << 30);
    return sar;
}

int ff_avs2_get_pic_type(AVS2PicHeader *pic) 
{
    if (pic->b_intra) {
        return pic->b_scene_pic ? (pic->b_scene_pic_output ? AVS2_PIC_G : AVS2_PIC_GB) 
                                : AVS2_PIC_I;
    } else {
        switch (pic->pic_coding_type)
        {
        case AVS2_PCT_P: return pic->b_scene_pred ? AVS2_PIC_S : AVS2_PIC_P;
        case AVS2_PCT_B: return AVS2_PIC_B;
        case AVS2_PCT_F: return AVS2_PIC_F;
        default:         return AVS2_PIC_UNKNOWN;
        } 
    }
}

const char* ff_avs2_pic_type_to_str(int type)
{
    static const char* type_str[] = {
        "I", "P", "B", "F", "S", "G", "GB"
    };
    if (type >= AVS2_PIC_I && type <= AVS2_PIC_GB) {
        return type_str[type];
    }
    return "unknown";
}

const char* ff_avs2_get_pic_type_str(AVS2PicHeader *pic)
{
    int type = ff_avs2_get_pic_type(pic);
    return ff_avs2_pic_type_to_str(type);
}

int ff_avs2_packet_split(AVS2PacketSplit *pkt, const uint8_t *data, int size, void *logctx)
{
    GetByteContext _bs, *bs=&_bs;
    bytestream2_init(bs, data, size);
    
    memset(pkt, 0, sizeof(*pkt));
    while (bytestream2_get_bytes_left(bs) >= 4) {
        AVS2EsUnit *unit = 0;
        int valid_slice = 0;
        uint32_t stc = -1;
        bs->buffer = avpriv_find_start_code(bs->buffer, bs->buffer_end, &stc);
        if (bs->buffer <= bs->buffer_end && (stc & 0xFFFFFF00) == 0x100) {
            if (!ff_avs2_valid_start_code(stc)) {
                av_log(logctx, AV_LOG_ERROR, "Invalid startcode 0x%08x @%d !!!\n", 
                        stc, bytestream2_tell(bs));
                return AVERROR_INVALIDDATA;
            }
            
            valid_slice = ff_avs2_valid_slice_stc(stc);

            if (pkt->nb_alloc < pkt->nb_units + 1) {
                int new_space = pkt->nb_units + 4;
                void *tmp = av_realloc_array(pkt->units, new_space, sizeof(*pkt->units));
                if (!tmp)
                    return AVERROR(ENOMEM);

                pkt->units = tmp;
                memset(pkt->units + pkt->nb_alloc, 0, 
                        (new_space - pkt->nb_alloc) * sizeof(*pkt->units));
                pkt->nb_alloc = new_space;
            }
            
            unit = &pkt->units[pkt->nb_units];
            if (valid_slice)
                bytestream2_seek(bs, -1, SEEK_CUR);
                
            unit->start_code = stc;
            unit->data_start = bytestream2_tell(bs);
            unit->data_len   = bytestream2_get_bytes_left(bs);

            // amend previous data_len
            if (pkt->nb_units > 0) {
                unit[-1].data_len -= 4 + unit->data_len - valid_slice;
            } 

            pkt->nb_units += 1;
        } else {
            break;
        }
    }

    av_log(logctx, AV_LOG_DEBUG, "pkt size=%d, nalu=%d:", size, pkt->nb_units);

    if (pkt->nb_units == 0) {
        av_log(logctx, AV_LOG_ERROR, "No NALU found in this packet !!!");
        return AVERROR_INVALIDDATA;
    } else {
        int first_stc_pos = pkt->units[0].data_start - 4;
        if (first_stc_pos > 0) {
            av_log(logctx, AV_LOG_WARNING, "First NALU @%d dons't start from pos 0!",
                    first_stc_pos);
        }
    }

    for (int i = 0; i < pkt->nb_units; i++) {
        AVS2EsUnit *unit = &pkt->units[i];
        av_log(logctx, AV_LOG_DEBUG, " [%02X] ..%ld..", unit->start_code & 0xff, unit->data_len);
    }
    av_log(logctx, AV_LOG_DEBUG, "\n");
    return 0;
}

/**
 * Free all the allocated memory in the packet.
 */
void ff_avs2_packet_uninit(AVS2PacketSplit *pkt) {
    av_freep(&pkt->units);
    pkt->nb_units = 0;
    pkt->nb_alloc = 0;
}

int ff_avs2_remove_pseudo_code(uint8_t *dst, const uint8_t *src, int size)
{
    static const uint8_t BITMASK[] = { 0x00, 0x00, 0xc0, 0x00, 0xf0, 0x00, 0xfc, 0x00 };
    int src_pos = 0;
    int dst_pos = 0;
    int cur_bit = 0;
    int last_bit = 0;
    
    uint8_t cur_byte = 0;
    uint8_t last_byte = 0;
    
    
    while (src_pos < 2 && src_pos < size){
        dst[dst_pos++] = src[src_pos++];
    }
    
    while (src_pos < size){
        cur_bit = 8;
        if (src[src_pos-2] == 0 && src[src_pos-1] == 0 && src[src_pos] == 0x02)
            cur_bit = 6;
        cur_byte = src[src_pos++];
        
        if (cur_bit == 8) {
            if (last_bit == 0) {
                dst[dst_pos++] = cur_byte;
            } else {
                dst[dst_pos++] = ((last_byte & BITMASK[last_bit]) | ((cur_byte & BITMASK[8 - last_bit]) >> last_bit));
                last_byte      = (cur_byte << (8 - last_bit)) & BITMASK[last_bit];
            }
        } else {
            if (last_bit == 0) {
                last_byte = cur_byte;
                last_bit  = cur_bit;
            } else {
                dst[dst_pos++] = ((last_byte & BITMASK[last_bit]) | ((cur_byte & BITMASK[8 - last_bit]) >> last_bit));
                last_byte = (cur_byte << (8 - last_bit)) & BITMASK[last_bit - 2];
                last_bit  = last_bit - 2;
            }
        }
    }
    
    if (last_bit != 0 && last_byte != 0) {
        dst[dst_pos++] = last_byte;
    }
    return dst_pos; // dst size
}
