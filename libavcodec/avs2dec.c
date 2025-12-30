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
 * @file avs2dec.c
 * @author JianfengZheng <jianfeng.zheng@mthreads.com>
 * @brief Chinese AVS2-Video (GY/T 299.1-2016 or IEEE 1857.4-2018) decoder
 */

#include <ctype.h>
#include "config_components.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "hwaccel_internal.h"
#include "codec_internal.h"
#include "mpeg12data.h"
#include "hwconfig.h"
#include "profiles.h"
#include "decode.h"
#include "avs2dec.h"


static void ff_avs2_dpb_trace(AVS2Context *h, const char* hint)
{
    int i, n = 0;
    av_log(h, AV_LOG_TRACE, "[DPB Trace] %s\n", hint);
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        AVS2Frame* iter = &h->DPB[i];
        if (iter && IS_DPB_FRAME_INUSE(iter)) {
            av_log(h, AV_LOG_TRACE, "   #%d: doi=%d, poi=%d, marks=%x\n",
                    i, iter->pic_header.doi, iter->poi, iter->dpb_marks);
            ++n;
        }
    }
    if (n==0) {
        av_log(h, AV_LOG_TRACE, "   #-: none\n");
    }
}

static int ff_avs2_dpb_init(AVS2Context *h) 
{
    int i;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        h->DPB[i].frame = av_frame_alloc();
        if (!h->DPB[i].frame)
            return AVERROR(ENOMEM);
        h->DPB[i].dpb_marks = AVS2_DPB_MARK_NULL;
    }
    return 0;
}

/**
 * @brief remove frame out of dpb
 * @param frame one of frame in h->DPB[]
 */
static inline void ff_avs2_dpb_remove_frame(AVS2Context *h, AVS2Frame *frame)
{
    /* frame->frame can be NULL if context init failed */
    if (!frame->frame || !frame->frame->buf[0])
        return;

    av_buffer_unref(&frame->hwaccel_priv_buf);
    frame->hwaccel_picture_private = NULL;

    av_frame_unref(frame->frame);
    frame->dpb_marks = AVS2_DPB_MARK_NULL;
}

static void ff_avs2_dpb_uninit(AVS2Context *h)
{
    int i;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        ff_avs2_dpb_remove_frame(h, &h->DPB[i]);
        av_frame_free(&h->DPB[i].frame);
    }
}

static void ff_avs2_dpb_output_frame(AVS2Context *h, AVFrame *dst_frame, int *got_frame)
{
    int i;
    int min_poi = INT_MAX;
    AVS2Frame *out_frame = NULL;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        AVS2Frame* iter = &h->DPB[i];
        if (IS_DPB_FRAME_OUTPUTABLE(iter)) {
            if (iter->poi < min_poi) {
                out_frame = iter;
                min_poi = iter->poi;
            }
        }
    }
    if (out_frame) {
        *got_frame = 1;
        av_frame_ref(dst_frame, out_frame->frame);
        out_frame->dpb_marks |= AVS2_DPB_MARK_OUTPUTED;
        av_log(h, AV_LOG_TRACE, "[DPB Trace] output poi=%d\n", min_poi);
    } else {
        *got_frame = 0;
    }
    return;
}

AVS2Frame* ff_avs2_dpb_get_frame_by_doi(AVS2Context *h, int doi)
{
    int i;
    AVS2Frame* iter;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        iter = &h->DPB[i];
        if (IS_DPB_FRAME_INUSE(iter) && iter->pic_header.doi == doi) {
            return iter;
        }
    }
    return NULL;
}

static inline AVS2Frame* ff_avs2_dpb_get_unused_frame(AVS2Context *h)
{
    int i;
    AVS2Frame* iter;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        iter = &h->DPB[i];
        if (IS_DPB_FRAME_UNUSED(iter))
            return iter;
    }
    return NULL;
}

static int ff_avs2_dpb_get_current_frame(AVS2Context *h)
{
    int i, ret;
    AVS2PicHeader *pic = &h->pic;
    AVS2RefCfgSet *rcs = &h->seq.rcs[pic->rcs_index];
    h->curr_frame = ff_avs2_dpb_get_unused_frame(h);
    if (!h->curr_frame) {
        av_log(h, AV_LOG_ERROR, "Can't get unused frame buffer for decoding !!!\n");
        return AVERROR(ENOBUFS);
    }
    
    h->curr_frame->dpb_marks |= AVS2_DPB_MARK_USED;
    memcpy(&h->curr_frame->pic_header, &h->pic, sizeof(AVS2PicHeader));
    h->curr_frame->pic_type = ff_avs2_get_pic_type(&h->pic);
    h->curr_frame->poi = ff_avs2_get_pic_poi(&h->seq, &h->pic);
    h->curr_frame->n_slice = 0;

    ret = ff_get_buffer(h->avctx, h->curr_frame->frame, AV_GET_BUFFER_FLAG_REF);
    AVS2_CHECK_RET(ret);

    if (h->avctx->hwaccel) {
        const FFHWAccel *hwaccel = ffhwaccel(h->avctx->hwaccel);
        av_assert0(!h->curr_frame->hwaccel_picture_private);
        if (hwaccel->frame_priv_data_size) {
            h->curr_frame->hwaccel_priv_buf = av_buffer_allocz(hwaccel->frame_priv_data_size);
            if (!h->curr_frame->hwaccel_priv_buf)
                return AVERROR(ENOMEM);
            h->curr_frame->hwaccel_picture_private = h->curr_frame->hwaccel_priv_buf->data;
        }
    }

    h->curr_frame->b_ref = rcs->b_ref_by_others;
    h->curr_frame->n_ref = rcs->n_ref;
    for (i = 0; i < rcs->n_ref; i++) {
        uint8_t ref_doi = pic->doi - rcs->ref_delta_doi[i];
        AVS2Frame* ref_frame = ff_avs2_dpb_get_frame_by_doi(h, ref_doi);
        if (!ref_frame) {
            av_log(h, AV_LOG_ERROR, "Can't get ref frame with doi=%d in dpb, "
                    "curr_doi=%d !!!\n", ref_doi, pic->doi);
            return AVERROR_INVALIDDATA;
        }
        h->curr_frame->ref_doi[i] = ref_doi;
        h->curr_frame->ref_poi[i] = ref_frame->poi;
    }

    return 0;
}

/**
 * @brief Chapter 9.2.4 of GY/T 299.1-2016
 * Update dpb marks for all buffered dpb frames. After update, dpb frames
 * could be output or removed from dpb.
 */
static void ff_avs2_dpb_marks_update(AVS2Context *h)
{
    int i;
    AVS2Frame* iter;
    AVS2Frame* curr = h->curr_frame;
    AVS2PicHeader *pic = NULL;
    AVS2RefCfgSet *rcs = NULL;

    if (!h->curr_frame)
        return;
    pic = &curr->pic_header;
    rcs = &h->seq.rcs[pic->rcs_index];

    /**
     * mark unref pictures
     */
    // if (curr->pic_type == AVS2_PIC_G || curr->pic_type == AVS2_PIC_GB) {
    //     for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
    //         iter = &h->DPB[i];
    //         if (IS_DPB_FRAME_INUSE(iter)) {
    //             if (iter->pic_type == AVS2_PIC_G || iter->pic_type == AVS2_PIC_GB) {
    //                 iter->dpb_marks |= AVS2_DPB_MARK_UNREF;
    //             }
    //         }
    //     }
    // }
    
    for (i = 0; i < rcs->n_rm; i++) {
        uint8_t rm_doi = pic->doi - rcs->rm_delta_doi[i];
        iter = ff_avs2_dpb_get_frame_by_doi(h, rm_doi);
        if (!iter) {
            if (rcs->n_rm == 1 && rcs->rm_delta_doi[i] == 1) {
                av_log(h, AV_LOG_TRACE, "[DPB Trace] Sliding window DPB update.\n");
            } else {
                av_log(h, AV_LOG_WARNING, "Can't get ref frame with doi=%d in dpb, "
                    "curr_doi=%d !!!\n", rm_doi, pic->doi);
            }
            continue;
        }
        //if (!h->seq.b_has_temporal_id || iter->pic_header.temporal_id > pic->temporal_id) {
            iter->dpb_marks |= AVS2_DPB_MARK_UNREF;
        //}        
    }

    /**
     * mark current picture
     */
    if (curr->pic_type == AVS2_PIC_GB) {
        curr->dpb_marks |= AVS2_DPB_MARK_REF;
        curr->dpb_marks |= AVS2_DPB_MARK_OUTPUTED;
    } else if (rcs->b_ref_by_others) {
        curr->dpb_marks |= AVS2_DPB_MARK_REF;
    } else {
        curr->dpb_marks |= AVS2_DPB_MARK_UNREF;
    }

    /**
     * mark outputable pictures
     */
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        iter = &h->DPB[i];
        if (IS_DPB_FRAME_UNUSED(iter) || iter->pic_type == AVS2_PIC_GB)
            continue;
        if ((uint8_t)(iter->pic_header.doi + iter->pic_header.output_delay) <= (uint8_t)pic->doi) {
            iter->dpb_marks |= AVS2_DPB_MARK_OUTPUTABLE;
        }
    }
}

static void ff_avs2_dpb_mark_eos(AVS2Context *h)
{
    int i;
    AVS2Frame* iter;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        iter = &h->DPB[i];
        if (IS_DPB_FRAME_UNUSED(iter))
            continue;
        iter->dpb_marks |= AVS2_DPB_MARK_UNREF;
        iter->dpb_marks |= AVS2_DPB_MARK_OUTPUTABLE;
    }
}

static void ff_avs2_dpb_remove_all_removable(AVS2Context *h) 
{
    int i;
    AVS2Frame* iter;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++) {
        iter = &h->DPB[i];
        if (IS_DPB_FRAME_REMOVABLE(iter)) {
            ff_avs2_dpb_remove_frame(h, iter);
        }
    }
}

static int ff_avs2_get_pixel_format(AVCodecContext *avctx)
{
    AVS2Context *h      = avctx->priv_data;
    AVS2SeqHeader *seq  = &h->seq;

    int ret;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
#define HWACCEL_MAX (CONFIG_AVS2_VAAPI_HWACCEL)
    enum AVPixelFormat pix_fmts[HWACCEL_MAX + 2], *fmtp = pix_fmts;

    if (seq->output_bit_depth == 10) {
        pix_fmt = AV_PIX_FMT_YUV420P10;
    }

    av_log(avctx, AV_LOG_DEBUG, "AVS2 decode get format: %s.\n",
           av_get_pix_fmt_name(pix_fmt));
    h->pix_fmt = pix_fmt;

    switch (h->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV420P10:
#if CONFIG_AVS2_VAAPI_HWACCEL
        *fmtp++ = AV_PIX_FMT_VAAPI;
#endif
        break;
    }

    *fmtp++ = h->pix_fmt;
    *fmtp = AV_PIX_FMT_NONE;

    ret = ff_get_format(avctx, pix_fmts);
    if (ret < 0)
        return ret;

    avctx->pix_fmt = ret;
    if (!avctx->hwaccel) {
        if (avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "Your platform doesn't support hardware"
                    " accelerated AVS2 decoding. Pick one GPU from MooreThreads"
                    "(https://www.mthreads.com/). All their GPU support AVS2.\n");
            return AVERROR(ENOTSUP);
        } else {
            //TODO: Native decoder has not been supported yet. Remove this after implementation.
            av_log(avctx, AV_LOG_WARNING, "Your platform doesn't support software"
                        " AVS2 decoding, but hardware accelerated AVS2 decoding is"
                        " available if MT GPU is installed..\n");
        }
    }

    return 0;
}

static void ff_avs2_set_context_with_seq_header(AVCodecContext *avctx, AVS2SeqHeader* seq)
{
    avctx->coded_width      = seq->width;
    avctx->coded_height     = seq->height;
    avctx->width            = seq->width;
    avctx->height           = seq->height;
    avctx->has_b_frames     = !seq->b_low_delay;
    avctx->profile          = seq->profile_id;
    avctx->level            = seq->level_id;
    avctx->framerate        = ff_avs2_frame_rate_c2q(seq->frame_rate_code);
    ff_set_sar(avctx, ff_avs2_get_sar(seq));

    //TODO: set color properties
}

static av_cold int ff_avs2_decode_init(AVCodecContext *avctx)
{
    int ret;
    AVS2Context *h  = avctx->priv_data;
    h->avctx        = avctx;
    h->pix_fmt      = AV_PIX_FMT_NONE;

    ret = ff_avs2_dpb_init(h);
    AVS2_CHECK_RET(ret);

    if (!avctx->internal->is_copy) {
        if (avctx->extradata_size > 0 && avctx->extradata) {
            ret = ff_avs2_decode_extradata(h, avctx->extradata, avctx->extradata_size, &h->seq);
            AVS2_CHECK_RET(ret);

            ff_avs2_set_context_with_seq_header(avctx, &h->seq);
        }
    }

    return 0;
}

static av_cold int ff_avs2_decode_end(AVCodecContext *avctx)
{
    AVS2Context *h  = avctx->priv_data;
    ff_avs2_packet_uninit(&h->pkt_split);
    ff_avs2_dpb_uninit(h);
    return 0;
}

static int ff_avs2_decode_frame_data(AVS2Context *h, const uint8_t *data, int size)
{
    AVCodecContext *const avctx = h->avctx;
    int ret = 0;
    int i_unit = 0;
    int b_got_pic_hdr = 0;

    ret = ff_avs2_packet_split(&h->pkt_split, data, size, h);
    AVS2_CHECK_RET(ret);

    for (i_unit = 0; i_unit < h->pkt_split.nb_units; i_unit++) {
        AVS2EsUnit* unit = &h->pkt_split.units[i_unit];
        const uint8_t *unit_data = data + unit->data_start;
        const int unit_size = unit->data_len;
        GetByteContext _bs, *bs=&_bs;
        bytestream2_init(bs, unit_data, unit_size);

        switch (unit->start_code)
        {
        case AVS2_STC_SEQ_HEADER:
            if (b_got_pic_hdr) {
                av_log(h, AV_LOG_ERROR, "Sequence header should come before picture header !!!\n");
                return AVERROR_INVALIDDATA;
            }
            ret = ff_avs2_decode_seq_header(h, bs, &h->seq);
            AVS2_CHECK_RET(ret);

            if (!h->b_got_seq) {
                ff_avs2_set_context_with_seq_header(avctx, &h->seq);
                h->b_got_seq = 1;
            }

            if (h->pix_fmt == AV_PIX_FMT_NONE) {
                ret = ff_avs2_get_pixel_format(avctx);
                AVS2_CHECK_RET(ret);
            }
            if (FF_HW_HAS_CB(avctx, decode_params)) {
                ret = FF_HW_CALL(avctx, decode_params, unit->start_code, unit_data, unit_size);
                AVS2_CHECK_RET(ret);
            }
            break;
        case AVS2_STC_EXTENSION:
            ret = ff_avs2_decode_ext(h, bs, !b_got_pic_hdr);
            break;
        case AVS2_STC_USER_DATA:
            ret = ff_avs2_decode_user_data(h, bs);
            break;
        case AVS2_STC_INTRA_PIC:
        case AVS2_STC_INTER_PIC:
            if (!h->b_got_seq) {
                av_log(h, AV_LOG_ERROR, "No sequence header before picture header !!!\n");
                return AVERROR_INVALIDDATA;
            }
            ret = ff_avs2_decode_pic_header(h, unit->start_code, bs, &h->pic);
            AVS2_CHECK_RET(ret);
            b_got_pic_hdr = 1;

            ff_avs2_dpb_trace(h, "start of pic");
            ret = ff_avs2_dpb_get_current_frame(h);
            AVS2_CHECK_RET(ret);

            if (avctx->hwaccel) {
                ret = FF_HW_CALL(avctx, start_frame, unit_data, unit_size);
                AVS2_CHECK_RET(ret);
            }
            break;
        case AVS2_STC_SEQ_END:
            ff_avs2_dpb_mark_eos(h);
            break;
        case AVS2_STC_VIDEO_EDIT:
            break;
        default:
            /**
             * Slice Data
             */
            if (!b_got_pic_hdr) {
                av_log(h, AV_LOG_ERROR, "No picture header before slice data !!!\n");
                return AVERROR_INVALIDDATA;
            }
            h->curr_frame->n_slice += 1;
            ret = ff_avs2_decode_slice_header(h, unit->start_code, bs);
            AVS2_CHECK_RET(ret);
            if (avctx->hwaccel) {
                ret = FF_HW_CALL(avctx, decode_slice, unit_data, unit_size);
                AVS2_CHECK_RET(ret);
            } else if (avctx->hw_device_ctx) {
                av_log(avctx, AV_LOG_ERROR, "Your platform doesn't suppport hardware"
                        " accelerated AVS2 decoding. Pick one GPU from MooreThreads"
                        "(https://www.mthreads.com/). All their GPU support AVS2.\n");
                return AVERROR(ENOTSUP);
            } else {
                //TODO: Native decoder has not been supported yet. Remove this after implementation.
                av_log(avctx, AV_LOG_WARNING, "Your platform doesn't support software"
                        " AVS2 decoding, but hardware accelerated AVS2 decoding is"
                        " available if MT GPU is installed.\n");
            }
            break;
        }

        AVS2_CHECK_RET(ret);
    }

    if (h->curr_frame && h->curr_frame->n_slice > 0 && avctx->hwaccel) {
        ret = FF_HW_SIMPLE_CALL(avctx, end_frame);
        AVS2_CHECK_RET(ret);
    }

    return size;
}
                    
static int ff_avs2_decode_frame(AVCodecContext *avctx, AVFrame *out_frame, 
                                int *got_output, AVPacket *pkt)
{
    int ret;
    size_t new_extradata_size;
    uint8_t *new_extradata;
    AVS2Context *h = avctx->priv_data;

    if (!pkt || pkt->size <= 0) {
        ff_avs2_dpb_mark_eos(h);
        ff_avs2_dpb_output_frame(h, out_frame, got_output);
        ff_avs2_dpb_remove_all_removable(h);
        ff_avs2_dpb_trace(h, "end of stream");
        return 0;
    }

    new_extradata = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                            &new_extradata_size);
    if (new_extradata && new_extradata_size > 0) {
        av_log(avctx, AV_LOG_DEBUG, "new_extradata found\n");
        ret = ff_avs2_decode_extradata(h, new_extradata, new_extradata_size, &h->seq);
        AVS2_CHECK_RET(ret);
    }

    ret = ff_avs2_decode_frame_data(h, pkt->data, pkt->size);

    ff_avs2_dpb_marks_update(h);
    ff_avs2_dpb_output_frame(h, out_frame, got_output);
    ff_avs2_dpb_remove_all_removable(h);
    ff_avs2_dpb_trace(h, "end of pic");
    h->curr_frame = NULL;

    AVS2_CHECK_RET(ret);
    return pkt->size;
}

static void ff_avs2_decode_flush(AVCodecContext *avctx)
{
    int i;
    AVS2Context *h = avctx->priv_data;

    h->curr_frame = NULL;
    for (i = 0; i < AVS2_MAX_DPB_COUNT; i++)
        ff_avs2_dpb_remove_frame(h, &h->DPB[i]);

    ff_avs2_dpb_trace(h, "decode flush");
}

static const AVClass avs2_class = {
    .class_name = "AVS2 video Decoder",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_avs2_decoder = {
    .p.name             = "avs2",
    CODEC_LONG_NAME("AVS2-Video; Chinese GY/T 299.1-2016 or GB/T 33475.2-2016; IEEE 1857.4-2018"),
    .p.type             = AVMEDIA_TYPE_VIDEO,
    .p.id               = AV_CODEC_ID_AVS2,
    .priv_data_size = sizeof(AVS2Context),
    .init               = ff_avs2_decode_init,
    .close              = ff_avs2_decode_end,
    FF_CODEC_DECODE_CB(ff_avs2_decode_frame),
    .flush              = ff_avs2_decode_flush,
    .p.capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .hw_configs         = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_AVS2_VAAPI_HWACCEL
                                HWACCEL_VAAPI(avs2),
#endif
                                NULL
                           },
    .p.priv_class       = &avs2_class,
    .p.profiles         = NULL_IF_CONFIG_SMALL(ff_avs2_profiles),
};