/*
 * Copyright (c) 2022 Mohamed Khaled <Mohamed_Khaled_Kamal@outlook.com>
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

#include <float.h>
#include <stdio.h>

#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_musa_internal.h"
#include "libavutil/musa_check.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

#include "musa/load_helper.h"

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV444P
};

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_MU(x) FF_MUSA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)


typedef struct MUSABilateralContext {
    const AVClass *class;
    AVMUSADeviceContext *hwctx;

    const AVPixFmtDescriptor *in_desc;
    int in_planes;
    int in_plane_channels[4];

    int   window_size;
    float sigmaS;
    float sigmaR;

    MUcontext   cu_ctx;
    MUmodule    cu_module;
    MUfunction  cu_func;
    MUfunction  cu_func_uv;
    MUstream    cu_stream;
    AVFrame    *held_in;
} MUSABilateralContext;

static av_cold void musabilateral_uninit(AVFilterContext *ctx)
{
    MUSABilateralContext *s = ctx->priv;

    if (s->hwctx && s->cu_module) {
        MusaFunctions *mu = s->hwctx->internal->cuda_dl;
        MUcontext bilateral;

        CHECK_MU(mu->muCtxPushCurrent(s->hwctx->musa_ctx));
        CHECK_MU(mu->muStreamSynchronize(s->cu_stream));
        CHECK_MU(mu->muModuleUnload(s->cu_module));
        s->cu_module = NULL;
        CHECK_MU(mu->muCtxPopCurrent(&bilateral));
    }

    av_frame_free(&s->held_in);
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static av_cold void set_format_info(AVFilterContext *ctx, enum AVPixelFormat format)
{
    MUSABilateralContext *s = ctx->priv;
    int i, p, d;

    s->in_desc = av_pix_fmt_desc_get(format);
    s->in_planes = av_pix_fmt_count_planes(format);

    // find maximum step of each component of each plane
    // For our subset of formats, this should accurately tell us how many channels MUSA needs
    // i.e. 1 for Y plane, 2 for UV plane of NV12, 4 for single plane of RGB0 formats

    for (i = 0; i < s->in_desc->nb_components; i++) {
        d = (s->in_desc->comp[i].depth + 7) / 8;
        p = s->in_desc->comp[i].plane;
        s->in_plane_channels[p] = FFMAX(s->in_plane_channels[p], s->in_desc->comp[i].step / d);
    }
}

static av_cold int init_processing_chain(AVFilterContext *ctx, int width, int height)
{
    FilterLink         *inl = ff_filter_link(ctx->inputs[0]);
    FilterLink        *outl = ff_filter_link(ctx->outputs[0]);
    AVHWFramesContext *in_frames_ctx;

    /* check that we have a hw context */
    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }
    in_frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;

    if (!format_is_supported(in_frames_ctx->sw_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported format: %s\n", av_get_pix_fmt_name(in_frames_ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    set_format_info(ctx, in_frames_ctx->sw_format);

    outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int musa_bilateral_load_functions(AVFilterContext *ctx)
{
    MUSABilateralContext *s = ctx->priv;
    MUcontext bilateral, cuda_ctx = s->hwctx->musa_ctx;
    MusaFunctions *mu = s->hwctx->internal->cuda_dl;
    int ret;

    extern const unsigned char ff_vf_bilateral_musa_mu_data[];
    extern const unsigned int ff_vf_bilateral_musa_mu_len;

    ret = CHECK_MU(mu->muCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_musa_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_bilateral_musa_mu_data, ff_vf_bilateral_musa_mu_len);
    if (ret < 0)
        goto fail;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func, s->cu_module, "Process_uchar"));
    if (ret < 0) {
        av_log(ctx, AV_LOG_FATAL, "Failed loading Process_uchar\n");
        goto fail;
    }

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_uv, s->cu_module, "Process_uchar2"));
    if (ret < 0) {
        av_log(ctx, AV_LOG_FATAL, "Failed loading Process_uchar2\n");
        goto fail;
    }

fail:
    CHECK_MU(mu->muCtxPopCurrent(&bilateral));

    return ret;
}

static av_cold int musa_bilateral_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    MUSABilateralContext *s  = ctx->priv;
    AVHWFramesContext     *frames_ctx;
    AVMUSADeviceContext *device_hwctx;
    int ret;

    ret = init_processing_chain(ctx, inlink->w, inlink->h);
    if (ret < 0)
        return ret;

    frames_ctx   = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    device_hwctx = frames_ctx->device_ctx->hwctx;

    s->hwctx = device_hwctx;
    s->cu_stream = s->hwctx->stream;

    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    // the window_size makes more sense when it is odd, so add 1 if even
    s->window_size |= 1;

    ret = musa_bilateral_load_functions(ctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int call_musa_kernel(AVFilterContext *ctx, MUfunction func,
                            MUtexObject src_tex[3], AVFrame *out_frame,
                            int width, int height, int pitch,
                            int width_uv, int height_uv, int pitch_uv,
                            int window_size, float sigmaS, float sigmaR)
{
    MUSABilateralContext *s = ctx->priv;
    MusaFunctions *mu = s->hwctx->internal->cuda_dl;
    int ret;

    MUdeviceptr dst_devptr[3] = {
        (MUdeviceptr)out_frame->data[0], (MUdeviceptr)out_frame->data[1], (MUdeviceptr)out_frame->data[2]
    };

    void *args_uchar[] = {
        &src_tex[0], &src_tex[1], &src_tex[2],
        &dst_devptr[0], &dst_devptr[1], &dst_devptr[2],
        &width, &height, &pitch,
        &width_uv, &height_uv, &pitch_uv,
        &window_size, &sigmaS, &sigmaR
    };

    ret = CHECK_MU(mu->muLaunchKernel(func,
                                      DIV_UP(width, BLOCKX), DIV_UP(height, BLOCKY), 1,
                                      BLOCKX, BLOCKY, 1, 0, s->cu_stream, args_uchar, NULL));
    if (ret < 0)
        return ret;

    return ret;
}

static int musa_bilateral_process_internal(AVFilterContext *ctx,
                                      AVFrame *out, AVFrame *in)
{
    MUSABilateralContext *s = ctx->priv;
    MusaFunctions *mu = s->hwctx->internal->cuda_dl;
    MUcontext bilateral, cuda_ctx = s->hwctx->musa_ctx;
    int i, ret;

    MUtexObject tex[3] = { 0, 0, 0 };

    ret = CHECK_MU(mu->muCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    for (i = 0; i < s->in_planes; i++) {
        MUSA_TEXTURE_DESC tex_desc = {
            .filterMode = MU_TR_FILTER_MODE_LINEAR,
            .flags = 0, // MU_TRSF_READ_AS_INTEGER to get raw ints instead of normalized floats from tex2D
        };

        MUSA_RESOURCE_DESC res_desc = {
            .resType = MU_RESOURCE_TYPE_PITCH2D,
            .res.pitch2D.format = MU_AD_FORMAT_UNSIGNED_INT8,
            .res.pitch2D.numChannels = s->in_plane_channels[i],
            .res.pitch2D.pitchInBytes = in->linesize[i],
            .res.pitch2D.devPtr = (MUdeviceptr)in->data[i],
        };

        if (i == 1 || i == 2) {
            res_desc.res.pitch2D.width = AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w);
            res_desc.res.pitch2D.height = AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h);
        } else {
            res_desc.res.pitch2D.width = in->width;
            res_desc.res.pitch2D.height = in->height;
        }

        ret = CHECK_MU(mu->muTexObjectCreate(&tex[i], &res_desc, &tex_desc, NULL));
        if (ret < 0)
            goto exit;
    }

    ret = call_musa_kernel(ctx, (s->in_plane_channels[1] > 1) ? s->cu_func_uv : s->cu_func,
                           tex, out,
                           out->width, out->height, out->linesize[0],
                           AV_CEIL_RSHIFT(out->width, s->in_desc->log2_chroma_w),
                           AV_CEIL_RSHIFT(out->height, s->in_desc->log2_chroma_h),
                           out->linesize[1] >> ((s->in_plane_channels[1] > 1) ? 1 : 0),
                           s->window_size, s->sigmaS, s->sigmaR);

exit:
    if (ret >= 0)
        ret = CHECK_MU(mu->muStreamSynchronize(s->cu_stream));

    for (i = 0; i < s->in_planes; i++)
        if (tex[i])
            CHECK_MU(mu->muTexObjectDestroy(tex[i]));

    CHECK_MU(mu->muCtxPopCurrent(&bilateral));

    return ret;
}

static int musa_bilateral_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext       *ctx = link->dst;
    MUSABilateralContext        *s = ctx->priv;
    AVFilterLink      *outlink = ctx->outputs[0];
    MusaFunctions          *mu = s->hwctx->internal->cuda_dl;

    AVFrame *out;
    MUcontext bilateral;
    int ret = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_MU(mu->muCtxPushCurrent(s->hwctx->musa_ctx));
    if (ret < 0)
        goto fail;

    ret = musa_bilateral_process_internal(ctx, out, in);
    if (ret < 0)
        goto fail;

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    ret = CHECK_MU(mu->muCtxPopCurrent(&bilateral));
    if (ret < 0)
        goto fail;

    /*
     * Keep one input frame alive until the next frame arrives.  The MUSA
     * hwframe buffer can otherwise be released while the downstream graph is
     * still finishing its use of the previous frame on raw-video inputs.
     */
    if (s->held_in)
        av_frame_free(&s->held_in);

    s->held_in = in;
    in = NULL;
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(MUSABilateralContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    { "sigmaS",      "set spatial sigma",          OFFSET(sigmaS), AV_OPT_TYPE_FLOAT,    {.dbl=0.1}, 0.1, 512, FLAGS },
    { "sigmaR",      "set range sigma",            OFFSET(sigmaR), AV_OPT_TYPE_FLOAT,    {.dbl=0.1}, 0.1, 512, FLAGS },
    { "window_size", "set neighbours window_size", OFFSET(window_size), AV_OPT_TYPE_INT, {.i64=1},   1,   255, FLAGS },
    { NULL }
};

static const AVClass musa_bilateral_class = {
    .class_name = "musabilateral",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad musa_bilateral_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
        .filter_frame = musa_bilateral_filter_frame,
    },
};

static const AVFilterPad musa_bilateral_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = musa_bilateral_config_props,
    },
};

const FFFilter ff_vf_bilateral_musa = {
    .p.name        = "bilateral_musa",
    .p.description = NULL_IF_CONFIG_SMALL("GPU accelerated bilateral filter"),
    .p.priv_class  = &musa_bilateral_class,
    .uninit        = musabilateral_uninit,
    .priv_size     = sizeof(MUSABilateralContext),
    FILTER_INPUTS(musa_bilateral_inputs),
    FILTER_OUTPUTS(musa_bilateral_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_MUSA),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
