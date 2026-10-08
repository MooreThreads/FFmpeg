/*
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/musa_check.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_musa_internal.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"

#include "musa/load_helper.h"

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
};

#define DIV_UP(a, b) (((a) + (b)-1) / (b))
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_MU(x) FF_MUSA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

typedef struct MUSAColorspaceContext {
    const AVClass* class;

    AVMUSADeviceContext* hwctx;
    AVBufferRef* frames_ctx;
    AVFrame* own_frame;
    AVFrame* tmp_frame;

    MUcontext cu_ctx;
    MUstream cu_stream;
    MUmodule cu_module;
    MUfunction cu_convert[AVCOL_RANGE_NB];

    enum AVPixelFormat pix_fmt;
    /* enum AVColorRange */
    int range;

    int num_planes;
} MUSAColorspaceContext;

static av_cold int musacolorspace_init(AVFilterContext* ctx)
{
    MUSAColorspaceContext* s = ctx->priv;

    s->own_frame = av_frame_alloc();
    if (!s->own_frame)
        return AVERROR(ENOMEM);

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void musacolorspace_uninit(AVFilterContext* ctx)
{
    MUSAColorspaceContext* s = ctx->priv;

    if (s->hwctx && s->cu_module) {
        MusaFunctions* mu = s->hwctx->internal->cuda_dl;
        MUcontext dummy;

        CHECK_MU(mu->muCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_MU(mu->muModuleUnload(s->cu_module));
        s->cu_module = NULL;
        CHECK_MU(mu->muCtxPopCurrent(&dummy));
    }

    av_frame_free(&s->own_frame);
    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->tmp_frame);
}

static av_cold int init_hwframe_ctx(MUSAColorspaceContext* s, AVBufferRef* device_ctx,
                                    int width, int height)
{
    AVBufferRef* out_ref = NULL;
    AVHWFramesContext* out_ctx;
    int ret;

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);

    out_ctx = (AVHWFramesContext*)out_ref->data;

    out_ctx->format = AV_PIX_FMT_MUSA;
    out_ctx->sw_format = s->pix_fmt;
    out_ctx->width = FFALIGN(width, 32);
    out_ctx->height = FFALIGN(height, 32);

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(s->own_frame);
    ret = av_hwframe_get_buffer(out_ref, s->own_frame, 0);
    if (ret < 0)
        goto fail;

    s->own_frame->width = width;
    s->own_frame->height = height;

    av_buffer_unref(&s->frames_ctx);
    s->frames_ctx = out_ref;

    return 0;
fail:
    av_buffer_unref(&out_ref);
    return ret;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (fmt == supported_formats[i])
            return 1;

    return 0;
}

static av_cold int init_processing_chain(AVFilterContext* ctx, int width,
                                         int height)
{
    FilterLink          *inl = ff_filter_link(ctx->inputs[0]);
    FilterLink         *outl = ff_filter_link(ctx->outputs[0]);
    MUSAColorspaceContext* s = ctx->priv;
    AVHWFramesContext* in_frames_ctx;

    int ret;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    s->pix_fmt = in_frames_ctx->sw_format;

    if (!format_is_supported(s->pix_fmt)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format: %s\n",
               av_get_pix_fmt_name(s->pix_fmt));
        return AVERROR(EINVAL);
    }

    if ((AVCOL_RANGE_MPEG != s->range) && (AVCOL_RANGE_JPEG != s->range)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported color range\n");
        return AVERROR(EINVAL);
    }

    s->num_planes = av_pix_fmt_count_planes(s->pix_fmt);

    ret = init_hwframe_ctx(s, in_frames_ctx->device_ref, width, height);
    if (ret < 0)
        return ret;

    outl->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int musacolorspace_load_functions(AVFilterContext* ctx)
{
    MUSAColorspaceContext* s = ctx->priv;
    MUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    MusaFunctions* mu = s->hwctx->internal->cuda_dl;
    int ret;

    extern const unsigned char ff_vf_colorspace_musa_mu_data[];
    extern const unsigned int ff_vf_colorspace_musa_mu_len;

    ret = CHECK_MU(mu->muCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_musa_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_colorspace_musa_mu_data,
                              ff_vf_colorspace_musa_mu_len);
    if (ret < 0)
        goto fail;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_convert[AVCOL_RANGE_MPEG], s->cu_module, "to_mpeg_musa"));
    if (ret < 0)
        goto fail;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_convert[AVCOL_RANGE_JPEG], s->cu_module, "to_jpeg_musa"));
    if (ret < 0)
        goto fail;

fail:
    CHECK_MU(mu->muCtxPopCurrent(&dummy));
    return ret;
}

static av_cold int musacolorspace_config_props(AVFilterLink* outlink)
{
    AVFilterContext* ctx = outlink->src;
    AVFilterLink* inlink = outlink->src->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    MUSAColorspaceContext* s = ctx->priv;
    AVHWFramesContext* frames_ctx;
    AVMUSADeviceContext* device_hwctx;
    int ret;

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    ret = init_processing_chain(ctx, inlink->w, inlink->h);
    if (ret < 0)
        return ret;

    frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    device_hwctx = frames_ctx->device_ctx->hwctx;

    s->hwctx = device_hwctx;
    s->cu_stream = s->hwctx->stream;

    if (inlink->sample_aspect_ratio.num) {
        outlink->sample_aspect_ratio = av_mul_q(
            (AVRational){outlink->h * inlink->w, outlink->w * inlink->h},
            inlink->sample_aspect_ratio);
    } else {
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    }

    ret = musacolorspace_load_functions(ctx);
    if (ret < 0)
        return ret;

    return ret;
}

static int conv_musa_convert(AVFilterContext* ctx, AVFrame* out, AVFrame* in)
{
    MUSAColorspaceContext* s = ctx->priv;
    MusaFunctions* mu = s->hwctx->internal->cuda_dl;
    MUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    int ret;

    ret = CHECK_MU(mu->muCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    out->color_range = s->range;

    for (int i = 0; i < s->num_planes; i++) {
        int width = in->width, height = in->height, comp_id = (i > 0);

        switch (s->pix_fmt) {
        case AV_PIX_FMT_YUV444P:
            break;
        case AV_PIX_FMT_YUV420P:
            width = comp_id ? in->width / 2 : in->width;
            /* fall-through */
        case AV_PIX_FMT_NV12:
            height = comp_id ? in->height / 2 : in->height;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format: %s\n",
                   av_get_pix_fmt_name(s->pix_fmt));
            return AVERROR(EINVAL);
        }

        if (!s->cu_convert[out->color_range]) {
            av_log(ctx, AV_LOG_ERROR, "Unsupported color range\n");
            return AVERROR(EINVAL);
        }

        if (in->color_range != out->color_range) {
            void* args[] = {&in->data[i], &out->data[i], &in->linesize[i],
                            &comp_id};
            ret = CHECK_MU(mu->muLaunchKernel(
                s->cu_convert[out->color_range], DIV_UP(width, BLOCKX),
                DIV_UP(height, BLOCKY), 1, BLOCKX, BLOCKY, 1, 0, s->cu_stream,
                args, NULL));
        } else {
            ret = av_hwframe_transfer_data(out, in, 0);
            if (ret < 0)
                return ret;
        }
    }

    CHECK_MU(mu->muCtxPopCurrent(&dummy));
    return ret;
}

static int musacolorspace_conv(AVFilterContext* ctx, AVFrame* out, AVFrame* in)
{
    MUSAColorspaceContext* s = ctx->priv;
    AVFilterLink* outlink = ctx->outputs[0];
    AVFrame* src = in;
    int ret;

    ret = conv_musa_convert(ctx, s->own_frame, src);
    if (ret < 0)
        return ret;

    src = s->own_frame;
    ret = av_hwframe_get_buffer(src->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        return ret;

    av_frame_move_ref(out, s->own_frame);
    av_frame_move_ref(s->own_frame, s->tmp_frame);

    s->own_frame->width = outlink->w;
    s->own_frame->height = outlink->h;

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        return ret;

    return 0;
}

static int musacolorspace_filter_frame(AVFilterLink* link, AVFrame* in)
{
    AVFilterContext* ctx = link->dst;
    MUSAColorspaceContext* s = ctx->priv;
    AVFilterLink* outlink = ctx->outputs[0];
    MusaFunctions* mu = s->hwctx->internal->cuda_dl;

    AVFrame* out = NULL;
    MUcontext dummy;
    int ret = 0;

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_MU(mu->muCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = musacolorspace_conv(ctx, out, in);

    CHECK_MU(mu->muCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(MUSAColorspaceContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    {"range", "Output video range", OFFSET(range), AV_OPT_TYPE_INT, { .i64 = AVCOL_RANGE_UNSPECIFIED }, AVCOL_RANGE_UNSPECIFIED, AVCOL_RANGE_NB - 1, FLAGS, .unit = "range"},
        {"tv",   "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range"},
        {"mpeg", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range"},
        {"pc",   "Full range",    0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range"},
        {"jpeg", "Full range",    0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range"},
    {NULL},
};

static const AVClass musacolorspace_class = {
    .class_name = "colorspace_musa",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad musacolorspace_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = musacolorspace_filter_frame,
    },
};

static const AVFilterPad musacolorspace_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = musacolorspace_config_props,
    },
};

const FFFilter ff_vf_colorspace_musa = {
    .p.name        = "colorspace_musa",
    .p.description = NULL_IF_CONFIG_SMALL("MUSA accelerated video color converter"),

    .p.priv_class  = &musacolorspace_class,

    .init = musacolorspace_init,
    .uninit = musacolorspace_uninit,

    .priv_size = sizeof(MUSAColorspaceContext),

    FILTER_INPUTS(musacolorspace_inputs),
    FILTER_OUTPUTS(musacolorspace_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_MUSA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
