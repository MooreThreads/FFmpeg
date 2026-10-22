/*
* Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_musa_internal.h"
#include "libavutil/musa_check.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"

#include "musa/load_helper.h"

#define CHECK_MU(x) FF_MUSA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

#define HIST_SIZE (3*256)
#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCKX 32
#define BLOCKY 16

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_YUV444P16,
};

struct thumb_frame {
    AVFrame *buf;               ///< cached frame
    int histogram[HIST_SIZE];   ///< RGB color distribution histogram of the frame
};

typedef struct ThumbnailMUSAContext {
    const AVClass *class;
    int n;                      ///< current frame
    int n_frames;               ///< number of frames for analysis
    struct thumb_frame *frames; ///< the n_frames frames
    AVRational tb;              ///< copy of the input timebase to ease access

    AVBufferRef *hw_frames_ctx;
    AVMUSADeviceContext *hwctx;

    MUmodule    cu_module;

    MUfunction  cu_func_uchar;
    MUfunction  cu_func_uchar2;
    MUfunction  cu_func_ushort;
    MUfunction  cu_func_ushort2;
    MUstream    cu_stream;

    MUdeviceptr data;

} ThumbnailMUSAContext;

#define OFFSET(x) offsetof(ThumbnailMUSAContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption thumbnail_musa_options[] = {
    { "n", "set the frames batch size", OFFSET(n_frames), AV_OPT_TYPE_INT, {.i64=100}, 2, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(thumbnail_musa);

static av_cold int init(AVFilterContext *ctx)
{
    ThumbnailMUSAContext *s = ctx->priv;

    s->frames = av_calloc(s->n_frames, sizeof(*s->frames));
    if (!s->frames) {
        av_log(ctx, AV_LOG_ERROR,
               "Allocation failure, try to lower the number of frames\n");
        return AVERROR(ENOMEM);
    }
    av_log(ctx, AV_LOG_VERBOSE, "batch size: %d frames\n", s->n_frames);
    return 0;
}

/**
 * @brief        Compute Sum-square deviation to estimate "closeness".
 * @param hist   color distribution histogram
 * @param median average color distribution histogram
 * @return       sum of squared errors
 */
static double frame_sum_square_err(const int *hist, const double *median)
{
    int i;
    double err, sum_sq_err = 0;

    for (i = 0; i < HIST_SIZE; i++) {
        err = median[i] - (double)hist[i];
        sum_sq_err += err*err;
    }
    return sum_sq_err;
}

static AVFrame *get_best_frame(AVFilterContext *ctx)
{
    AVFrame *picref;
    ThumbnailMUSAContext *s = ctx->priv;
    int i, j, best_frame_idx = 0;
    int nb_frames = s->n;
    double avg_hist[HIST_SIZE] = {0}, sq_err, min_sq_err = -1;

    // average histogram of the N frames
    for (j = 0; j < FF_ARRAY_ELEMS(avg_hist); j++) {
        for (i = 0; i < nb_frames; i++)
            avg_hist[j] += (double)s->frames[i].histogram[j];
        avg_hist[j] /= nb_frames;
    }

    // find the frame closer to the average using the sum of squared errors
    for (i = 0; i < nb_frames; i++) {
        sq_err = frame_sum_square_err(s->frames[i].histogram, avg_hist);
        if (i == 0 || sq_err < min_sq_err)
            best_frame_idx = i, min_sq_err = sq_err;
    }

    // free and reset everything (except the best frame buffer)
    for (i = 0; i < nb_frames; i++) {
        memset(s->frames[i].histogram, 0, sizeof(s->frames[i].histogram));
        if (i != best_frame_idx)
            av_frame_free(&s->frames[i].buf);
    }
    s->n = 0;

    // raise the chosen one
    picref = s->frames[best_frame_idx].buf;
    av_log(ctx, AV_LOG_INFO, "frame id #%d (pts_time=%f) selected "
           "from a set of %d images\n", best_frame_idx,
           picref->pts * av_q2d(s->tb), nb_frames);
    s->frames[best_frame_idx].buf = NULL;

    return picref;
}

static int thumbnail_kernel(AVFilterContext *ctx, MUfunction func, int channels,
    int *histogram, uint8_t *src_dptr, int src_width, int src_height, int src_pitch, int pixel_size)
{
    int ret;
    ThumbnailMUSAContext *s = ctx->priv;
    MusaFunctions *mu = s->hwctx->internal->cuda_dl;
    MUtexObject tex = 0;
    void *args[] = { &tex, &histogram, &src_width, &src_height };

    MUSA_TEXTURE_DESC tex_desc = {
        .filterMode = MU_TR_FILTER_MODE_LINEAR,
        .flags = MU_TRSF_READ_AS_INTEGER,
    };

    MUSA_RESOURCE_DESC res_desc = {
        .resType = MU_RESOURCE_TYPE_PITCH2D,
        .res.pitch2D.format = pixel_size == 1 ?
                              MU_AD_FORMAT_UNSIGNED_INT8 :
                              MU_AD_FORMAT_UNSIGNED_INT16,
        .res.pitch2D.numChannels = channels,
        .res.pitch2D.width = src_width,
        .res.pitch2D.height = src_height,
        .res.pitch2D.pitchInBytes = src_pitch,
        .res.pitch2D.devPtr = (MUdeviceptr)src_dptr,
    };

    ret = CHECK_MU(mu->muTexObjectCreate(&tex, &res_desc, &tex_desc, NULL));
    if (ret < 0)
        goto exit;

    ret = CHECK_MU(mu->muLaunchKernel(func,
                                      DIV_UP(src_width, BLOCKX), DIV_UP(src_height, BLOCKY), 1,
                                      BLOCKX, BLOCKY, 1, 0, s->cu_stream, args, NULL));
    if (ret >= 0)
        ret = CHECK_MU(mu->muStreamSynchronize(s->cu_stream));
exit:
    if (tex)
        CHECK_MU(mu->muTexObjectDestroy(tex));

    return ret;
}

static int thumbnail(AVFilterContext *ctx, int *histogram, AVFrame *in)
{
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext*)in->hw_frames_ctx->data;
    ThumbnailMUSAContext *s = ctx->priv;
    int ret;

    switch (in_frames_ctx->sw_format) {
    case AV_PIX_FMT_NV12:
        ret = thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_uchar2, 2,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 1);
        if (ret < 0)
            return ret;
        break;
    case AV_PIX_FMT_YUV420P:
        ret = thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 1);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 512, in->data[2], in->width / 2, in->height / 2, in->linesize[2], 1);
        if (ret < 0)
            return ret;
        break;
    case AV_PIX_FMT_YUV444P:
        ret = thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 256, in->data[1], in->width, in->height, in->linesize[1], 1);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 512, in->data[2], in->width, in->height, in->linesize[2], 1);
        if (ret < 0)
            return ret;
        break;
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_P016LE:
        ret = thumbnail_kernel(ctx, s->cu_func_ushort, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 2);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_ushort2, 2,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 2);
        if (ret < 0)
            return ret;
        break;
    case AV_PIX_FMT_YUV444P16:
        ret = thumbnail_kernel(ctx, s->cu_func_ushort2, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 2);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_ushort2, 1,
            histogram + 256, in->data[1], in->width, in->height, in->linesize[1], 2);
        if (ret < 0)
            return ret;
        ret = thumbnail_kernel(ctx, s->cu_func_ushort2, 1,
            histogram + 512, in->data[2], in->width, in->height, in->linesize[2], 2);
        if (ret < 0)
            return ret;
        break;
    default:
        return AVERROR_BUG;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx  = inlink->dst;
    ThumbnailMUSAContext *s   = ctx->priv;
    MusaFunctions *mu = s->hwctx->internal->cuda_dl;
    AVFilterLink *outlink = ctx->outputs[0];
    int *hist = s->frames[s->n].histogram;
    AVHWFramesContext *hw_frames_ctx = (AVHWFramesContext*)s->hw_frames_ctx->data;
    MUcontext dummy;
    MUSA_MEMCPY2D cpy = { 0 };
    int ret = 0;

    // keep a reference of each frame
    s->frames[s->n].buf = frame;

    ret = CHECK_MU(mu->muCtxPushCurrent(s->hwctx->musa_ctx));
    if (ret < 0)
        return ret;

    ret = CHECK_MU(mu->muMemsetD8Async(s->data, 0, HIST_SIZE * sizeof(int), s->cu_stream));
    if (ret < 0)
        goto fail;

    ret = thumbnail(ctx, (int*)s->data, frame);
    if (ret < 0)
        goto fail;

    cpy.srcMemoryType = MU_MEMORYTYPE_DEVICE;
    cpy.dstMemoryType = MU_MEMORYTYPE_HOST;
    cpy.srcDevice = s->data;
    cpy.dstHost = hist;
    cpy.srcPitch = HIST_SIZE * sizeof(int);
    cpy.dstPitch = HIST_SIZE * sizeof(int);
    cpy.WidthInBytes = HIST_SIZE * sizeof(int);
    cpy.Height = 1;

    ret = CHECK_MU(mu->muMemcpy2DAsync(&cpy, s->cu_stream));
    if (ret < 0)
        goto fail;

    ret = CHECK_MU(mu->muStreamSynchronize(s->cu_stream));
    if (ret < 0)
        goto fail;

    if (hw_frames_ctx->sw_format == AV_PIX_FMT_NV12 || hw_frames_ctx->sw_format == AV_PIX_FMT_YUV420P ||
        hw_frames_ctx->sw_format == AV_PIX_FMT_P010LE || hw_frames_ctx->sw_format == AV_PIX_FMT_P016LE)
    {
        int i;
        for (i = 256; i < HIST_SIZE; i++)
            hist[i] = 4 * hist[i];
    }

    ret = CHECK_MU(mu->muCtxPopCurrent(&dummy));
    if (ret < 0)
        return ret;

    // no selection until the buffer of N frames is filled up
    s->n++;
    if (s->n < s->n_frames)
        return 0;

    return ff_filter_frame(outlink, get_best_frame(ctx));

fail:
    CHECK_MU(mu->muCtxPopCurrent(&dummy));
    av_frame_free(&s->frames[s->n].buf);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ThumbnailMUSAContext *s = ctx->priv;

    if (s->hwctx) {
        MusaFunctions *mu = s->hwctx->internal->cuda_dl;
        MUcontext dummy;
        int ret;

        ret = CHECK_MU(mu->muCtxPushCurrent(s->hwctx->musa_ctx));
        if (ret >= 0) {
            CHECK_MU(mu->muStreamSynchronize(s->cu_stream));
            if (s->data) {
                CHECK_MU(mu->muMemFree(s->data));
                s->data = 0;
            }

            if (s->cu_module) {
                CHECK_MU(mu->muModuleUnload(s->cu_module));
                s->cu_module = NULL;
            }

            CHECK_MU(mu->muCtxPopCurrent(&dummy));
        }
    }

    if (s->frames) {
        for (int i = 0; i < s->n_frames; i++)
            av_frame_free(&s->frames[i].buf);
        av_freep(&s->frames);
    }

    av_buffer_unref(&s->hw_frames_ctx);
    s->hwctx = NULL;
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    ThumbnailMUSAContext *s = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->n) {
        ret = ff_filter_frame(link, get_best_frame(ctx));
        if (ret < 0)
            return ret;
        ret = AVERROR_EOF;
    }
    if (ret < 0)
        return ret;
    return 0;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FilterLink      *inl = ff_filter_link(inlink);
    FilterLink     *outl = ff_filter_link(ctx->outputs[0]);
    ThumbnailMUSAContext *s = ctx->priv;
    AVHWFramesContext     *hw_frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    AVMUSADeviceContext *device_hwctx = hw_frames_ctx->device_ctx->hwctx;
    MUcontext dummy, cuda_ctx = device_hwctx->musa_ctx;
    MusaFunctions *mu = device_hwctx->internal->cuda_dl;
    int ret, pop_ret;

    extern const unsigned char ff_vf_thumbnail_musa_mu_data[];
    extern const unsigned int ff_vf_thumbnail_musa_mu_len;

    if (!format_is_supported(hw_frames_ctx->sw_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n", av_get_pix_fmt_name(hw_frames_ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    /* Keep the device alive until our GPU resources have been released. */
    s->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
    if (!s->hw_frames_ctx)
        return AVERROR(ENOMEM);

    s->hwctx = device_hwctx;
    s->cu_stream = s->hwctx->stream;

    ret = CHECK_MU(mu->muCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_musa_load_module(ctx, device_hwctx, &s->cu_module, ff_vf_thumbnail_musa_mu_data, ff_vf_thumbnail_musa_mu_len);
    if (ret < 0)
        goto pop_ctx;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_uchar, s->cu_module, "Thumbnail_uchar"));
    if (ret < 0)
        goto pop_ctx;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_uchar2, s->cu_module, "Thumbnail_uchar2"));
    if (ret < 0)
        goto pop_ctx;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_ushort, s->cu_module, "Thumbnail_ushort"));
    if (ret < 0)
        goto pop_ctx;

    ret = CHECK_MU(mu->muModuleGetFunction(&s->cu_func_ushort2, s->cu_module, "Thumbnail_ushort2"));
    if (ret < 0)
        goto pop_ctx;

    ret = CHECK_MU(mu->muMemAlloc(&s->data, HIST_SIZE * sizeof(int)));
    if (ret < 0)
        goto pop_ctx;

pop_ctx:
    pop_ret = CHECK_MU(mu->muCtxPopCurrent(&dummy));
    if (ret < 0)
        return ret;
    if (pop_ret < 0)
        return pop_ret;

    outl->hw_frames_ctx = av_buffer_ref(s->hw_frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    s->tb = inlink->time_base;

    return 0;
}

static const AVFilterPad thumbnail_musa_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad thumbnail_musa_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const FFFilter ff_vf_thumbnail_musa = {
    .p.name        = "thumbnail_musa",
    .p.description = NULL_IF_CONFIG_SMALL("Select the most representative frame in a given sequence of consecutive frames using MUSA."),
    .p.priv_class  = &thumbnail_musa_class,
    .priv_size     = sizeof(ThumbnailMUSAContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(thumbnail_musa_inputs),
    FILTER_OUTPUTS(thumbnail_musa_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_MUSA),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
