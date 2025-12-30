/*
 * Moore threads MUVID decoder
 * Copyright (c) 2025
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

#include "config_components.h"

#include "compat/musa/dynlink_loader.h"

#include "libavutil/buffer.h"
#include "libavutil/mathematics.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_musa_internal.h"
#include "libavutil/musa_check.h"
#include "libavutil/fifo.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "bsf.h"
#include "codec_internal.h"
#include "decode.h"
#include "hwconfig.h"
#include "mtdec.h"
#include "internal.h"

typedef struct MuvidContext
{
    AVClass *avclass;

    MUvideodecoder mudecoder;
    MUvideoparser muparser;

    /* This packet coincides with AVCodecInternal.in_pkt
     * and is not owned by us. */
    AVPacket *pkt;

    char *mu_gpu;
    int nb_surfaces;
    int drop_second_field;
    char *crop_expr;
    char *resize_expr;

    struct {
        int left;
        int top;
        int right;
        int bottom;
    } crop;

    struct {
        int width;
        int height;
    } resize;

    AVBufferRef *hwdevice;
    AVBufferRef *hwframe;

    AVFifo      *frame_queue;

    int deint_mode;
    int deint_mode_current;
    int64_t prev_pts;
    int progressive_sequence;

    int internal_error;
    int decoder_flushing;

    int *key_frame;

    musaVideoCodec codec_type;
    musaVideoChromaFormat chroma_format;

    MUVIDDECODECAPS caps8, caps10, caps12;

    MUVIDPARSERPARAMS muparseinfo;
    MUVIDEOFORMATEX *muparse_ext;

    MusaFunctions *mudl;
    MuvidFunctions *mvdl;
} MuvidContext;

typedef struct MuvidParsedFrame
{
    MUVIDPARSERDISPINFO dispinfo;
    int second_field;
    int is_deinterlacing;
} MuvidParsedFrame;

#define CHECK_MU(x) FF_MUSA_CHECK_DL(avctx, ctx->mudl, x)

// It recommends [2;4] range
#define MUVID_MAX_DISPLAY_DELAY (4)

// Actual pool size will be determined by parser.
#define MUVID_DEFAULT_NUM_SURFACES (MUVID_MAX_DISPLAY_DELAY + 1)

static int MUSAAPI muvid_handle_video_sequence(void *opaque, MUVIDEOFORMAT* format)
{
    AVCodecContext *avctx = opaque;
    MuvidContext *ctx = avctx->priv_data;
    AVHWFramesContext *hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;
    MUVIDDECODECAPS *caps = NULL;
    MUVIDDECODECREATEINFO muinfo;
    int surface_fmt;
    int fifo_size_inc;

    int old_width = avctx->width;
    int old_height = avctx->height;

    enum AVPixelFormat pix_fmts[3] = { AV_PIX_FMT_MUSA,
                                       AV_PIX_FMT_NONE,  // Will be updated below
                                       AV_PIX_FMT_NONE };

    av_log(avctx, AV_LOG_TRACE, "pfnSequenceCallback, progressive_sequence=%d\n", format->progressive_sequence);

    memset(&muinfo, 0, sizeof(muinfo));

    ctx->internal_error = 0;

    avctx->coded_width = muinfo.ulWidth = format->coded_width;
    avctx->coded_height = muinfo.ulHeight = format->coded_height;

    // apply cropping
    muinfo.display_area.left = format->display_area.left + ctx->crop.left;
    muinfo.display_area.top = format->display_area.top + ctx->crop.top;
    muinfo.display_area.right = format->display_area.right - ctx->crop.right;
    muinfo.display_area.bottom = format->display_area.bottom - ctx->crop.bottom;

    // width and height need to be set before calling ff_get_format
    if (ctx->resize_expr) {
        avctx->width = ctx->resize.width;
        avctx->height = ctx->resize.height;
    } else {
        avctx->width = muinfo.display_area.right - muinfo.display_area.left;
        avctx->height = muinfo.display_area.bottom - muinfo.display_area.top;
    }

    // target width/height need to be multiples of two
    muinfo.ulTargetWidth = avctx->width = (avctx->width + 1) & ~1;
    muinfo.ulTargetHeight = avctx->height = (avctx->height + 1) & ~1;

    // aspect ratio conversion, 1:1, depends on scaled resolution
    // muinfo.target_rect.left = 0;
    // muinfo.target_rect.top = 0;
    // muinfo.target_rect.right = muinfo.ulTargetWidth;
    // muinfo.target_rect.bottom = muinfo.ulTargetHeight;

    switch (format->bit_depth_luma_minus8) {
    case 0: // 8-bit
        pix_fmts[1] = AV_PIX_FMT_NV12;
        caps = &ctx->caps8;
        break;
    case 2: // 10-bit
        pix_fmts[1] = AV_PIX_FMT_P010;
        caps = &ctx->caps10;
        break;
    case 4: // 12-bit
        pix_fmts[1] = AV_PIX_FMT_P016;
        caps = &ctx->caps12;
        break;
    default:
        break;
    }

    if (!caps || !caps->bIsSupported) {
        av_log(avctx, AV_LOG_ERROR, "unsupported bit depth: %d\n",
               format->bit_depth_luma_minus8 + 8);
        ctx->internal_error = AVERROR(EINVAL);
        return 0;
    }

    surface_fmt = ff_get_format(avctx, pix_fmts);
    if (surface_fmt < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_get_format failed: %d\n", surface_fmt);
        ctx->internal_error = AVERROR(EINVAL);
        return 0;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Formats: Original: %s | HW: %s | SW: %s\n",
           av_get_pix_fmt_name(avctx->pix_fmt),
           av_get_pix_fmt_name(surface_fmt),
           av_get_pix_fmt_name(avctx->sw_pix_fmt));

    avctx->pix_fmt = surface_fmt;

    // Update our hwframe ctx, as the get_format callback might have refreshed it!
    if (avctx->hw_frames_ctx) {
        av_buffer_unref(&ctx->hwframe);

        ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->hwframe) {
            ctx->internal_error = AVERROR(ENOMEM);
            return 0;
        }

        hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;
    }

    ff_set_sar(avctx, av_div_q(
        (AVRational){ format->display_aspect_ratio.x, format->display_aspect_ratio.y },
        (AVRational){ avctx->width, avctx->height }));

    ctx->deint_mode_current = format->progressive_sequence
                              ? musaVideoDeinterlaceMode_Weave
                              : ctx->deint_mode;

    ctx->progressive_sequence = format->progressive_sequence;

    if (!format->progressive_sequence && ctx->deint_mode_current == musaVideoDeinterlaceMode_Weave)
        avctx->flags |= AV_CODEC_FLAG_INTERLACED_DCT;
    else
        avctx->flags &= ~AV_CODEC_FLAG_INTERLACED_DCT;

    if (format->video_signal_description.video_full_range_flag)
        avctx->color_range = AVCOL_RANGE_JPEG;
    else
        avctx->color_range = AVCOL_RANGE_MPEG;

    avctx->color_primaries = format->video_signal_description.color_primaries;
    avctx->color_trc = format->video_signal_description.transfer_characteristics;
    avctx->colorspace = format->video_signal_description.matrix_coefficients;

    if (format->bitrate)
        avctx->bit_rate = format->bitrate;

    if (format->frame_rate.numerator && format->frame_rate.denominator) {
        avctx->framerate.num = format->frame_rate.numerator;
        avctx->framerate.den = format->frame_rate.denominator;
    }

    if (ctx->mudecoder
            && avctx->coded_width == format->coded_width
            && avctx->coded_height == format->coded_height
            && avctx->width == old_width
            && avctx->height == old_height
            && ctx->chroma_format == format->chroma_format
            && ctx->codec_type == format->codec)
        return 1;

    if (ctx->mudecoder) {
        av_log(avctx, AV_LOG_TRACE, "Re-initializing decoder\n");
        ctx->internal_error = CHECK_MU(ctx->mvdl->muvidDestroyDecoder(ctx->mudecoder));
        if (ctx->internal_error < 0)
            return 0;
        ctx->mudecoder = NULL;
    }

    if (hwframe_ctx->pool && (
            hwframe_ctx->width < avctx->width ||
            hwframe_ctx->height < avctx->height ||
            hwframe_ctx->format != AV_PIX_FMT_MUSA ||
            hwframe_ctx->sw_format != avctx->sw_pix_fmt)) {
        av_log(avctx, AV_LOG_ERROR, "AVHWFramesContext is already initialized with incompatible parameters\n");
        av_log(avctx, AV_LOG_DEBUG, "width: %d <-> %d\n", hwframe_ctx->width, avctx->width);
        av_log(avctx, AV_LOG_DEBUG, "height: %d <-> %d\n", hwframe_ctx->height, avctx->height);
        av_log(avctx, AV_LOG_DEBUG, "format: %s <-> musa\n", av_get_pix_fmt_name(hwframe_ctx->format));
        av_log(avctx, AV_LOG_DEBUG, "sw_format: %s <-> %s\n",
               av_get_pix_fmt_name(hwframe_ctx->sw_format), av_get_pix_fmt_name(avctx->sw_pix_fmt));
        ctx->internal_error = AVERROR(EINVAL);
        return 0;
    }

    ctx->chroma_format = format->chroma_format;

    muinfo.CodecType = ctx->codec_type = format->codec;
    muinfo.ChromaFormat = format->chroma_format;

    switch (avctx->sw_pix_fmt) {
    case AV_PIX_FMT_NV12:
        muinfo.OutputFormat = musaVideoSurfaceFormat_NV12;
        break;
    case AV_PIX_FMT_P010:
    case AV_PIX_FMT_P016:
        muinfo.OutputFormat = musaVideoSurfaceFormat_P016;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
               av_get_pix_fmt_name(avctx->sw_pix_fmt));
        ctx->internal_error = AVERROR(EINVAL);
        return 0;
    }

    fifo_size_inc = ctx->nb_surfaces;
    ctx->nb_surfaces = FFMAX(ctx->nb_surfaces, format->min_num_decode_surfaces + 3) + fifo_size_inc;

    if (avctx->extra_hw_frames > 0)
        ctx->nb_surfaces += avctx->extra_hw_frames;

    fifo_size_inc = ctx->nb_surfaces - fifo_size_inc;
    if (fifo_size_inc > 0 && av_fifo_grow2(ctx->frame_queue, fifo_size_inc) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to grow frame queue on video sequence callback\n");
        ctx->internal_error = AVERROR(ENOMEM);
        return 0;
    }

    if (fifo_size_inc > 0 && av_reallocp_array(&ctx->key_frame, ctx->nb_surfaces, sizeof(int)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to grow key frame array on video sequence callback\n");
        ctx->internal_error = AVERROR(ENOMEM);
        return 0;
    }

    muinfo.ulNumDecodeSurfaces = ctx->nb_surfaces;
    muinfo.ulNumOutputSurfaces = ctx->nb_surfaces;
    muinfo.ulCreationFlags = musaVideoCreate_MUSA;
    muinfo.bitDepthMinus8 = format->bit_depth_luma_minus8;
    muinfo.DeinterlaceMode = ctx->deint_mode_current;

    if (ctx->deint_mode_current != musaVideoDeinterlaceMode_Weave && !ctx->drop_second_field)
        avctx->framerate = av_mul_q(avctx->framerate, (AVRational){2, 1});

    ctx->internal_error = CHECK_MU(ctx->mvdl->muvidCreateDecoder(&ctx->mudecoder, &muinfo));
    if (ctx->internal_error < 0)
        return 0;

    if (!hwframe_ctx->pool) {
        hwframe_ctx->format = AV_PIX_FMT_MUSA;
        hwframe_ctx->sw_format = avctx->sw_pix_fmt;
        hwframe_ctx->width = avctx->width;
        hwframe_ctx->height = avctx->height;

        if ((ctx->internal_error = av_hwframe_ctx_init(ctx->hwframe)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_hwframe_ctx_init failed\n");
            return 0;
        }
    }

    if(ctx->muparseinfo.ulMaxNumDecodeSurfaces != muinfo.ulNumDecodeSurfaces) {
        ctx->muparseinfo.ulMaxNumDecodeSurfaces = muinfo.ulNumDecodeSurfaces;
        return muinfo.ulNumDecodeSurfaces;
    }

    return 1;
}

static int MUSAAPI muvid_handle_picture_decode(void *opaque, MUVIDPICPARAMS* picparams)
{
    AVCodecContext *avctx = opaque;
    MuvidContext *ctx = avctx->priv_data;

    av_log(avctx, AV_LOG_TRACE, "pfnDecodePicture\n");

    if(picparams->intra_pic_flag)
        ctx->key_frame[picparams->CurrPicIdx] = picparams->intra_pic_flag;

    ctx->internal_error = CHECK_MU(ctx->mvdl->muvidDecodePicture(ctx->mudecoder, picparams));
    if (ctx->internal_error < 0)
        return 0;

    return 1;
}

static int MUSAAPI muvid_handle_picture_display(void *opaque, MUVIDPARSERDISPINFO* dispinfo)
{
    AVCodecContext *avctx = opaque;
    MuvidContext *ctx = avctx->priv_data;
    MuvidParsedFrame parsed_frame = { { 0 } };

    parsed_frame.dispinfo = *dispinfo;
    ctx->internal_error = 0;

    // For some reason, dispinfo->progressive_frame is sometimes wrong.
    parsed_frame.dispinfo.progressive_frame = ctx->progressive_sequence;

    if (ctx->deint_mode_current == musaVideoDeinterlaceMode_Weave) {
        av_fifo_write(ctx->frame_queue, &parsed_frame, 1);
    } else {
        parsed_frame.is_deinterlacing = 1;
        av_fifo_write(ctx->frame_queue, &parsed_frame, 1);
        if (!ctx->drop_second_field) {
            parsed_frame.second_field = 1;
            av_fifo_write(ctx->frame_queue, &parsed_frame, 1);
        }
    }

    return 1;
}

static int muvid_is_buffer_full(AVCodecContext *avctx)
{
    MuvidContext *ctx = avctx->priv_data;

    int delay = ctx->muparseinfo.ulMaxDisplayDelay;
    if (ctx->deint_mode != musaVideoDeinterlaceMode_Weave && !ctx->drop_second_field)
        delay *= 2;

    return av_fifo_can_read(ctx->frame_queue) + delay >= FFMIN(ctx->nb_surfaces, 5);
}

static int muvid_decode_packet(AVCodecContext *avctx, const AVPacket *avpkt)
{
    MuvidContext *ctx = avctx->priv_data;
    AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)ctx->hwdevice->data;
    AVMUSADeviceContext *device_hwctx = device_ctx->hwctx;
    MUcontext dummy, musa_ctx = device_hwctx->musa_ctx;
    MUVIDSOURCEDATAPACKET cupkt;
    int ret = 0, eret = 0, is_flush = ctx->decoder_flushing;

    av_log(avctx, AV_LOG_TRACE, "muvid_decode_packet\n");

    if (is_flush && avpkt && avpkt->size)
        return AVERROR_EOF;

    if (muvid_is_buffer_full(avctx) && avpkt && avpkt->size)
        return AVERROR(EAGAIN);

    ret = CHECK_MU(ctx->mudl->muCtxPushCurrent(musa_ctx));
    if (ret < 0) {
        return ret;
    }

    memset(&cupkt, 0, sizeof(cupkt));

    if (avpkt && avpkt->size) {
        cupkt.payload_size = avpkt->size;
        cupkt.payload = avpkt->data;

        if (avpkt->pts != AV_NOPTS_VALUE) {
            cupkt.flags = MUVID_PKT_TIMESTAMP;
            if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
                cupkt.timestamp = av_rescale_q(avpkt->pts, avctx->pkt_timebase, (AVRational){1, 10000000});
            else
                cupkt.timestamp = avpkt->pts;
        }
    } else {
        cupkt.flags = MUVID_PKT_ENDOFSTREAM;
        ctx->decoder_flushing = 1;
    }

    ret = CHECK_MU(ctx->mvdl->muvidParseVideoData(ctx->muparser, &cupkt));

    if (ret < 0)
        goto error;

    // muvidParseVideoData doesn't return an error just because stuff failed...
    if (ctx->internal_error) {
        av_log(avctx, AV_LOG_ERROR, "muvid decode callback error\n");
        ret = ctx->internal_error;
        goto error;
    }

error:
    eret = CHECK_MU(ctx->mudl->muCtxPopCurrent(&dummy));

    if (eret < 0)
        return eret;
    else if (ret < 0)
        return ret;
    else if (is_flush)
        return AVERROR_EOF;
    else
        return 0;
}

static int muvid_output_frame(AVCodecContext *avctx, AVFrame *frame)
{
    // AVHWFramesContext *hwctx = (AVHWFramesContext *)frame->hw_frames_ctx->data;
    MuvidContext *ctx = avctx->priv_data;
    AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)ctx->hwdevice->data;
    AVMUSADeviceContext *device_hwctx = device_ctx->hwctx;
    MUcontext dummy, musa_ctx = device_hwctx->musa_ctx;
    MuvidParsedFrame parsed_frame;
    MUdeviceptr mapped_frame = 0;
    int ret = 0, eret = 0;

    av_log(avctx, AV_LOG_TRACE, "muvid_output_frame\n");

    if (ctx->decoder_flushing) {
        ret = muvid_decode_packet(avctx, NULL);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    if (!muvid_is_buffer_full(avctx)) {
        AVPacket *const pkt = ctx->pkt;
        ret = ff_decode_get_packet(avctx, pkt);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;

        ret = muvid_decode_packet(avctx, pkt);
        av_packet_unref(pkt);
        // muvid_is_buffer_full() should avoid this.
        if (ret == AVERROR(EAGAIN))
            ret = AVERROR_EXTERNAL;
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    ret = CHECK_MU(ctx->mudl->muCtxPushCurrent(musa_ctx));
    if (ret < 0)
        return ret;

    if (av_fifo_read(ctx->frame_queue, &parsed_frame, 1) >= 0) {
        const AVPixFmtDescriptor *pixdesc;
        MUVIDPROCPARAMS params;
        unsigned int pitch = 0;
        int offset = 0;
        int i;

        memset(&params, 0, sizeof(params));
        params.progressive_frame = parsed_frame.dispinfo.progressive_frame;
        params.second_field = parsed_frame.second_field;
        params.top_field_first = parsed_frame.dispinfo.top_field_first;

        MUSAPROCPARAMS postprocParams = { 0 };
        postprocParams.flag = musaMapVideoFlag_Musa;
        postprocParams.width = avctx->width;
        postprocParams.height = avctx->height;
        postprocParams.format =
        avctx->sw_pix_fmt == AV_PIX_FMT_NV12 ? musaVideoSurfaceFormat_NV12 : musaVideoSurfaceFormat_P016;

        params.postProcParams = &postprocParams;

        ret = CHECK_MU(ctx->mvdl->muvidMapVideoFrame(ctx->mudecoder, parsed_frame.dispinfo.picture_index, &mapped_frame, &pitch, &params));
        if (ret < 0)
            goto error;

        MUSAMAPVIDEOTOMUSAPARAMS* out = &postprocParams.specificParams.toMusaParams;
        if (avctx->pix_fmt == AV_PIX_FMT_MUSA) {
            ret = av_hwframe_get_buffer(ctx->hwframe, frame, 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "av_hwframe_get_buffer failed\n");
                goto error;
            }

            ret = ff_decode_frame_props(avctx, frame);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "ff_decode_frame_props failed\n");
                goto error;
            }

            pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);

            for (i = 0; i < pixdesc->nb_components; i++) {
                int height = avctx->height >> (i ? pixdesc->log2_chroma_h : 0);
                MUSA_MEMCPY2D cpy = {
                    .srcMemoryType = MU_MEMORYTYPE_DEVICE,
                    .dstMemoryType = MU_MEMORYTYPE_DEVICE,
                    // .srcDevice     = mapped_frame,
                    .srcDevice     = (uint8_t*)out->dataBase + offset,
                    .dstDevice     = (MUdeviceptr)frame->data[i],
                    .srcPitch      = pitch,
                    .dstPitch      = frame->linesize[i],
                    .srcY          = offset,
                    .WidthInBytes  = FFMIN(pitch, frame->linesize[i]),
                    .Height        = height,
                };

                ret = CHECK_MU(ctx->mudl->muMemcpy2DAsync(&cpy, device_hwctx->stream));
                if (ret < 0)
                    goto error;

                offset += height;
            }
        } else if (avctx->pix_fmt == AV_PIX_FMT_NV12      ||
                   avctx->pix_fmt == AV_PIX_FMT_P010      ||
                   avctx->pix_fmt == AV_PIX_FMT_P016      ||
                   avctx->pix_fmt == AV_PIX_FMT_YUV444P   ||
                   avctx->pix_fmt == AV_PIX_FMT_YUV444P16) {
            unsigned int offset = 0;
            AVFrame *tmp_frame = av_frame_alloc();
            if (!tmp_frame) {
                av_log(avctx, AV_LOG_ERROR, "av_frame_alloc failed\n");
                ret = AVERROR(ENOMEM);
                goto error;
            }

            pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);

            tmp_frame->format        = AV_PIX_FMT_MUSA;
            tmp_frame->hw_frames_ctx = av_buffer_ref(ctx->hwframe);
            if (!tmp_frame->hw_frames_ctx) {
                ret = AVERROR(ENOMEM);
                av_frame_free(&tmp_frame);
                goto error;
            }

            tmp_frame->width         = avctx->width;
            tmp_frame->height        = avctx->height;

            /*
             * Note that the following logic would not work for three plane
             * YUV420 because the pitch value is different for the chroma
             * planes.
             */
            for (i = 0; i < pixdesc->nb_components; i++) {
                // tmp_frame->data[i]     = (uint8_t*)mapped_frame + offset;
                tmp_frame->data[i]     = (uint8_t*)out->dataBase + offset;
                tmp_frame->linesize[i] = pitch;
                offset += pitch * (avctx->height >> (i ? pixdesc->log2_chroma_h : 0));
            }

            ret = ff_get_buffer(avctx, frame, 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "ff_get_buffer failed\n");
                av_frame_free(&tmp_frame);
                goto error;
            }

            ret = av_hwframe_transfer_data(frame, tmp_frame, 0);
            if (ret) {
                av_log(avctx, AV_LOG_ERROR, "av_hwframe_transfer_data failed\n");
                av_frame_free(&tmp_frame);
                goto error;
            }
            av_frame_free(&tmp_frame);
        } else {
            ret = AVERROR_BUG;
            goto error;
        }

        if (ctx->key_frame[parsed_frame.dispinfo.picture_index])
            frame->flags |= AV_FRAME_FLAG_KEY;
        else
            frame->flags &= ~AV_FRAME_FLAG_KEY;
        ctx->key_frame[parsed_frame.dispinfo.picture_index] = 0;

        frame->width = avctx->width;
        frame->height = avctx->height;
        if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
            frame->pts = av_rescale_q(parsed_frame.dispinfo.timestamp, (AVRational){1, 10000000}, avctx->pkt_timebase);
        else
            frame->pts = parsed_frame.dispinfo.timestamp;

        if (parsed_frame.second_field) {
            if (ctx->prev_pts == INT64_MIN) {
                ctx->prev_pts = frame->pts;
                frame->pts += (avctx->pkt_timebase.den * avctx->framerate.den) / (avctx->pkt_timebase.num * avctx->framerate.num);
            } else {
                int pts_diff = (frame->pts - ctx->prev_pts) / 2;
                ctx->prev_pts = frame->pts;
                frame->pts += pts_diff;
            }
        }

        /* MUVIDs opaque reordering breaks the internal pkt logic.
         * So set pkt_pts and clear all the other pkt_ fields.
         */
        frame->duration = 0;
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pos = -1;
        frame->pkt_size = -1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        if (!parsed_frame.is_deinterlacing && !parsed_frame.dispinfo.progressive_frame)
            frame->flags |= AV_FRAME_FLAG_INTERLACED;

        if ((frame->flags & AV_FRAME_FLAG_INTERLACED) && parsed_frame.dispinfo.top_field_first)
            frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    } else if (ctx->decoder_flushing) {
        ret = AVERROR_EOF;
    } else {
        ret = AVERROR(EAGAIN);
    }

error:
    if (ret < 0)
        av_frame_unref(frame);

    if (mapped_frame)
        eret = CHECK_MU(ctx->mvdl->muvidUnmapVideoFrame(ctx->mudecoder, mapped_frame));

    eret = CHECK_MU(ctx->mudl->muCtxPopCurrent(&dummy));

    if (eret < 0)
        return eret;
    else
        return ret;
}

static av_cold int muvid_decode_end(AVCodecContext *avctx)
{
    MuvidContext *ctx = avctx->priv_data;
    AVHWDeviceContext *device_ctx = ctx->hwdevice ? (AVHWDeviceContext *)ctx->hwdevice->data : NULL;
    AVMUSADeviceContext *device_hwctx = device_ctx ? device_ctx->hwctx : NULL;
    MUcontext dummy, musa_ctx = device_hwctx ? device_hwctx->musa_ctx : NULL;

    av_fifo_freep2(&ctx->frame_queue);

    if (musa_ctx) {
        ctx->mudl->muCtxPushCurrent(musa_ctx);

        if (ctx->muparser)
            ctx->mvdl->muvidDestroyVideoParser(ctx->muparser);

        if (ctx->mudecoder)
            ctx->mvdl->muvidDestroyDecoder(ctx->mudecoder);

        ctx->mudl->muCtxPopCurrent(&dummy);
    }

    ctx->mudl = NULL;

    av_buffer_unref(&ctx->hwframe);
    av_buffer_unref(&ctx->hwdevice);

    av_freep(&ctx->key_frame);
    av_freep(&ctx->muparse_ext);

    muvid_free_functions(&ctx->mvdl);

    return 0;
}

static int muvid_test_capabilities(AVCodecContext *avctx,
                                   const MUVIDPARSERPARAMS *muparseinfo,
                                   int probed_width,
                                   int probed_height,
                                   int bit_depth)
{
    MuvidContext *ctx = avctx->priv_data;
    MUVIDDECODECAPS *caps;
    int res8 = 0, res10 = 0, res12 = 0;

    memset(&ctx->caps8, 0, sizeof(ctx->caps8));
    memset(&ctx->caps10, 0, sizeof(ctx->caps10));
    if (!ctx->mvdl->muvidGetDecoderCaps) {
        av_log(avctx, AV_LOG_WARNING, "Used Moore threads driver is too old to perform a capability check.\n");
        av_log(avctx, AV_LOG_WARNING, "The minimum required version is "
#if defined(_WIN32) || defined(__CYGWIN__)
            "378.66"
#else
            "378.13"
#endif
            ". Continuing blind.\n");
        ctx->caps8.bIsSupported = ctx->caps10.bIsSupported = 1;
        // 12 bit was not supported before the capability check was introduced, so disable it.
        ctx->caps12.bIsSupported = 0;
        return 0;
    }

    ctx->caps8.eCodecType = ctx->caps10.eCodecType = ctx->caps12.eCodecType
        = muparseinfo->CodecType;
    ctx->caps8.eChromaFormat = ctx->caps10.eChromaFormat = ctx->caps12.eChromaFormat
        = musaVideoChromaFormat_420;

    ctx->caps8.nBitDepthMinus8 = 0;
    ctx->caps10.nBitDepthMinus8 = 2;
    ctx->caps12.nBitDepthMinus8 = 4;

    res8 = CHECK_MU(ctx->mvdl->muvidGetDecoderCaps(&ctx->caps8));
    res10 = CHECK_MU(ctx->mvdl->muvidGetDecoderCaps(&ctx->caps10));
    // res12 = CHECK_MU(ctx->mvdl->muvidGetDecoderCaps(&ctx->caps12));

    av_log(avctx, AV_LOG_VERBOSE, "MUVID capabilities for %s:\n", avctx->codec->name);
    av_log(avctx, AV_LOG_VERBOSE, "8 bit: supported: %d, min_width: %d, max_width: %d, min_height: %d, max_height: %d\n",
           ctx->caps8.bIsSupported, ctx->caps8.nMinWidth, ctx->caps8.nMaxWidth, ctx->caps8.nMinHeight, ctx->caps8.nMaxHeight);
    av_log(avctx, AV_LOG_VERBOSE, "10 bit: supported: %d, min_width: %d, max_width: %d, min_height: %d, max_height: %d\n",
           ctx->caps10.bIsSupported, ctx->caps10.nMinWidth, ctx->caps10.nMaxWidth, ctx->caps10.nMinHeight, ctx->caps10.nMaxHeight);
    av_log(avctx, AV_LOG_VERBOSE, "12 bit: supported: %d, min_width: %d, max_width: %d, min_height: %d, max_height: %d\n",
           ctx->caps12.bIsSupported, ctx->caps12.nMinWidth, ctx->caps12.nMaxWidth, ctx->caps12.nMinHeight, ctx->caps12.nMaxHeight);

    switch (bit_depth) {
    case 10:
        caps = &ctx->caps10;
        if (res10 < 0)
            return res10;
        break;
    case 12:
        caps = &ctx->caps12;
        if (res12 < 0)
            return res12;
        break;
    default:
        caps = &ctx->caps8;
        if (res8 < 0)
            return res8;
    }

    if (!ctx->caps8.bIsSupported) {
        av_log(avctx, AV_LOG_ERROR, "Codec %s is not supported.\n", avctx->codec->name);
        return AVERROR(EINVAL);
    }

    if (!caps->bIsSupported) {
        av_log(avctx, AV_LOG_ERROR, "Bit depth %d is not supported.\n", bit_depth);
        return AVERROR(EINVAL);
    }

    if (probed_width > caps->nMaxWidth || probed_width < caps->nMinWidth) {
        av_log(avctx, AV_LOG_ERROR, "Video width %d not within range from %d to %d\n",
               probed_width, caps->nMinWidth, caps->nMaxWidth);
        return AVERROR(EINVAL);
    }

    if (probed_height > caps->nMaxHeight || probed_height < caps->nMinHeight) {
        av_log(avctx, AV_LOG_ERROR, "Video height %d not within range from %d to %d\n",
               probed_height, caps->nMinHeight, caps->nMaxHeight);
        return AVERROR(EINVAL);
    }

    if ((probed_width * probed_height) / 256 > caps->nMaxMBCount) {
        av_log(avctx, AV_LOG_ERROR, "Video macroblock count %d exceeds maximum of %d\n",
               (int)(probed_width * probed_height) / 256, caps->nMaxMBCount);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold int muvid_decode_init(AVCodecContext *avctx)
{
    MuvidContext *ctx = avctx->priv_data;
    AVMUSADeviceContext *device_hwctx;
    AVHWDeviceContext *device_ctx;
    AVHWFramesContext *hwframe_ctx;
    MUVIDSOURCEDATAPACKET seq_pkt;
    MUcontext musa_ctx = NULL;
    MUcontext dummy;
    uint8_t *extradata;
    int extradata_size;
    int ret = 0;

    enum AVPixelFormat pix_fmts[3] = { AV_PIX_FMT_MUSA,
                                       AV_PIX_FMT_NV12,
                                       AV_PIX_FMT_NONE };

    int probed_width = avctx->coded_width ? avctx->coded_width : 1280;
    int probed_height = avctx->coded_height ? avctx->coded_height : 720;
    int probed_bit_depth = 8;

    const AVPixFmtDescriptor *probe_desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    if (probe_desc && probe_desc->nb_components)
        probed_bit_depth = probe_desc->comp[0].depth;

    ctx->pkt = avctx->internal->in_pkt;
    // Accelerated transcoding scenarios with 'ffmpeg' require that the
    // pix_fmt be set to AV_PIX_FMT_MUSA early. The sw_pix_fmt, and the
    // pix_fmt for non-accelerated transcoding, do not need to be correct
    // but need to be set to something. We arbitrarily pick NV12.
    ret = ff_get_format(avctx, pix_fmts);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_get_format failed: %d\n", ret);
        return ret;
    }
    avctx->pix_fmt = ret;

    if (ctx->resize_expr && sscanf(ctx->resize_expr, "%dx%d",
                                   &ctx->resize.width, &ctx->resize.height) != 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid resize expressions\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (ctx->crop_expr && sscanf(ctx->crop_expr, "%dx%dx%dx%d",
                                 &ctx->crop.top, &ctx->crop.bottom,
                                 &ctx->crop.left, &ctx->crop.right) != 4) {
        av_log(avctx, AV_LOG_ERROR, "Invalid cropping expressions\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    ret = muvid_load_functions(&ctx->mvdl, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed loading nvmuvid.\n");
        goto error;
    }

    // respect the deprecated "surfaces" option if non-default value is given by user;
    if(ctx->nb_surfaces < 0)
        ctx->nb_surfaces = MUVID_DEFAULT_NUM_SURFACES;

    ctx->frame_queue = av_fifo_alloc2(ctx->nb_surfaces, sizeof(MuvidParsedFrame), 0);
    if (!ctx->frame_queue) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    if (avctx->hw_frames_ctx) {
        ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->hwframe) {
            ret = AVERROR(ENOMEM);
            goto error;
        }

        hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;

        ctx->hwdevice = av_buffer_ref(hwframe_ctx->device_ref);
        if (!ctx->hwdevice) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
    } else {
        if (avctx->hw_device_ctx) {
            ctx->hwdevice = av_buffer_ref(avctx->hw_device_ctx);
            if (!ctx->hwdevice) {
                ret = AVERROR(ENOMEM);
                goto error;
            }
        } else {
            ret = av_hwdevice_ctx_create(&ctx->hwdevice, AV_HWDEVICE_TYPE_MUSA, ctx->mu_gpu, NULL, 0);
            if (ret < 0)
                goto error;
        }

        ctx->hwframe = av_hwframe_ctx_alloc(ctx->hwdevice);
        if (!ctx->hwframe) {
            av_log(avctx, AV_LOG_ERROR, "av_hwframe_ctx_alloc failed\n");
            ret = AVERROR(ENOMEM);
            goto error;
        }

        hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;
    }

    device_ctx = hwframe_ctx->device_ctx;
    device_hwctx = device_ctx->hwctx;

    musa_ctx = device_hwctx->musa_ctx;
    ctx->mudl = device_hwctx->internal->musa_dl;

    memset(&ctx->muparseinfo, 0, sizeof(ctx->muparseinfo));
    memset(&seq_pkt, 0, sizeof(seq_pkt));

    switch (avctx->codec->id) {
#if CONFIG_H264_MUVID_DECODER
    case AV_CODEC_ID_H264:
        ctx->muparseinfo.CodecType = musaVideoCodec_H264;
        break;
#endif
#if CONFIG_HEVC_MUVID_DECODER
    case AV_CODEC_ID_HEVC:
        ctx->muparseinfo.CodecType = musaVideoCodec_HEVC;
        break;
#endif

#if CONFIG_VP9_MUVID_DECODER
    case AV_CODEC_ID_VP9:
        ctx->muparseinfo.CodecType = musaVideoCodec_VP9;
        break;
#endif
#if CONFIG_AV1_MUVID_DECODER
    case AV_CODEC_ID_AV1:
        ctx->muparseinfo.CodecType = musaVideoCodec_AV1;
        break;
#endif
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid MUVID codec!\n");
        return AVERROR_BUG;
    }

    if (ffcodec(avctx->codec)->bsfs) {
        const AVCodecParameters *par = avctx->internal->bsf->par_out;
        extradata = par->extradata;
        extradata_size = par->extradata_size;
    } else {
        extradata = avctx->extradata;
        extradata_size = avctx->extradata_size;
    }

    // Check first bit to determine whether it's AV1CodecConfigurationRecord.
    // Skip first 4 bytes of AV1CodecConfigurationRecord to keep configOBUs
    // only, otherwise muvidParseVideoData report unknown error.
    if (avctx->codec->id == AV_CODEC_ID_AV1 &&
            extradata_size > 4 &&
            extradata[0] & 0x80) {
        extradata += 4;
        extradata_size -= 4;
    }

    ctx->muparse_ext = av_mallocz(sizeof(*ctx->muparse_ext)
            + FFMAX(extradata_size - (int)sizeof(ctx->muparse_ext->raw_seqhdr_data), 0));
    if (!ctx->muparse_ext) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    if (extradata_size > 0)
        memcpy(ctx->muparse_ext->raw_seqhdr_data, extradata, extradata_size);
    ctx->muparse_ext->format.seqhdr_data_length = extradata_size;

    ctx->muparseinfo.pExtVideoInfo = ctx->muparse_ext;

    ctx->key_frame = av_mallocz(ctx->nb_surfaces * sizeof(int));
    if (!ctx->key_frame) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    ctx->muparseinfo.ulMaxNumDecodeSurfaces = ctx->nb_surfaces;
    ctx->muparseinfo.ulMaxDisplayDelay = (avctx->flags & AV_CODEC_FLAG_LOW_DELAY) ? 0 : MUVID_MAX_DISPLAY_DELAY;
    ctx->muparseinfo.pUserData = avctx;
    ctx->muparseinfo.pfnSequenceCallback = muvid_handle_video_sequence;
    ctx->muparseinfo.pfnDecodePicture = muvid_handle_picture_decode;
    ctx->muparseinfo.pfnDisplayPicture = muvid_handle_picture_display;

    ret = CHECK_MU(ctx->mudl->muCtxPushCurrent(musa_ctx));
    if (ret < 0)
        goto error;

    ret = muvid_test_capabilities(avctx, &ctx->muparseinfo,
                                  probed_width,
                                  probed_height,
                                  probed_bit_depth);
    if (ret < 0)
        goto error;

    ret = CHECK_MU(ctx->mvdl->muvidCreateVideoParser(&ctx->muparser, &ctx->muparseinfo));
    if (ret < 0)
        goto error;

    seq_pkt.payload = ctx->muparse_ext->raw_seqhdr_data;
    seq_pkt.payload_size = ctx->muparse_ext->format.seqhdr_data_length;

    if (seq_pkt.payload && seq_pkt.payload_size) {
        ret = CHECK_MU(ctx->mvdl->muvidParseVideoData(ctx->muparser, &seq_pkt));
        if (ret < 0)
            goto error;
    }

    ret = CHECK_MU(ctx->mudl->muCtxPopCurrent(&dummy));
    if (ret < 0)
        goto error;

    ctx->prev_pts = INT64_MIN;

    if (!avctx->pkt_timebase.num || !avctx->pkt_timebase.den)
        av_log(avctx, AV_LOG_WARNING, "Invalid pkt_timebase, passing timestamps as-is.\n");

    return 0;

error:
    muvid_decode_end(avctx);
    return ret;
}

static void muvid_flush(AVCodecContext *avctx)
{
    MuvidContext *ctx = avctx->priv_data;
    AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)ctx->hwdevice->data;
    AVMUSADeviceContext *device_hwctx = device_ctx->hwctx;
    MUcontext dummy, musa_ctx = device_hwctx->musa_ctx;
    MUVIDSOURCEDATAPACKET seq_pkt = { 0 };
    int ret;

    ret = CHECK_MU(ctx->mudl->muCtxPushCurrent(musa_ctx));
    if (ret < 0)
        goto error;

    av_fifo_reset2(ctx->frame_queue);

    if (ctx->mudecoder) {
        ctx->mvdl->muvidDestroyDecoder(ctx->mudecoder);
        ctx->mudecoder = NULL;
    }

    if (ctx->muparser) {
        ctx->mvdl->muvidDestroyVideoParser(ctx->muparser);
        ctx->muparser = NULL;
    }

    ret = CHECK_MU(ctx->mvdl->muvidCreateVideoParser(&ctx->muparser, &ctx->muparseinfo));
    if (ret < 0)
        goto error;

    seq_pkt.payload = ctx->muparse_ext->raw_seqhdr_data;
    seq_pkt.payload_size = ctx->muparse_ext->format.seqhdr_data_length;

    if (seq_pkt.payload && seq_pkt.payload_size) {
        ret = CHECK_MU(ctx->mvdl->muvidParseVideoData(ctx->muparser, &seq_pkt));
        if (ret < 0)
            goto error;
    }

    ret = CHECK_MU(ctx->mudl->muCtxPopCurrent(&dummy));
    if (ret < 0)
        goto error;

    ctx->prev_pts = INT64_MIN;
    ctx->decoder_flushing = 0;

    return;
 error:
    av_log(avctx, AV_LOG_ERROR, "MUSA reinit on flush failed\n");
}

#define OFFSET(x) offsetof(MuvidContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "deint",    "Set deinterlacing mode", OFFSET(deint_mode), AV_OPT_TYPE_INT,   { .i64 = musaVideoDeinterlaceMode_Weave    }, musaVideoDeinterlaceMode_Weave, musaVideoDeinterlaceMode_Adaptive, VD, .unit = "deint" },
    { "weave",    "Weave deinterlacing (do nothing)",        0, AV_OPT_TYPE_CONST, { .i64 = musaVideoDeinterlaceMode_Weave    }, 0, 0, VD, .unit = "deint" },
    { "bob",      "Bob deinterlacing",                       0, AV_OPT_TYPE_CONST, { .i64 = musaVideoDeinterlaceMode_Bob      }, 0, 0, VD, .unit = "deint" },
    { "adaptive", "Adaptive deinterlacing",                  0, AV_OPT_TYPE_CONST, { .i64 = musaVideoDeinterlaceMode_Adaptive }, 0, 0, VD, .unit = "deint" },
    { "gpu",      "GPU to be used for decoding", OFFSET(mu_gpu), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VD },
    { "surfaces", "Maximum surfaces to be used for decoding", OFFSET(nb_surfaces), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, VD | AV_OPT_FLAG_DEPRECATED },
    { "drop_second_field", "Drop second field when deinterlacing", OFFSET(drop_second_field), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VD },
    { "crop",     "Crop (top)x(bottom)x(left)x(right)", OFFSET(crop_expr), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VD },
    { "resize",   "Resize (width)x(height)", OFFSET(resize_expr), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VD },
    { NULL }
};

static const AVCodecHWConfigInternal *const muvid_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt     = AV_PIX_FMT_MUSA,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX |
                           AV_CODEC_HW_CONFIG_METHOD_INTERNAL,
            .device_type = AV_HWDEVICE_TYPE_MUSA
        },
        .hwaccel = NULL,
    },
    NULL
};

#define DEFINE_MUVID_CODEC(x, X, bsf_name) \
    static const AVClass x##_muvid_class = { \
        .class_name = #x "_muvid", \
        .item_name = av_default_item_name, \
        .option = options, \
        .version = LIBAVUTIL_VERSION_INT, \
    }; \
    const FFCodec ff_##x##_muvid_decoder = { \
        .p.name         = #x "_muvid", \
        CODEC_LONG_NAME("Moore threads MUVID " #X " decoder"), \
        .p.type         = AVMEDIA_TYPE_VIDEO, \
        .p.id           = AV_CODEC_ID_##X, \
        .priv_data_size = sizeof(MuvidContext), \
        .p.priv_class   = &x##_muvid_class, \
        .init           = muvid_decode_init, \
        .close          = muvid_decode_end, \
        FF_CODEC_RECEIVE_FRAME_CB(muvid_output_frame), \
        .flush          = muvid_flush, \
        .bsfs           = bsf_name, \
        .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HARDWARE, \
        .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE | \
                          FF_CODEC_CAP_SETS_FRAME_PROPS, \
        .hw_configs     = muvid_hw_configs, \
        .p.wrapper_name = "muvid", \
    };

#if CONFIG_AV1_MUVID_DECODER
DEFINE_MUVID_CODEC(av1, AV1, NULL)
#endif

#if CONFIG_HEVC_MUVID_DECODER
DEFINE_MUVID_CODEC(hevc, HEVC, "hevc_mp4toannexb")
#endif

#if CONFIG_H264_MUVID_DECODER
DEFINE_MUVID_CODEC(h264, H264, "h264_mp4toannexb")
#endif

#if CONFIG_VP9_MUVID_DECODER
DEFINE_MUVID_CODEC(vp9, VP9, NULL)
#endif