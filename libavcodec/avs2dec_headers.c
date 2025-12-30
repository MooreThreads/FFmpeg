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
 * @file avs2dec_headers.c
 * @author JianfengZheng <jianfeng.zheng@mthreads.com>
 * @brief Chinese AVS2-Video (GY/T 299.1-2016) headers decoding
 */

#include <ctype.h>
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "get_bits.h"
#include "golomb.h"
#include "profiles.h"
#include "libavcodec/mpegvideodec.h"
#include "avs2dec.h"


static const uint8_t avs2_wq_model88[4][64] = {
    //   l a b c d h
    //     0 1 2 3 4 5
    {
    // Mode 0
    0,0,0,4,4,4,5,5,
    0,0,3,3,3,3,5,5,
    0,3,2,2,1,1,5,5,
    4,3,2,2,1,5,5,5,
    4,3,1,1,5,5,5,5,
    4,3,1,5,5,5,5,5,
    5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5 },
    {
    // Mode 1
    0,0,0,4,4,4,5,5,
    0,0,4,4,4,4,5,5,
    0,3,2,2,2,1,5,5,
    3,3,2,2,1,5,5,5,
    3,3,2,1,5,5,5,5,
    3,3,1,5,5,5,5,5,
    5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5 },
    {
    // Mode 2
    0,0,0,4,4,3,5,5,
    0,0,4,4,3,2,5,5,
    0,4,4,3,2,1,5,5,
    4,4,3,2,1,5,5,5,
    4,3,2,1,5,5,5,5,
    3,2,1,5,5,5,5,5,
    5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5 },
    {
    // Mode 3
    0,0,0,3,2,1,5,5,
    0,0,4,3,2,1,5,5,
    0,4,4,3,2,1,5,5,
    3,3,3,3,2,5,5,5,
    2,2,2,2,5,5,5,5,
    1,1,1,5,5,5,5,5,
    5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5 }
};

static const uint8_t avs2_wq_model44[4][16] = {
    //   l a b c d h
    //     0 1 2 3 4 5
    {
    // Mode 0
    0, 4, 3, 5,
    4, 2, 1, 5,
    3, 1, 1, 5,
    5, 5, 5, 5 },
    {
    // Mode 1
    0, 4, 4, 5,
    3, 2, 2, 5,
    3, 2, 1, 5,
    5, 5, 5, 5 },
    {
    // Mode 2
    0, 4, 3, 5,
    4, 3, 2, 5,
    3, 2, 1, 5,
    5, 5, 5, 5 },
    {
    // Mode 3
    0, 3, 1, 5,
    3, 4, 2, 5,
    1, 2, 2, 5,
    5, 5, 5, 5 }
};

static const uint8_t avs2_default_wq_param[2][6]=
{
    { 67,71,71,80,80,106 },
    { 64,49,53,58,58,64 }
};


static int ff_avs2_decode_rcs(GetBitContext* gb, AVS2RefCfgSet* rcs, void* logctx) {
    int j = 0;
    rcs->b_ref_by_others        = get_bits1(gb);
    rcs->n_ref                  = get_bits(gb, 3);
    for (j = 0; j < rcs->n_ref; j++) {
        rcs->ref_delta_doi[j]   = get_bits(gb, 6);
    }
    rcs->n_rm                   = get_bits(gb, 3);
    for (j = 0; j < rcs->n_rm; j++) {
        rcs->rm_delta_doi[j]    = get_bits(gb, 6);
    }
    if(check_marker(logctx, gb, "[end of 'rcs[i]']")==0) 
        return AVERROR_INVALIDDATA;
    return 0;
}

static int ff_avs2_decode_wqm(GetBitContext* gb, AVS2WQMatrix *wqm) {
    int i;
    for (i = 0; i < 16; i++) {
        wqm->m44[i] = get_ue_golomb(gb);
    }
    for (i = 0; i < 64; i++) {
        wqm->m88[i] = get_ue_golomb(gb);
    }
    return 0;
}

static void ff_avs2_amend_alf_coeff(int8_t *dst, const int16_t *src)
{
    int i, sum = src[8] + 64;
    for (i = 0; i < 8; i++) {
        dst[i] = av_clip(src[i], -64, 63);
        sum -= 2 * dst[i];
    }
    dst[8] = av_clip(sum, 0, 127);
 } 

/**
 * Chapter 9.12.2 of GY/T 299.1-2016
 */
static void ff_avs2_process_alf_param(int8_t (*coeff)[9], const AVS2AlfParam *alf) 
{
    int i, j, c = 0;
    int tab[16] = { 0 };
    if(alf->b_enable[0]){
        // distance:[0,2,3,5] -> tab:[0,0, 1,1,1, 2,2,2,2,2, 3,3,3,3,3,3] 
        for (i = 1; i < alf->luma.n_filter; i++) {
            for (j = 0; j < alf->luma.region_distance[i]; j++) {
                tab[c+1] = tab[c];
                c += 1;
            }
            tab[c] += 1;
        }
        for (i = c; i < 16; i++) {
            tab[i] = tab[c];
        }

        for (i = 0; i < 16; i++) {
            ff_avs2_amend_alf_coeff(coeff[i], alf->luma.coeff[tab[i]]);
        }
    }
    for (i = 0; i < 2; i++) {
        if(alf->b_enable[i+1])
            ff_avs2_amend_alf_coeff(coeff[16 + i], alf->chroma[i].coeff);
    }
}

static int ff_avs2_decode_alf_param(GetBitContext* gb, AVS2PicHeader *pic) 
{
    int i, j, s;
    AVS2AlfParam _alf, *alf = &_alf;
    memset(alf, 0, sizeof(AVS2AlfParam));
    for (i = 0; i < 16; i++) {
        alf->luma.region_distance[i] = i > 0;
    }

    /**
     * Chapter 7.1.8 of GY/T 299.1-2016
     */
    for (i = 0; i < 3; i++) {
        alf->b_enable[i] = get_bits1(gb);
    }

    if (alf->b_enable[0]) {
        alf->luma.n_filter = get_ue_golomb(gb) + 1;
        for (i = 0; i < alf->luma.n_filter; i++) {
            if (i > 0 && alf->luma.n_filter != 16) {
                alf->luma.region_distance[i] = get_ue_golomb(gb);
            }
            for (j = 0; j < 9; j++) {
                alf->luma.coeff[i][j] = get_se_golomb(gb);
            }
        }
    }
    for (i = 0; i < 2; i++) {
        if (alf->b_enable[i+1]) {
            for (j = 0; j < 9; j++) {
                alf->chroma[i].coeff[j] = get_se_golomb(gb);
            }
        }
    }

    /**
     * Chapter 7.2.8 of GY/T 299.1-2016
     */
    for (s = 0, i = 0; i < alf->luma.n_filter; i++) {
        s += alf->luma.region_distance[i];
    }
    if (s > 15) {
        return AVERROR_INVALIDDATA;
    }

    pic->b_alf_enable[0] = alf->b_enable[0];
    pic->b_alf_enable[1] = alf->b_enable[1];
    pic->b_alf_enable[2] = alf->b_enable[2];
    ff_avs2_process_alf_param(pic->alf_coeff, alf);

    return 0;
}

int ff_avs2_decode_seq_header(AVS2Context *h, GetByteContext* bs, AVS2SeqHeader *seq)
{
    int i;
    unsigned int br_lower, br_upper;
    GetBitContext _gb, *gb = &_gb;
    init_get_bits8(gb, bs->buffer, bs->buffer_end - bs->buffer);

    ff_avs2_set_default_seq_header(&h->seq);
    seq->profile_id     = get_bits(gb, 8);
    seq->level_id       = get_bits(gb, 8);
    seq->b_progressive  = get_bits1(gb);
    seq->b_field_coding = get_bits1(gb);

    seq->width          = get_bits(gb, 14);
    seq->height         = get_bits(gb, 14);
    if (seq->width < 16 || seq->height < 16) {
        return AVERROR_INVALIDDATA;
    }

    seq->chroma_format  = get_bits(gb, 2);
    if (seq->chroma_format != AVS2_CHROMA_YUV_420) {
        av_log(h, AV_LOG_ERROR, "AVS2 don't support chroma format other than YUV420 !!!\n");
        return AVERROR_INVALIDDATA;
    }

    /* sample_precision seems useless */
    if (seq->profile_id == AVS2_PROFILE_MAIN_10) {
        seq->output_bit_depth = 6 + (get_bits(gb, 3) << 1);
        seq->sample_bit_depth = 6 + (get_bits(gb, 3) << 1);
    } else {
        seq->output_bit_depth = 6 + (get_bits(gb, 3) << 1);
        seq->sample_bit_depth = 8;
    }
    if (seq->sample_bit_depth != 8 && seq->sample_bit_depth != 10) {
        av_log(h, AV_LOG_ERROR, "Invalid sample_precision : %d !!!\n", seq->sample_bit_depth);
        return AVERROR_INVALIDDATA;
    }
    if (seq->output_bit_depth != 8 && seq->output_bit_depth != 10) {
        av_log(h, AV_LOG_ERROR, "Invalid encoding_precision : %d !!!\n", seq->output_bit_depth);
        return AVERROR_INVALIDDATA;
    }
    if (seq->sample_bit_depth < seq->output_bit_depth) {
        av_log(h, AV_LOG_ERROR, "encoding_precision smaller than sample_precision !!!\n");
        return AVERROR_INVALIDDATA;
    }
    seq->aspect_ratio_code  = get_bits(gb, 4);
    seq->frame_rate_code    = get_bits(gb, 4);
    
    br_lower    = get_bits(gb, 18);
    if(check_marker(h, gb, "[before 'bit_rate_upper']")==0) 
        return AVERROR_INVALIDDATA;
    br_upper    = get_bits(gb, 12);
    seq->bitrate = ((br_upper << 18) + br_lower) * (int64_t)400;

    seq->b_low_delay        = get_bits1(gb);
    if(check_marker(h, gb, "[before 'temporal_id_enable_flag']")==0) 
        return AVERROR_INVALIDDATA;
    seq->b_has_temporal_id  = get_bits1(gb);
    seq->bbv_buffer_size    = get_bits(gb, 18);
    seq->log2_lcu_size      = get_bits(gb, 3);;

    if (seq->log2_lcu_size < 4 || seq->log2_lcu_size > 6) {
        av_log(h, AV_LOG_ERROR, "Invalid LCU size: %d\n", seq->log2_lcu_size);
        return AVERROR_INVALIDDATA;
    }

    seq->b_enable_wq        = get_bits1(gb);
    if (seq->b_enable_wq) {
        if (get_bits1(gb)) {
            ff_avs2_decode_wqm(gb, &seq->wqm);
        }
    }

    seq->b_disable_scene_pic            = get_bits1(gb);
    seq->b_multi_hypothesis_skip        = get_bits1(gb);
    seq->b_dual_hypothesis_prediction   = get_bits1(gb);
    seq->b_weighted_skip                = get_bits1(gb);

    seq->b_amp  = get_bits1(gb);
    seq->b_nsqt = get_bits1(gb);
    seq->b_nsip = get_bits1(gb);
    seq->b_2nd_transform    = get_bits1(gb);
    seq->b_sao  = get_bits1(gb);
    seq->b_alf  = get_bits1(gb);
    seq->b_pmvr = get_bits1(gb);

    if(check_marker(h, gb, "[before 'num_of_rcs']")==0) 
        return AVERROR_INVALIDDATA;
    seq->n_rcs  = get_bits(gb, 6);
    if (seq->n_rcs > AVS2_MAX_RCS_COUNT) {
        av_log(h, AV_LOG_ERROR, "num_of_rcs(%d) should not exceed 32\n", seq->n_rcs);
        return AVERROR_INVALIDDATA;
    }
    for (i = 0; i < seq->n_rcs; i++) {
        AVS2RefCfgSet* rcs = &seq->rcs[i];
        if (ff_avs2_decode_rcs(gb, rcs, h) < 0) {
            return AVERROR_INVALIDDATA;
        }
    }

    if (seq->b_low_delay == 0) {
        seq->output_reorder_delay   = get_bits(gb, 5);
    }
    seq->b_cross_slice_loop_filter  = get_bits1(gb);

    skip_bits(gb, 2);
    align_get_bits(gb);

    av_log(h, AV_LOG_INFO, "Got seq header: %dx%d, lcu:%d, profile=%d, level=%d\n", 
            seq->width, seq->height, seq->log2_lcu_size,
            seq->profile_id, seq->level_id);
    return 0;
}

int ff_avs2_decode_user_data(AVS2Context *h, GetByteContext* bs)
{
    const uint8_t *p;
    const uint8_t *log_fmt;
    int log_level;

    for (p = bs->buffer; p < bs->buffer_end && isprint(*p); p++) {}
    if (p == bs->buffer_end) {
        log_level = AV_LOG_DEBUG;
        log_fmt = "%c";
    } else {
        log_level = AV_LOG_TRACE;
        log_fmt = "%02x ";
    }

    av_log(h, log_level, "Got user Data: ");
    for (p = bs->buffer; p < bs->buffer_end; p++)
        av_log(h, log_level, log_fmt, *p);
    av_log(h, log_level, "\n");

    return 0;
}

static int ff_avs2_decode_seq_display_ext(AVS2Context *h, GetBitContext* gb)
{
    AVS2SeqDisplayExt* ext = &h->seq_display_ext;
    ext->extension_id = AVS2_EXT_SEQ_DISPLAY;

    ext->video_format = get_bits(gb, 3);
    ext->b_full_range = get_bits1(gb);
    ext->b_color_desc = get_bits1(gb);
    if (ext->b_color_desc) {
        ext->color_primaries = get_bits(gb, 8);
        ext->color_transfer  = get_bits(gb, 8);
        ext->color_matrix    = get_bits(gb, 8);
    }
    ext->display_h = get_bits(gb, 14);
    if(check_marker(h, gb, "[sequence_display_extension]")==0) 
        return AVERROR_INVALIDDATA;
    ext->display_w = get_bits(gb, 14);

    ext->b_td_mode = get_bits1(gb);
    if (ext->b_td_mode) {
        ext->td_packing_mode = get_bits(gb, 8);
        ext->b_view_reverse = get_bits1(gb);
    }

    av_log(h, AV_LOG_INFO, "Got sequence_display_extension\n");
    return 0;
}

static int ff_avs2_decode_temporal_scale_ext(AVS2Context *h, GetBitContext* gb)
{
    int i, fr_code, br_lower, br_upper;
    AVS2TemporalScaleExt* ext = &h->tempo_scale_ext;
    ext->extension_id = AVS2_EXT_TEMPORAL_SCALE;

    av_log(h, AV_LOG_INFO, "got temporal_scalability_extension()\n");

    ext->n_level = get_bits(gb, 3);
    if (get_bits_left(gb) < 33 * ext->n_level) {
        av_log(h, AV_LOG_ERROR, "NOT enough data for temporal_scalability_extension()\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < ext->n_level; i++) {
        fr_code  = get_bits(gb, 4);
        br_lower = get_bits(gb, 18);
        if(check_marker(h, gb, "[temporal_scale_ext]")==0) 
            return AVERROR_INVALIDDATA;
        br_upper = get_bits(gb, 12);
        ext->level[i].framerate = ff_avs2_frame_rate_c2q(fr_code);
        ext->level[i].bitrate   = ((br_upper << 18) + br_lower) * (int64_t)400;
    }

    av_log(h, AV_LOG_INFO, "Got temporal_scalability_extension: %d level\n", ext->n_level);
    for (i = 0; i < ext->n_level; i++) {
        av_log(h, AV_LOG_INFO, "level[%d] : framerate=%f, bitrate=%" PRId64 "\n",  
                i, av_q2d(ext->level[i].framerate), ext->level[i].bitrate);
    }
    return 0;
}

static int ff_avs2_decode_copyright_ext(AVS2Context *h, GetBitContext* gb, AVS2CopyrightExt* ext)
{
    if (get_bits_left(gb) < 1+8+1+7+23*3 ) {
        av_log(h, AV_LOG_ERROR, "NOT enough data for copyright_extension()\n");
        return AVERROR_INVALIDDATA;
    }
    ext->extension_id = AVS2_EXT_COPYRIGHT;

    ext->b_flag = get_bits1(gb);
    ext->copy_id = get_bits(gb, 8);
    ext->b_original = get_bits1(gb);
    skip_bits(gb, 7);

    if(check_marker(h, gb, "copyright_number_1")==0) 
        return AVERROR_INVALIDDATA;
    ext->copy_number = (uint64_t)get_bits(gb, 20) << 44;
    
    if(check_marker(h, gb, "copyright_number_2")==0) 
        return AVERROR_INVALIDDATA;
    ext->copy_number += (uint64_t)get_bits(gb, 22) << 22;

    if(check_marker(h, gb, "copyright_number_3")==0) 
        return AVERROR_INVALIDDATA;
    ext->copy_number += (uint64_t)get_bits(gb, 22);

    av_log(h, AV_LOG_INFO, "Got copyright_extension: original:%d, id:%d, number%" PRId64 "\n",
            ext->b_original, ext->copy_id, ext->copy_number);
    return 0;
}

static int ff_avs2_decode_pic_display_ext(AVS2Context *h, GetBitContext* gb)
{
    int i = 0;
    AVS2SeqHeader *seq = &h->seq;
    AVS2PicHeader *pic = &h->pic;
    AVS2PicDisplayExt *ext = &h->pic_display_ext;
    ext->extension_id = AVS2_EXT_PIC_DISPLAY;
    
    if (seq->b_progressive) {
        if (pic->b_repeat_first_field) {
            ext->n_offset = pic->b_top_field_first ? 3 : 2;
        } else {
            ext->n_offset = 1;
        }
    } else {
        if (pic->b_picture_structure == 0) {
            ext->n_offset = 1;
        } else {
            ext->n_offset = pic->b_repeat_first_field ? 3 : 2;
        }
    }

    if (get_bits_left(gb) < 34 * ext->n_offset) {
        av_log(h, AV_LOG_ERROR, "NOT enough data for picture_display_extension()\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < ext->n_offset; i++) {
        ext->offset[i][0] = (int16_t)get_bits(gb, 16);
        if(check_marker(h, gb, "picture_centre_horizontal_offset")==0) 
            return AVERROR_INVALIDDATA;

        ext->offset[i][1] = (int16_t)get_bits(gb, 16);
        if(check_marker(h, gb, "picture_centre_vertical_offset")==0) 
            return AVERROR_INVALIDDATA;
    }

    av_log(h, AV_LOG_INFO, "Got picture_display_extension\n");
    return 0;
}

int ff_avs2_decode_ext(AVS2Context *h, GetByteContext* bs, int b_seq_ext)
{
    int ret = 0;
    int ext_type = 0;
    GetBitContext _gb, *gb = &_gb;
    init_get_bits8(gb, bs->buffer, bs->buffer_end - bs->buffer);

    ext_type = get_bits(gb, 4);
    if (b_seq_ext) {
        if (ext_type == AVS2_EXT_SEQ_DISPLAY) {
            ret = ff_avs2_decode_seq_display_ext(h, gb);
        } else if (ext_type == AVS2_EXT_TEMPORAL_SCALE) {
            ret = ff_avs2_decode_temporal_scale_ext(h, gb);
        } else if (ext_type == AVS2_EXT_COPYRIGHT) {
            ret = ff_avs2_decode_copyright_ext(h, gb, &h->seq_copyright_ext);
        } else if (ext_type == AVS2_EXT_MASTERING) {
            av_log(h, AV_LOG_WARNING, "Skip mastering_display_and_content_metadata_extension() \n");
        } else if (ext_type == AVS2_EXT_CAMERA_PARAM) {
            av_log(h, AV_LOG_WARNING, "Skip seq camera_parameters_extension() \n");
        } else {
            av_log(h, AV_LOG_WARNING, "Skip seq reserved_extension_data_byte \n");
        }
    } else {
        if (ext_type == AVS2_EXT_COPYRIGHT) {
            ret = ff_avs2_decode_copyright_ext(h, gb, &h->pic_copyright_ext);
        } else if (ext_type == AVS2_EXT_PIC_DISPLAY) {
            ret = ff_avs2_decode_pic_display_ext(h, gb);
        } else if (ext_type == AVS2_EXT_CAMERA_PARAM) {
            av_log(h, AV_LOG_WARNING, "Skip pic camera_parameters_extension()() \n");
        } else if (ext_type == AVS2_EXT_ROI_PARAM) {
            av_log(h, AV_LOG_WARNING, "Skip roi_parameters_extension() \n");
        } else {
            av_log(h, AV_LOG_WARNING, "Skip pic reserved_extension_data_byte \n");
        }
    }
    AVS2_CHECK_RET(ret);

    return 0;
}

int ff_avs2_decode_extradata(AVS2Context *h, const uint8_t *data, int size,
                             AVS2SeqHeader *seq)
{
    int ret = 0;
    int i_unit = 0;

    ret = ff_avs2_packet_split(&h->pkt_split, data, size, h);
    AVS2_CHECK_RET(ret);

    for (i_unit = 0; i_unit < h->pkt_split.nb_units; i_unit++) {
        AVS2EsUnit* unit = &h->pkt_split.units[i_unit];
        GetByteContext _bs, *bs=&_bs;
        bytestream2_init(bs, data + unit->data_start, unit->data_len);

        switch (unit->start_code)
        {
        case AVS2_STC_SEQ_HEADER:
            ret = ff_avs2_decode_seq_header(h, bs, &h->seq);
            break;
        case AVS2_STC_EXTENSION:
            ret = ff_avs2_decode_ext(h, bs, 1);
            break;
        case AVS2_STC_USER_DATA:
            ret = ff_avs2_decode_user_data(h, bs);
            break;
        default:
            av_log(h, AV_LOG_ERROR, "Extradata contain un-supported start code 0x%08x !!!\n",
                    unit->start_code);
            return AVERROR_INVALIDDATA;
        }

        AVS2_CHECK_RET(ret);
    }

    return 0;
}

int ff_avs2_decode_pic_header(AVS2Context *h, uint32_t stc, 
                              GetByteContext* bs, AVS2PicHeader *pic)
{
    int i, ret, buf_size;
    AVS2SeqHeader *seq = &h->seq;
    GetBitContext _gb, *gb = &_gb;
    
    uint8_t *rm_pseudo_buffer = av_mallocz(bs->buffer_end - bs->buffer_start);
    if (!rm_pseudo_buffer)
        goto error;
    
    buf_size = ff_avs2_remove_pseudo_code(rm_pseudo_buffer, bs->buffer, bs->buffer_end - bs->buffer_start);
    
    init_get_bits8(gb, rm_pseudo_buffer, buf_size);

    ff_avs2_set_default_pic_header(&h->seq, &h->pic, stc == AVS2_STC_INTRA_PIC);
    pic->bbv_delay = get_bits_long(gb, 32);

    if (pic->b_intra) {
        pic->b_time_code = get_bits1(gb);
        if (pic->b_time_code) {
            skip_bits1(gb);
            pic->time_code_hh = get_bits(gb, 5);
            pic->time_code_mm = get_bits(gb, 6);
            pic->time_code_ss = get_bits(gb, 6);
            pic->time_code_ff = get_bits(gb, 6);
        }
        if (seq->b_disable_scene_pic == 0) {
            pic->b_scene_pic = get_bits1(gb);
            if (pic->b_scene_pic) {
                pic->b_scene_pic_output = get_bits1(gb);
            }
        }
    } else {
        pic->pic_coding_type = get_bits(gb, 2);
        if (seq->b_disable_scene_pic == 0) {
            if (pic->pic_coding_type == AVS2_PCT_P) {
                pic->b_scene_pred = get_bits1(gb);
            } 
            if (pic->pic_coding_type != AVS2_PCT_B && pic->b_scene_pred == 0) {
                pic->b_scene_ref  = get_bits1(gb);
            }
        }
    }

    pic->doi = get_bits(gb, 8);
    if (seq->b_has_temporal_id) {
        pic->temporal_id = get_bits(gb, 3);
    }

    if (seq->b_low_delay == 0) {
        if (pic->b_intra) {
            if (pic->b_scene_pic == 0 || pic->b_scene_pic_output == 1) {
                pic->output_delay = get_ue_golomb(gb);
            }
        } else {
            pic->output_delay = get_ue_golomb(gb);
        }
    }

    pic->b_use_rcs = get_bits1(gb);
    if (pic->b_use_rcs) {
        pic->rcs_index = get_bits(gb, 5);
    } else {
        pic->rcs_index = seq->n_rcs;
        ret = ff_avs2_decode_rcs(gb, &seq->rcs[seq->n_rcs], h);
        AVS2_CHECK_RET(ret);
    }
    
    if (seq->b_low_delay) {
        pic->bbv_check_times = get_ue_golomb(gb);
    }

    pic->b_progressive_frame = get_bits1(gb);
    if (pic->b_progressive_frame == 0) {
        pic->b_picture_structure = get_bits1(gb);
    }
    pic->b_top_field_first = get_bits1(gb);
    pic->b_repeat_first_field = get_bits1(gb);
    if (seq->b_field_coding) {
        pic->b_top_field_picture = get_bits1(gb);
        skip_bits1(gb);
    }

    pic->b_fixed_qp = get_bits1(gb);
    pic->pic_qp = get_bits(gb, 7);

    if (!pic->b_intra) {
        if (!(pic->pic_coding_type == AVS2_PCT_B && pic->b_picture_structure)) {
            skip_bits1(gb);
        }
        pic->b_random_access = get_bits1(gb);
    }

    pic->b_disable_lf = get_bits1(gb);
    if (!pic->b_disable_lf) {
        pic->b_lf_param = get_bits1(gb);
        if (pic->b_lf_param) {
            pic->lf_alpha_offset = get_se_golomb(gb);
            pic->lf_beta_offset = get_se_golomb(gb);
        }
    }

    pic->b_no_chroma_quant_param = get_bits1(gb);
    if (pic->b_no_chroma_quant_param == 0) {
        pic->cb_quant_delta = get_se_golomb(gb);
        pic->cr_quant_delta = get_se_golomb(gb);
    }

    pic->b_enable_pic_wq = seq->b_enable_wq && get_bits1(gb);
    if (pic->b_enable_pic_wq) {
        pic->wq_data_index = get_bits(gb, 2);
        if (pic->wq_data_index == 1) {
            int8_t wq_param[6];
            skip_bits1(gb);
            pic->wq_param_index = get_bits(gb, 2);
            pic->wq_model = get_bits(gb, 2);
            if (pic->wq_param_index == 0){
                for (i = 0; i < 6; i++) {
                    wq_param[i] = avs2_default_wq_param[1][i];
                }
            }
            else if (pic->wq_param_index == 1 || pic->wq_param_index == 2) {
                int *wq_param_delta = pic->wq_param_delta[pic->wq_param_index - 1];
                for (i = 0; i < 6; i++) {
                    wq_param_delta[i] = get_se_golomb(gb);
                    wq_param[i] = wq_param_delta[i] + avs2_default_wq_param[pic->wq_param_index - 1][i];
                }
            }
            
            for (i = 0; i < 64; i++) 
                pic->wqm.m88[i] = wq_param[avs2_wq_model88[pic->wq_model][i]];
            for (i = 0; i < 16; i++)
                pic->wqm.m44[i] = wq_param[avs2_wq_model44[pic->wq_model][i]];
            
        } else if (pic->wq_data_index == 2) {
            ff_avs2_decode_wqm(gb, &pic->wqm);
        }
    }

    if (seq->b_alf) {
        ret = ff_avs2_decode_alf_param(gb, &h->pic);
        AVS2_CHECK_RET(ret);
    }

    align_get_bits(gb);

    av_log(h, AV_LOG_DEBUG, "<%s>, ra:%d, tid=%d, doi=%d, poi=%d \n",
            ff_avs2_get_pic_type_str(&h->pic), h->pic.b_random_access,
            h->pic.temporal_id, h->pic.doi,
            ff_avs2_get_pic_poi(&h->seq, &h->pic));
error:
    av_free(rm_pseudo_buffer);
    return 0;
}

int ff_avs2_decode_slice_header(AVS2Context *h, uint32_t stc, GetByteContext *bs) 
{
    AVS2SeqHeader *seq = &h->seq;
    AVS2PicHeader *pic = &h->pic;
    AVS2SlcHeader *slc = &h->slc;
    GetBitContext _gb, *gb = &_gb;
    
    int const MAX_SLICE_HEADER_BYTES = 5;
    int buf_size;

    uint8_t *rm_pseudo_buffer = av_mallocz(MAX_SLICE_HEADER_BYTES);
    if (!rm_pseudo_buffer)
        goto error;

    buf_size = ff_avs2_remove_pseudo_code(rm_pseudo_buffer, bs->buffer, MAX_SLICE_HEADER_BYTES);
    
    init_get_bits8(gb, rm_pseudo_buffer, buf_size);

    slc->lcu_y = get_bits(gb, 8);
    if (seq->height > (144 << seq->log2_lcu_size)) {
        slc->lcu_y += get_bits(gb, 3) << 7 ;
    }
    slc->lcu_x = get_bits(gb, 8);
    if (seq->width > (255 << seq->log2_lcu_size)) {
        slc->lcu_x += get_bits(gb, 2) << 8;
    }
    if (!pic->b_fixed_qp) {
        slc->b_fixed_qp = get_bits1(gb);
        slc->slice_qp = get_bits(gb, 7);
    } else {
        slc->b_fixed_qp = 1;
        slc->slice_qp = pic->pic_qp;
    }
    if (seq->b_sao) {
        slc->b_sao[0] = get_bits1(gb);
        slc->b_sao[1] = get_bits1(gb);
        slc->b_sao[2] = get_bits1(gb);
    }

    align_get_bits(gb);     // aec_byte_alignment_bit
    slc->aec_byte_offset = get_bits_count(gb) >> 3;

    av_log(h, AV_LOG_TRACE, "slice[%d, %d]\n", slc->lcu_x, slc->lcu_y);
    
error:
    av_free(rm_pseudo_buffer);
    return 0;
}