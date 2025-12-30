/*
 * AVS2 (Chinese GY/T 299.1-2016) HW decode acceleration through VA API
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

#include "hwaccel_internal.h"
#include "vaapi_decode.h"
#include "internal.h"
#include "avs2dec.h"

/**
 * @file
 * This file implements the glue code between FFmpeg's and VA API's
 * structures for AVS2 (Chinese GY/T 299.1-2016) decoding.
 */

static int vaapi_avs2_pic_type_cvt(int avs2_pic_type) 
{
    switch (avs2_pic_type)
    {
    case AVS2_PIC_I:    return VA_AVS2_I_IMG;
    case AVS2_PIC_P:    return VA_AVS2_P_IMG;
    case AVS2_PIC_B:    return VA_AVS2_B_IMG;
    case AVS2_PIC_F:    return VA_AVS2_F_IMG;
    case AVS2_PIC_S:    return VA_AVS2_S_IMG;
    case AVS2_PIC_G:    return VA_AVS2_G_IMG;
    case AVS2_PIC_GB:   return VA_AVS2_GB_IMG;
    default:            return VA_AVS2_I_IMG;
    }

}

static void vaapi_avs2_init_pic(VAPictureAVS2 *va_pic)
{
    va_pic->surface_id = VA_INVALID_SURFACE;
    va_pic->doi = -1;
    va_pic->poi = -1;
    va_pic->num_ref = 0;
}

static void vaapi_avs2_fill_pic(VAPictureAVS2 *va_pic, const AVS2Frame *frame)
{
    int i;
    va_pic->surface_id = ff_vaapi_get_surface_id(frame->frame);
    va_pic->doi = frame->pic_header.doi;
    va_pic->poi = frame->poi;
    va_pic->num_ref = frame->n_ref;
    for (i = 0; i < frame->n_ref; i++) {
        va_pic->ref_doi[i] = frame->ref_doi[i];
        va_pic->ref_poi[i] = frame->ref_poi[i];
    }
}

/** Initialize and start decoding a frame with VA API. */
static int vaapi_avs2_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    int i, err;
    AVS2Frame *ref_frame;
    AVS2Context   *h   = avctx->priv_data;
    AVS2SeqHeader *seq = &h->seq;
    AVS2PicHeader *pic = &h->pic;
    
    VAPictureParameterBufferAVS2 pic_param;

    VAAPIDecodePicture *vapic = h->curr_frame->hwaccel_picture_private;
    vapic->output_surface = ff_vaapi_get_surface_id(h->curr_frame->frame);

    //@see avs2_dec_gen_pic_param() in avs2_dec_pic.c
    pic_param = (VAPictureParameterBufferAVS2) {
        .width = seq->width,
        .height = seq->height,

        .log2_lcu_size_minus4 = seq->log2_lcu_size - 4,
        .chroma_format = seq->chroma_format,
        .output_bit_depth_minus8 = seq->output_bit_depth - 8,
        .weighted_skip_enable = seq->b_weighted_skip,
        .multi_hypothesis_skip_enable = seq->b_multi_hypothesis_skip,
        .nonsquare_intra_prediction_enable = seq->b_nsip,
        .dph_enable = seq->b_dual_hypothesis_prediction,
        .encoding_bit_depth_minus8 = seq->sample_bit_depth - 8,
        .field_coded_sequence = seq->b_field_coding,
        .pmvr_enable = seq->b_pmvr,
        .nonsquare_quadtree_transform_enable = seq->b_nsqt,
        .inter_amp_enable = seq->b_amp,
        .secondary_transform_enable_flag = seq->b_2nd_transform,
        .fixed_pic_qp = pic->b_fixed_qp,
        .pic_qp = pic->pic_qp,
        .picture_structure = pic->b_picture_structure,
        .top_field_picture_flag = pic->b_top_field_picture,
        .scene_picture_disable = seq->b_disable_scene_pic,
        .scene_reference_enable = pic->b_intra ? 0 : pic->b_scene_ref,
        .pic_type = vaapi_avs2_pic_type_cvt(h->curr_frame->pic_type),

        .lf_cross_slice_enable_flag = seq->b_cross_slice_loop_filter,
        .lf_pic_dbk_disable_flag = pic->b_disable_lf,
        .sao_enable = seq->b_sao,
        .alf_enable = seq->b_alf,
        .alpha_c_offset = pic->lf_alpha_offset,
        .beta_offset = pic->lf_beta_offset,
        .pic_alf_on_Y = pic->b_alf_enable[0],
        .pic_alf_on_U = pic->b_alf_enable[1],
        .pic_alf_on_V = pic->b_alf_enable[2],
        .pic_weight_quant_enable = pic->b_enable_pic_wq,
        .pic_weight_quant_data_index = pic->wq_data_index,
        .chroma_quant_param_delta_cb = pic->cb_quant_delta,
        .chroma_quant_param_delta_cr = pic->cr_quant_delta,

        .non_ref_flag = !h->curr_frame->b_ref,
        .num_of_ref = h->curr_frame->n_ref,
    };

    vaapi_avs2_fill_pic(&pic_param.CurrPic, h->curr_frame);
    for (i = 0; i < VA_AVS2_MAX_REF_COUNT; i++) {
        vaapi_avs2_init_pic(&pic_param.ref_list[i]);
    }
    for (i = 0; i < h->curr_frame->n_ref; i++) {
        ref_frame = ff_avs2_dpb_get_frame_by_doi(h, h->curr_frame->ref_doi[i]);
        if (!ref_frame) {
            av_log(avctx, AV_LOG_ERROR, "Can't get ref frame with doi=%d in dpb, "
                    "curr_doi=%d !!!\n", h->curr_frame->ref_doi[i], pic->doi);
            return AVERROR_INVALIDDATA;
        }
        vaapi_avs2_fill_pic(&pic_param.ref_list[i], ref_frame);
    }
    if(pic->wq_data_index == 0){
        memcpy(pic_param.wq_mat, seq->wqm.m44, 16);
        memcpy(pic_param.wq_mat + 16, seq->wqm.m88, 64);
    }
    else{
    memcpy(pic_param.wq_mat, pic->wqm.m44, 16);
    memcpy(pic_param.wq_mat + 16, pic->wqm.m88, 64);
    }
    memcpy(pic_param.alf_coeff[0], pic->alf_coeff[0], sizeof(pic_param.alf_coeff));

    err = ff_vaapi_decode_make_param_buffer(avctx, vapic,
                                            VAPictureParameterBufferType,
                                            &pic_param, sizeof(pic_param));
    if (err < 0)
        goto fail;

    return 0;
fail:
    ff_vaapi_decode_cancel(avctx, vapic);
    return err;
}

/** End a hardware decoding based frame. */
static int vaapi_avs2_end_frame(AVCodecContext *avctx)
{
    AVS2Context *h = avctx->priv_data;
    VAAPIDecodePicture *vapic = h->curr_frame->hwaccel_picture_private;
    return ff_vaapi_decode_issue(avctx, vapic);
}

/** Decode the given H.264 slice with VA API. */
static int vaapi_avs2_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    int err;
    AVS2Context *h = avctx->priv_data;
    AVS2SlcHeader *slc = &h->slc;
    VAAPIDecodePicture *vapic = h->curr_frame->hwaccel_picture_private;
    
    VASliceParameterBufferAVS2 slice_param;
    slice_param = (VASliceParameterBufferAVS2) {
        .slice_data_size        = size,
        .slice_data_offset      = 0,
        .slice_data_flag        = VA_SLICE_DATA_FLAG_ALL,
        .lcu_start_x            = slc->lcu_x,
        .lcu_start_y            = slc->lcu_y,

        .fixed_slice_qp         = slc->b_fixed_qp,
        .slice_qp               = slc->slice_qp,
        .slice_sao_enable_Y     = slc->b_sao[0],
        .slice_sao_enable_U     = slc->b_sao[1],
        .slice_sao_enable_V     = slc->b_sao[2],

        .vlc_byte_offset        = slc->aec_byte_offset & 0xf,
    };

    err = ff_vaapi_decode_make_slice_buffer(avctx, vapic,
                                            &slice_param, sizeof(slice_param),
                                            buffer, size);
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, vapic);
    return err;
}

const FFHWAccel ff_avs2_vaapi_hwaccel = {
    .p.name                 = "avs2_vaapi",
    .p.type                 = AVMEDIA_TYPE_VIDEO,
    .p.id                   = AV_CODEC_ID_AVS2,
    .p.pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_avs2_start_frame,
    .end_frame            = &vaapi_avs2_end_frame,
    .decode_slice         = &vaapi_avs2_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .frame_params         = &ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
