/*
 * AVS (Chinese GY/T 257.1—2012) HW decode acceleration through VA-API
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
#include "hwconfig.h"
#include "vaapi_decode.h"
#include "cavs.h"

/**
 * @file
 * This file implements the glue code between FFmpeg's and VA-API's
 * structures for AVS (Chinese GY/T 257.1—2012) decoding.
 */

static int vaapi_avs_pic_type_cvt(int pict_type) 
{
    switch (pict_type)
    {
    case AV_PICTURE_TYPE_I: return VA_AVS_I_IMG;
    case AV_PICTURE_TYPE_P: return VA_AVS_P_IMG;
    case AV_PICTURE_TYPE_B: return VA_AVS_B_IMG;
    default:                return VA_AVS_I_IMG;
    }
}

static void vaapi_avs_fill_pic(VAPictureAVS *va_pic, const AVSFrame *frame)
{
    va_pic->surface_id = ff_vaapi_get_surface_id(frame->f);
    va_pic->poc = frame->poc / 2;
}

/** Initialize and start decoding a frame with VA API. */
static int vaapi_avs_start_frame(AVCodecContext         *avctx,
                                av_unused const uint8_t *buffer,
                                av_unused uint32_t       size)
{
    int i, err;
    AVSContext   *h   = avctx->priv_data;
    VAPictureParameterBufferAVS pic_param = {};
    VAAPIDecodePicture *vapic = h->cur.hwaccel_picture_private;
    vapic->output_surface = ff_vaapi_get_surface_id(h->cur.f);

    pic_param = (VAPictureParameterBufferAVS) {
        .width = h->width,
        .height = h->height,
        .picture_type = vaapi_avs_pic_type_cvt(h->cur.f->pict_type),
        .progressive_seq_flag = h->progressive_seq,
        .progressive_frame_flag = h->progressive_frame,
        .picture_structure_flag = h->pic_structure,
        .fixed_pic_qp_flag = h->qp_fixed,
        .picture_qp = h->qp,
        .loop_filter_disable_flag = h->loop_filter_disable,
        .alpha_c_offset = h->alpha_offset,
        .beta_offset = h->beta_offset,
        .skip_mode_flag_flag = h->skip_mode_flag,
        .picture_reference_flag = h->ref_flag,
    };

    if (h->profile == 0x48) {
        pic_param.guangdian_fields.guangdian_flag = 1;
        pic_param.guangdian_fields.aec_flag = h->aec_flag;
        pic_param.guangdian_fields.weight_quant_flag = h->weight_quant_flag;
        pic_param.guangdian_fields.pb_field_enhanced_flag = h->pb_field_enhanced_flag;
        pic_param.guangdian_fields.chroma_quant_param_delta_cb = h->chroma_quant_param_delta_cb;
        pic_param.guangdian_fields.chroma_quant_param_delta_cr = h->chroma_quant_param_delta_cr;
        memcpy(pic_param.guangdian_fields.wqm_8x8, h->wqm_8x8, 64);
    }

    vaapi_avs_fill_pic(&pic_param.curr_pic, &h->cur);
    for (i = 0; i < 2; i++) {
        vaapi_avs_fill_pic(&pic_param.ref_list[i], &h->DPB[i]);
    }

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
static int vaapi_avs_end_frame(AVCodecContext *avctx)
{
    AVSContext *h = avctx->priv_data;
    VAAPIDecodePicture *vapic = h->cur.hwaccel_picture_private;
    return ff_vaapi_decode_issue(avctx, vapic);
}

/** Decode the given H.264 slice with VA API. */
static int vaapi_avs_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    int err;
    AVSContext *h = avctx->priv_data;
    VAAPIDecodePicture *vapic = h->cur.hwaccel_picture_private;
    VASliceParameterBufferAVS slice_param;
    slice_param = (VASliceParameterBufferAVS) {
        .slice_data_size        = size,
        .slice_data_offset      = 0,
        .slice_data_flag        = VA_SLICE_DATA_FLAG_ALL,
        .mb_data_bit_offset     = get_bits_count(&h->gb),
        .slice_vertical_pos     = h->stc,
        .fixed_slice_qp_flag    = h->qp_fixed,
        .slice_qp               = h->qp,
        .slice_weight_pred_flag = h->slice_weight_pred_flag,
        .mb_weight_pred_flag    = h->mb_weight_pred_flag,
    };

    *((uint32_t *)slice_param.luma_scale) = *((uint32_t *)h->luma_scale);
    *((uint32_t *)slice_param.luma_shift) = *((uint32_t *)h->luma_shift);
    *((uint32_t *)slice_param.chroma_scale) = *((uint32_t *)h->chroma_scale);
    *((uint32_t *)slice_param.chroma_shift) = *((uint32_t *)h->chroma_shift);

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

const FFHWAccel ff_cavs_vaapi_hwaccel = {
    .p.name                 = "cavs_vaapi",
    .p.type                 = AVMEDIA_TYPE_VIDEO,
    .p.id                   = AV_CODEC_ID_CAVS,
    .p.pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_avs_start_frame,
    .end_frame            = &vaapi_avs_end_frame,
    .decode_slice         = &vaapi_avs_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .frame_params         = &ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
