/*
 * Copyright (C) 2018 Philip Langdale <philipl@overt.org>
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

#include "libavutil/avassert.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_musa_internal.h"
#include "libavutil/musa_check.h"

#include "filters.h"
#include "yadif.h"

#include "musa/load_helper.h"

extern const unsigned char ff_vf_yadif_musa_mu_data[];
extern const unsigned int ff_vf_yadif_musa_mu_len;

typedef struct DeintMUSAContext {
    YADIFContext yadif;

    AVMUSADeviceContext *hwctx;
    AVBufferRef         *device_ref;
    AVBufferRef         *input_frames_ref;
    AVHWFramesContext   *input_frames;

    MUmodule    cu_module;
    MUfunction  cu_func_uchar;
    MUfunction  cu_func_uchar2;
    MUfunction  cu_func_ushort;
    MUfunction  cu_func_ushort2;
} DeintMUSAContext;

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define ALIGN_UP(a, b) (((a) + (b) - 1) & ~((b) - 1))
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_MU(x) FF_MUSA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

static MUresult call_kernel(AVFilterContext *ctx, MUfunction func,
                            MUdeviceptr prev, MUdeviceptr cur, MUdeviceptr next,
                            MUarray_format format, int channels,
                            int src_width,  // Width is pixels per channel
                            int src_height, // Height is pixels per channel
                            int src_pitch,  // Pitch is bytes
                            MUdeviceptr dst,
                            int dst_width,  // Width is pixels per channel
                            int dst_height, // Height is pixels per channel
                            int dst_pitch,  // Pitch is pixels per channel
                            int parity, int tff)
{
    DeintMUSAContext *s = ctx->priv;
    MusaFunctions *mu = s->hwctx->internal->cuda_dl;
    MUtexObject tex_prev = 0, tex_cur = 0, tex_next = 0;
    int ret;
    int skip_spatial_check = s->yadif.mode&2;

    void *args[] = { &dst, &tex_prev, &tex_cur, &tex_next,
                     &dst_width, &dst_height, &dst_pitch,
                     &src_width, &src_height, &parity, &tff,
                     &skip_spatial_check };

    MUSA_TEXTURE_DESC tex_desc = {
        .filterMode = MU_TR_FILTER_MODE_POINT,
        .flags = MU_TRSF_READ_AS_INTEGER,
    };

    MUSA_RESOURCE_DESC res_desc = {
        .resType = MU_RESOURCE_TYPE_PITCH2D,
        .res.pitch2D.format = format,
        .res.pitch2D.numChannels = channels,
        .res.pitch2D.width = src_width,
        .res.pitch2D.height = src_height,
        .res.pitch2D.pitchInBytes = src_pitch,
    };

    res_desc.res.pitch2D.devPtr = (MUdeviceptr)prev;
    ret = CHECK_MU(mu->muTexObjectCreate(&tex_prev, &res_desc, &tex_desc, NULL));
    if (ret < 0)
        goto exit;

    res_desc.res.pitch2D.devPtr = (MUdeviceptr)cur;
    ret = CHECK_MU(mu->muTexObjectCreate(&tex_cur, &res_desc, &tex_desc, NULL));
    if (ret < 0)
        goto exit;

    res_desc.res.pitch2D.devPtr = (MUdeviceptr)next;
    ret = CHECK_MU(mu->muTexObjectCreate(&tex_next, &res_desc, &tex_desc, NULL));
    if (ret < 0)
        goto exit;

    ret = CHECK_MU(mu->muLaunchKernel(func,
                                      DIV_UP(dst_width, BLOCKX), DIV_UP(dst_height, BLOCKY), 1,
                                      BLOCKX, BLOCKY, 1,
                                      0, s->hwctx->stream, args, NULL));

exit:
    if (tex_prev)
        CHECK_MU(mu->muTexObjectDestroy(tex_prev));
    if (tex_cur)
        CHECK_MU(mu->muTexObjectDestroy(tex_cur));
    if (tex_next)
        CHECK_MU(mu->muTexObjectDestroy(tex_next));

    return ret;
}

static void filter(AVFilterContext *ctx, AVFrame *dst,
                   int parity, int tff)
{
    DeintMUSAContext *s = ctx->priv;
    YADIFContext *y = &s->yadif;
    MusaFunctions *mu = s->hwctx->internal->cuda_dl;
    MUcontext dummy;
    int i, ret;

    ret = CHECK_MU(mu->muCtxPushCurrent(s->hwctx->musa_ctx));
    if (ret < 0)
        return;

    for (i = 0; i < y->csp->nb_components; i++) {
        MUfunction func;
        MUarray_format format;
        int pixel_size, channels;
        const AVComponentDescriptor *comp = &y->csp->comp[i];

        if (comp->plane < i) {
            // We process planes as a whole, so don't reprocess
            // them for additional components
            continue;
        }

        pixel_size = (comp->depth + comp->shift) / 8;
        channels = comp->step / pixel_size;
        if (pixel_size > 2 || channels > 2) {
            av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format: %s\n", y->csp->name);
            goto exit;
        }
        switch (pixel_size) {
        case 1:
            func = channels == 1 ? s->cu_func_uchar : s->cu_func_uchar2;
            format = MU_AD_FORMAT_UNSIGNED_INT8;
            break;
        case 2:
            func = channels == 1 ? s->cu_func_ushort : s->cu_func_ushort2;
            format = MU_AD_FORMAT_UNSIGNED_INT16;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format: %s\n", y->csp->name);
            goto exit;
        }
        av_log(ctx, AV_LOG_TRACE,
               "Deinterlacing plane %d: pixel_size: %d channels: %d\n",
               comp->plane, pixel_size, channels);
        call_kernel(ctx, func,
                    (MUdeviceptr)y->prev->data[i],
                    (MUdeviceptr)y->cur->data[i],
                    (MUdeviceptr)y->next->data[i],
                    format, channels,
                    AV_CEIL_RSHIFT(y->cur->width, i ? y->csp->log2_chroma_w : 0),
                    AV_CEIL_RSHIFT(y->cur->height, i ? y->csp->log2_chroma_h : 0),
                    y->cur->linesize[i],
                    (MUdeviceptr)dst->data[i],
                    AV_CEIL_RSHIFT(dst->width, i ? y->csp->log2_chroma_w : 0),
                    AV_CEIL_RSHIFT(dst->height, i ? y->csp->log2_chroma_h : 0),
                    dst->linesize[i] / comp->step,
                    parity, tff);
    }

exit:
    CHECK_MU(mu->muCtxPopCurrent(&dummy));
    return;
}

static av_cold void deint_musa_uninit(AVFilterContext *ctx)
{
    MUcontext dummy;
    DeintMUSAContext *s = ctx->priv;

    if (s->hwctx && s->cu_module) {
        MusaFunctions *mu = s->hwctx->internal->cuda_dl;
        CHECK_MU(mu->muCtxPushCurrent(s->hwctx->musa_ctx));
        CHECK_MU(mu->muModuleUnload(s->cu_module));
        CHECK_MU(mu->muCtxPopCurrent(&dummy));
    }

    ff_yadif_uninit(ctx);

    av_buffer_unref(&s->device_ref);
    s->hwctx = NULL;
    av_buffer_unref(&s->input_frames_ref);
    s->input_frames = NULL;
}

static int config_input(AVFilterLink *inlink)
{
    FilterLink        *l = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    DeintMUSAContext *s  = ctx->priv;

    if (!l->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the processing device.\n");
        return AVERROR(EINVAL);
    }

    s->input_frames_ref = av_buffer_ref(l->hw_frames_ctx);
    if (!s->input_frames_ref) {
        av_log(ctx, AV_LOG_ERROR, "A input frames reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    s->input_frames = (AVHWFramesContext*)s->input_frames_ref->data;

    return 0;
}

static int config_output(AVFilterLink *link)
{
    FilterLink *l = ff_filter_link(link);
    AVHWFramesContext *output_frames;
    AVFilterContext *ctx = link->src;
    DeintMUSAContext *s = ctx->priv;
    YADIFContext *y = &s->yadif;
    MusaFunctions *mu;
    int ret = 0;
    MUcontext dummy;

    av_assert0(s->input_frames);
    s->device_ref = av_buffer_ref(s->input_frames->device_ref);
    if (!s->device_ref) {
        av_log(ctx, AV_LOG_ERROR, "A device reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    s->hwctx = ((AVHWDeviceContext*)s->device_ref->data)->hwctx;
    mu = s->hwctx->internal->cuda_dl;

    l->hw_frames_ctx = av_hwframe_ctx_alloc(s->device_ref);
    if (!l->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create HW frame context "
               "for output.\n");
        ret = AVERROR(ENOMEM);
        goto exit;
    }

    output_frames = (AVHWFramesContext*)l->hw_frames_ctx->data;

    output_frames->format    = AV_PIX_FMT_MUSA;
    output_frames->sw_format = s->input_frames->sw_format;
    output_frames->width     = ctx->inputs[0]->w;
    output_frames->height    = ctx->inputs[0]->h;

    output_frames->initial_pool_size = 4;

    ret = ff_filter_init_hw_frames(ctx, link, 10);
    if (ret < 0)
        goto exit;

    ret = av_hwframe_ctx_init(l->hw_frames_ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to initialise MUSA frame "
               "context for output: %d\n", ret);
        goto exit;
    }

    ret = ff_yadif_config_output_common(link);
    if (ret < 0)
        goto exit;

    y->csp = av_pix_fmt_desc_get(output_frames->sw_format);
    y->filter = filter;

    ret = CHECK_MU(mu->muCtxPushCurrent(s->hwctx->musa_ctx));
    if (ret < 0)
        goto exit;

    ret = ff_musa_load_module(ctx, s->hwctx, &s->cu_module, ff_vf_yadif_musa_mu_data, ff_vf_yadif_musa_mu_len);
    if (ret < 0)
        goto exit;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_uchar, s->cu_module, "yadif_uchar"));
    if (ret < 0)
        goto exit;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_uchar2, s->cu_module, "yadif_uchar2"));
    if (ret < 0)
        goto exit;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_ushort, s->cu_module, "yadif_ushort"));
    if (ret < 0)
        goto exit;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_ushort2, s->cu_module, "yadif_ushort2"));
    if (ret < 0)
        goto exit;

exit:
    CHECK_MU(mu->muCtxPopCurrent(&dummy));

    return ret;
}

static const AVClass yadif_musa_class = {
    .class_name = "yadif_musa",
    .item_name  = av_default_item_name,
    .option     = ff_yadif_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static const AVFilterPad deint_musa_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = ff_yadif_filter_frame,
        .config_props  = config_input,
    },
};

static const AVFilterPad deint_musa_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = ff_yadif_request_frame,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_yadif_musa = {
    .p.name         = "yadif_musa",
    .p.description  = NULL_IF_CONFIG_SMALL("Deinterlace MUSA frames"),
    .p.priv_class   = &yadif_musa_class,
    .p.flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .priv_size      = sizeof(DeintMUSAContext),
    .uninit         = deint_musa_uninit,
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_MUSA),
    FILTER_INPUTS(deint_musa_inputs),
    FILTER_OUTPUTS(deint_musa_outputs),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
