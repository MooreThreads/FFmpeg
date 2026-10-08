/*
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

#include "libavutil/buffer.h"
#include "libavutil/hwcontext.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct MUSAUploadContext {
    const AVClass *class;
    int device_idx;

    AVBufferRef *hwdevice;
    AVBufferRef *hwframe;
} MUSAUploadContext;

static av_cold int musaupload_init(AVFilterContext *ctx)
{
    MUSAUploadContext *s = ctx->priv;
    char buf[64] = { 0 };

    snprintf(buf, sizeof(buf), "%d", s->device_idx);

    return av_hwdevice_ctx_create(&s->hwdevice, AV_HWDEVICE_TYPE_MUSA, buf, NULL, 0);
}

static av_cold void musaupload_uninit(AVFilterContext *ctx)
{
    MUSAUploadContext *s = ctx->priv;

    av_buffer_unref(&s->hwframe);
    av_buffer_unref(&s->hwdevice);
}

static int musaupload_query_formats(const AVFilterContext *ctx,
                                    AVFilterFormatsConfig **cfg_in,
                                    AVFilterFormatsConfig **cfg_out)
{
    int ret;

    static const enum AVPixelFormat input_pix_fmts[] = {
        AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_NV16, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_P010, AV_PIX_FMT_P016, AV_PIX_FMT_P210, AV_PIX_FMT_P216, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10,
        AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P10MSB, AV_PIX_FMT_YUV444P12MSB, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_0RGB32, AV_PIX_FMT_0BGR32, AV_PIX_FMT_RGB32, AV_PIX_FMT_BGR32,
#if CONFIG_VULKAN
        AV_PIX_FMT_VULKAN,
#endif
        AV_PIX_FMT_NONE,
    };
    static const enum AVPixelFormat output_pix_fmts[] = {
        AV_PIX_FMT_MUSA, AV_PIX_FMT_NONE,
    };
    AVFilterFormats *in_fmts  = ff_make_pixel_format_list(input_pix_fmts);
    AVFilterFormats *out_fmts;

    ret = ff_formats_ref(in_fmts, &cfg_in[0]->formats);
    if (ret < 0)
        return ret;

    out_fmts = ff_make_pixel_format_list(output_pix_fmts);

    ret = ff_formats_ref(out_fmts, &cfg_out[0]->formats);
    if (ret < 0)
        return ret;

    return 0;
}

static int musaupload_config_output(AVFilterLink *outlink)
{
    FilterLink     *outl = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    MUSAUploadContext *s = ctx->priv;

    AVHWFramesContext *hwframe_ctx;
    int ret;

    av_buffer_unref(&s->hwframe);
    s->hwframe = av_hwframe_ctx_alloc(s->hwdevice);
    if (!s->hwframe)
        return AVERROR(ENOMEM);

    hwframe_ctx            = (AVHWFramesContext*)s->hwframe->data;
    hwframe_ctx->format    = AV_PIX_FMT_MUSA;
    if (inl->hw_frames_ctx) {
        AVHWFramesContext *in_hwframe_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
        hwframe_ctx->sw_format = in_hwframe_ctx->sw_format;
    } else {
        hwframe_ctx->sw_format = inlink->format;
    }
    hwframe_ctx->width     = inlink->w;
    hwframe_ctx->height    = inlink->h;

    ret = av_hwframe_ctx_init(s->hwframe);
    if (ret < 0)
        return ret;

    outl->hw_frames_ctx = av_buffer_ref(s->hwframe);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static int musaupload_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext   *ctx = link->dst;
    AVFilterLink  *outlink = ctx->outputs[0];

    AVFrame *out = NULL;
    int ret;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    out->width  = in->width;
    out->height = in->height;

    ret = av_hwframe_transfer_data(out, in, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error transferring data to the GPU\n");
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(ctx->outputs[0], out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(MUSAUploadContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption musaupload_options[] = {
    { "device", "Number of the device to use", OFFSET(device_idx), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(musaupload);

static const AVFilterPad musaupload_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = musaupload_filter_frame,
    },
};

static const AVFilterPad musaupload_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = musaupload_config_output,
    },
};

const FFFilter ff_vf_hwupload_musa = {
    .p.name        = "hwupload_musa",
    .p.description = NULL_IF_CONFIG_SMALL("Upload a system memory frame to a MUSA device."),

    .p.priv_class  = &musaupload_class,

    .init      = musaupload_init,
    .uninit    = musaupload_uninit,

    .priv_size  = sizeof(MUSAUploadContext),

    FILTER_INPUTS(musaupload_inputs),
    FILTER_OUTPUTS(musaupload_outputs),

    FILTER_QUERY_FUNC2(musaupload_query_formats),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
