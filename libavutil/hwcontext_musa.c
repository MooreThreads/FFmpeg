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

#include "buffer.h"
#include "common.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "hwcontext_musa_internal.h"
#if CONFIG_VULKAN
#include "hwcontext_vulkan.h"
#endif
#include "musa_check.h"
#include "mem.h"
#include "pixdesc.h"
#include "pixfmt.h"
#include "imgutils.h"

typedef struct MUSAFramesContext {
    int shift_width, shift_height;
    int tex_alignment;
} MUSAFramesContext;

typedef struct MUSADeviceContext {
    AVMUSADeviceContext p;
    AVMUSADeviceContextInternal internal;
} MUSADeviceContext;

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV16,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_P210,
    AV_PIX_FMT_P216,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P10MSB,
    AV_PIX_FMT_YUV444P12MSB,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
    AV_PIX_FMT_RGB32,
    AV_PIX_FMT_BGR32,
#if CONFIG_VULKAN
    AV_PIX_FMT_VULKAN,
#endif
};

#define CHECK_MU(x) FF_MUSA_CHECK_DL(device_ctx, mu, x)

static int cuda_frames_get_constraints(AVHWDeviceContext *ctx,
                                       const void *hwconfig,
                                       AVHWFramesConstraints *constraints)
{
    int i;

    constraints->valid_sw_formats = av_malloc_array(FF_ARRAY_ELEMS(supported_formats) + 1,
                                                    sizeof(*constraints->valid_sw_formats));
    if (!constraints->valid_sw_formats)
        return AVERROR(ENOMEM);

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        constraints->valid_sw_formats[i] = supported_formats[i];
    constraints->valid_sw_formats[FF_ARRAY_ELEMS(supported_formats)] = AV_PIX_FMT_NONE;

    constraints->valid_hw_formats = av_malloc_array(2, sizeof(*constraints->valid_hw_formats));
    if (!constraints->valid_hw_formats)
        return AVERROR(ENOMEM);

    constraints->valid_hw_formats[0] = AV_PIX_FMT_MUSA;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    return 0;
}

static void cuda_buffer_free(void *opaque, uint8_t *data)
{
    AVHWFramesContext        *ctx = opaque;
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVMUSADeviceContext    *hwctx = device_ctx->hwctx;
    MusaFunctions             *mu = hwctx->internal->cuda_dl;

    MUcontext dummy;

    CHECK_MU(mu->muCtxPushCurrent(hwctx->musa_ctx));

    CHECK_MU(mu->muMemFree((MUdeviceptr)data));

    CHECK_MU(mu->muCtxPopCurrent(&dummy));
}

static AVBufferRef *cuda_pool_alloc(void *opaque, size_t size)
{
    AVHWFramesContext        *ctx = opaque;
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVMUSADeviceContext    *hwctx = device_ctx->hwctx;
    MusaFunctions             *mu = hwctx->internal->cuda_dl;

    AVBufferRef *ret = NULL;
    MUcontext dummy = NULL;
    MUdeviceptr data;
    int err;

    err = CHECK_MU(mu->muCtxPushCurrent(hwctx->musa_ctx));
    if (err < 0)
        return NULL;

    err = CHECK_MU(mu->muMemAlloc(&data, size));
    if (err < 0)
        goto fail;

    ret = av_buffer_create((uint8_t*)data, size, cuda_buffer_free, ctx, 0);
    if (!ret) {
        CHECK_MU(mu->muMemFree(data));
        goto fail;
    }

fail:
    CHECK_MU(mu->muCtxPopCurrent(&dummy));
    return ret;
}

static int cuda_frames_init(AVHWFramesContext *ctx)
{
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVMUSADeviceContext    *hwctx = device_ctx->hwctx;
    MUSAFramesContext       *priv = ctx->hwctx;
    MusaFunctions             *mu = hwctx->internal->cuda_dl;
    int err, i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++) {
        if (ctx->sw_format == supported_formats[i])
            break;
    }
    if (i == FF_ARRAY_ELEMS(supported_formats)) {
        av_log(ctx, AV_LOG_ERROR, "Pixel format '%s' is not supported\n",
               av_get_pix_fmt_name(ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    err = CHECK_MU(mu->muDeviceGetAttribute(&priv->tex_alignment,
                                            14 /* MU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT */,
                                            hwctx->internal->cuda_device));
    if (err < 0)
        return err;

    av_log(ctx, AV_LOG_DEBUG, "CUDA texture alignment: %d\n", priv->tex_alignment);

    // YUV420P is a special case.
    // Since nvenc expects the U/V planes to have half the linesize of the Y plane
    // alignment has to be doubled to ensure the U/V planes still end up aligned.
    if (ctx->sw_format == AV_PIX_FMT_YUV420P)
        priv->tex_alignment *= 2;

    av_pix_fmt_get_chroma_sub_sample(ctx->sw_format, &priv->shift_width, &priv->shift_height);

    if (!ctx->pool) {
        int size = av_image_get_buffer_size(ctx->sw_format, ctx->width, ctx->height, priv->tex_alignment);
        if (size < 0)
            return size;

        ffhwframesctx(ctx)->pool_internal =
            av_buffer_pool_init2(size, ctx, cuda_pool_alloc, NULL);
        if (!ffhwframesctx(ctx)->pool_internal)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int cuda_get_buffer(AVHWFramesContext *ctx, AVFrame *frame)
{
    MUSAFramesContext *priv = ctx->hwctx;
    int res;

    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    res = av_image_fill_arrays(frame->data, frame->linesize, frame->buf[0]->data,
                               ctx->sw_format, ctx->width, ctx->height, priv->tex_alignment);
    if (res < 0)
        return res;

    // YUV420P is a special case.
    // Nvenc expects the U/V planes in swapped order from how ffmpeg expects them, also chroma is half-aligned
    if (ctx->sw_format == AV_PIX_FMT_YUV420P) {
        frame->linesize[1] = frame->linesize[2] = frame->linesize[0] / 2;
        frame->data[2]     = frame->data[1];
        frame->data[1]     = frame->data[2] + frame->linesize[2] * (ctx->height / 2);
    }

    frame->format = AV_PIX_FMT_MUSA;
    frame->width  = ctx->width;
    frame->height = ctx->height;

    return 0;
}

static int cuda_transfer_get_formats(AVHWFramesContext *ctx,
                                     enum AVHWFrameTransferDirection dir,
                                     enum AVPixelFormat **formats)
{
    enum AVPixelFormat *fmts;

    fmts = av_malloc_array(2, sizeof(*fmts));
    if (!fmts)
        return AVERROR(ENOMEM);

    fmts[0] = ctx->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;

    return 0;
}

static int cuda_transfer_data(AVHWFramesContext *ctx, AVFrame *dst,
                                 const AVFrame *src)
{
    MUSAFramesContext       *priv = ctx->hwctx;
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVMUSADeviceContext    *hwctx = device_ctx->hwctx;
    MusaFunctions             *mu = hwctx->internal->cuda_dl;

    MUcontext dummy;
    int i, ret;

    if ((src->hw_frames_ctx && ((AVHWFramesContext*)src->hw_frames_ctx->data)->format != AV_PIX_FMT_MUSA) ||
        (dst->hw_frames_ctx && ((AVHWFramesContext*)dst->hw_frames_ctx->data)->format != AV_PIX_FMT_MUSA))
        return AVERROR(ENOSYS);

    av_log(ctx, AV_LOG_DEBUG,
           "MUSA transfer src=%s dst=%s src_hw=%p dst_hw=%p src_buf=%p dst_buf=%p\n",
           av_get_pix_fmt_name(src->format), av_get_pix_fmt_name(dst->format),
           src->hw_frames_ctx ? src->hw_frames_ctx->data : NULL,
           dst->hw_frames_ctx ? dst->hw_frames_ctx->data : NULL,
           src->buf[0], dst->buf[0]);
    for (i = 0; i < FF_ARRAY_ELEMS(src->data); i++) {
        if (!src->data[i] && !dst->data[i])
            continue;
        av_log(ctx, AV_LOG_DEBUG,
               "  plane %d: src_data=%p src_linesize=%d dst_data=%p dst_linesize=%d\n",
               i, src->data[i], src->linesize[i], dst->data[i], dst->linesize[i]);
    }

    ret = CHECK_MU(mu->muCtxPushCurrent(hwctx->musa_ctx));
    if (ret < 0)
        return ret;

    for (i = 0; i < FF_ARRAY_ELEMS(src->data) && src->data[i]; i++) {
        MUSA_MEMCPY2D cpy = {
            .srcPitch      = src->linesize[i],
            .dstPitch      = dst->linesize[i],
            .WidthInBytes  = FFMIN(src->linesize[i], dst->linesize[i]),
            .Height        = src->height >> ((i == 0 || i == 3) ? 0 : priv->shift_height),
        };

        if (src->hw_frames_ctx) {
            cpy.srcMemoryType = MU_MEMORYTYPE_DEVICE;
            cpy.srcDevice     = (MUdeviceptr)src->data[i];
        } else {
            cpy.srcMemoryType = MU_MEMORYTYPE_HOST;
            cpy.srcHost       = src->data[i];
        }

        if (dst->hw_frames_ctx) {
            cpy.dstMemoryType = MU_MEMORYTYPE_DEVICE;
            cpy.dstDevice     = (MUdeviceptr)dst->data[i];
        } else {
            cpy.dstMemoryType = MU_MEMORYTYPE_HOST;
            cpy.dstHost       = dst->data[i];
        }

        ret = CHECK_MU(mu->muMemcpy2DAsync(&cpy, hwctx->stream));
        if (ret < 0)
            goto exit;
    }

    if (!dst->hw_frames_ctx) {
        ret = CHECK_MU(mu->muStreamSynchronize(hwctx->stream));
        if (ret < 0)
            goto exit;
    }

exit:
    CHECK_MU(mu->muCtxPopCurrent(&dummy));

    return ret;
}

static int cuda_map_frame(AVHWFramesContext *ctx, AVFrame *dst,
                          const AVFrame *src, int flags)
{
    AVHWFramesContext *src_ctx;
    int i, ret;

    if ((flags & AV_HWFRAME_MAP_DIRECT) && dst->format != AV_PIX_FMT_MUSA)
        return AVERROR(ENOSYS);

    if (!src->hw_frames_ctx || src->format != AV_PIX_FMT_MUSA)
        return AVERROR(ENOSYS);

    src_ctx = (AVHWFramesContext*)src->hw_frames_ctx->data;
    if (src_ctx->format != AV_PIX_FMT_MUSA || dst->format != AV_PIX_FMT_MUSA)
        return AVERROR(ENOSYS);

    if (src_ctx->device_ctx != ctx->device_ctx)
        return AVERROR(ENOSYS);

    for (i = 0; i < FF_ARRAY_ELEMS(src->data); i++) {
        dst->data[i]     = src->data[i];
        dst->linesize[i] = src->linesize[i];
    }

    dst->width  = src->width;
    dst->height = src->height;

    ret = ff_hwframe_map_create(src->hw_frames_ctx, dst, src, NULL, NULL);
    if (ret < 0)
        return ret;

    return av_frame_copy_props(dst, src);
}

static int cuda_map_from(AVHWFramesContext *ctx, AVFrame *dst,
                         const AVFrame *src, int flags)
{
    return cuda_map_frame(ctx, dst, src, flags);
}

static int cuda_map_to(AVHWFramesContext *ctx, AVFrame *dst,
                       const AVFrame *src, int flags)
{
    return cuda_map_frame(ctx, dst, src, flags);
}

static int cuda_frames_derive(AVHWFramesContext *dst_ctx,
                              AVHWFramesContext *src_ctx, int flags)
{
    MUSAFramesContext *dst = dst_ctx->hwctx;
    MUSAFramesContext *src = src_ctx->hwctx;

    if (dst_ctx->format != AV_PIX_FMT_MUSA || src_ctx->format != AV_PIX_FMT_MUSA)
        return AVERROR(ENOSYS);

    if (dst_ctx->device_ctx != src_ctx->device_ctx)
        return AVERROR(ENOSYS);

    dst->shift_width   = src->shift_width;
    dst->shift_height  = src->shift_height;
    dst->tex_alignment = src->tex_alignment;

    return 0;
}

static void cuda_device_uninit(AVHWDeviceContext *device_ctx)
{
    MUSADeviceContext *hwctx = device_ctx->hwctx;

    if (hwctx->p.internal) {
        MusaFunctions *mu = hwctx->internal.cuda_dl;

        if (hwctx->internal.is_allocated && hwctx->p.musa_ctx) {
            if (hwctx->internal.flags & AV_MUSA_USE_PRIMARY_CONTEXT)
                CHECK_MU(mu->muDevicePrimaryCtxRelease(hwctx->internal.cuda_device));
            else if (!(hwctx->internal.flags & AV_MUSA_USE_CURRENT_CONTEXT))
                CHECK_MU(mu->muCtxDestroy(hwctx->p.musa_ctx));

            hwctx->p.musa_ctx = NULL;
        }

        musa_free_functions(&hwctx->internal.cuda_dl);
        memset(&hwctx->internal, 0, sizeof(hwctx->internal));
        hwctx->p.internal = NULL;
    }
}

static int cuda_device_init(AVHWDeviceContext *ctx)
{
    MUSADeviceContext *hwctx = ctx->hwctx;
    int ret;

    hwctx->p.internal = &hwctx->internal;

    if (!hwctx->internal.cuda_dl) {
        ret = musa_load_functions(&hwctx->internal.cuda_dl, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Could not dynamically load CUDA\n");
            goto error;
        }
    }

    return 0;

error:
    cuda_device_uninit(ctx);
    return ret;
}

static int cuda_context_init(AVHWDeviceContext *device_ctx, int flags) {
    AVMUSADeviceContext *hwctx = device_ctx->hwctx;
    MusaFunctions *mu;
    MUcontext dummy;
    int ret, dev_active = 0;
    unsigned int dev_flags = 0;

    const unsigned int desired_flags = MU_CTX_SCHED_BLOCKING_SYNC;

    mu = hwctx->internal->cuda_dl;

    hwctx->internal->flags = flags;

    if (flags & AV_MUSA_USE_PRIMARY_CONTEXT) {
        ret = CHECK_MU(mu->muDevicePrimaryCtxGetState(hwctx->internal->cuda_device,
                       &dev_flags, &dev_active));
        if (ret < 0)
            return ret;

        if (dev_active && dev_flags != desired_flags) {
            av_log(device_ctx, AV_LOG_ERROR, "Primary context already active with incompatible flags.\n");
            return AVERROR(ENOTSUP);
        } else if (dev_flags != desired_flags) {
            ret = CHECK_MU(mu->muDevicePrimaryCtxSetFlags(hwctx->internal->cuda_device,
                           desired_flags));
            if (ret < 0)
                return ret;
        }

        ret = CHECK_MU(mu->muDevicePrimaryCtxRetain(&hwctx->musa_ctx,
                                                    hwctx->internal->cuda_device));
        if (ret < 0)
            return ret;
    } else if (flags & AV_MUSA_USE_CURRENT_CONTEXT) {
        ret = CHECK_MU(mu->muCtxGetCurrent(&hwctx->musa_ctx));
        if (ret < 0)
            return ret;
        av_log(device_ctx, AV_LOG_INFO, "Using current CUDA context.\n");
    } else {
        ret = CHECK_MU(mu->muCtxCreate(&hwctx->musa_ctx, desired_flags,
                                       hwctx->internal->cuda_device));
        if (ret < 0)
            return ret;

        CHECK_MU(mu->muCtxPopCurrent(&dummy));
    }

    hwctx->internal->is_allocated = 1;

    // Setting stream to NULL will make functions automatically use the default MUstream
    hwctx->stream = NULL;

    return 0;
}

static int cuda_flags_from_opts(AVHWDeviceContext *device_ctx,
                                AVDictionary *opts, int *flags)
{
    AVDictionaryEntry *primary_ctx_opt = av_dict_get(opts, "primary_ctx", NULL, 0);
    AVDictionaryEntry *current_ctx_opt = av_dict_get(opts, "current_ctx", NULL, 0);

    int use_primary_ctx = 0, use_current_ctx = 0;
    if (primary_ctx_opt)
        use_primary_ctx = strtol(primary_ctx_opt->value, NULL, 10);

    if (current_ctx_opt)
        use_current_ctx = strtol(current_ctx_opt->value, NULL, 10);

    if (use_primary_ctx && use_current_ctx) {
        av_log(device_ctx, AV_LOG_ERROR, "Requested both primary and current CUDA context simultaneously.\n");
        return AVERROR(EINVAL);
    }

    if (primary_ctx_opt && use_primary_ctx) {
        av_log(device_ctx, AV_LOG_VERBOSE, "Using CUDA primary device context\n");
        *flags |= AV_MUSA_USE_PRIMARY_CONTEXT;
    } else if (primary_ctx_opt) {
        av_log(device_ctx, AV_LOG_VERBOSE, "Disabling use of CUDA primary device context\n");
        *flags &= ~AV_MUSA_USE_PRIMARY_CONTEXT;
    }

    if (current_ctx_opt && use_current_ctx) {
        av_log(device_ctx, AV_LOG_VERBOSE, "Using CUDA current device context\n");
        *flags |= AV_MUSA_USE_CURRENT_CONTEXT;
    } else if (current_ctx_opt) {
        av_log(device_ctx, AV_LOG_VERBOSE, "Disabling use of CUDA current device context\n");
        *flags &= ~AV_MUSA_USE_CURRENT_CONTEXT;
    }

    return 0;
}

static int cuda_device_create(AVHWDeviceContext *device_ctx,
                              const char *device,
                              AVDictionary *opts, int flags)
{
    AVMUSADeviceContext *hwctx = device_ctx->hwctx;
    MusaFunctions *mu;
    int ret, device_idx = 0;

    ret = cuda_flags_from_opts(device_ctx, opts, &flags);
    if (ret < 0)
        goto error;

    if (device)
        device_idx = strtol(device, NULL, 0);

    ret = cuda_device_init(device_ctx);
    if (ret < 0)
        goto error;

    mu = hwctx->internal->cuda_dl;

    ret = CHECK_MU(mu->muInit(0));
    if (ret < 0)
        goto error;

    ret = CHECK_MU(mu->muDeviceGet(&hwctx->internal->cuda_device, device_idx));
    if (ret < 0)
        goto error;

    ret = cuda_context_init(device_ctx, flags);
    if (ret < 0)
        goto error;

    return 0;

error:
    cuda_device_uninit(device_ctx);
    return ret;
}

static int cuda_device_derive(AVHWDeviceContext *device_ctx,
                              AVHWDeviceContext *src_ctx, AVDictionary *opts,
                              int flags) {
    AVMUSADeviceContext *hwctx = device_ctx->hwctx;
    MusaFunctions *mu;
    const char *src_uuid = NULL;
#if CONFIG_VULKAN
    VkPhysicalDeviceIDProperties vk_idp;
#endif
    int ret, i, device_count;

    ret = cuda_flags_from_opts(device_ctx, opts, &flags);
    if (ret < 0)
        goto error;

#if CONFIG_VULKAN
    vk_idp = (VkPhysicalDeviceIDProperties) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
    };
#endif

    switch (src_ctx->type) {
#if CONFIG_VULKAN
#define TYPE PFN_vkGetPhysicalDeviceProperties2
    case AV_HWDEVICE_TYPE_VULKAN: {
        AVVulkanDeviceContext *vkctx = src_ctx->hwctx;
        TYPE prop_fn = (TYPE)vkctx->get_proc_addr(vkctx->inst, "vkGetPhysicalDeviceProperties2");
        VkPhysicalDeviceProperties2 vk_dev_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &vk_idp,
        };
        prop_fn(vkctx->phys_dev, &vk_dev_props);
        src_uuid = vk_idp.deviceUUID;
        break;
    }
#undef TYPE
#endif
    default:
        ret = AVERROR(ENOSYS);
        goto error;
    }

    if (!src_uuid) {
        av_log(device_ctx, AV_LOG_ERROR,
               "Failed to get UUID of source device.\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    ret = cuda_device_init(device_ctx);
    if (ret < 0)
        goto error;

    mu = hwctx->internal->cuda_dl;

    ret = CHECK_MU(mu->muInit(0));
    if (ret < 0)
        goto error;

    ret = CHECK_MU(mu->muDeviceGetCount(&device_count));
    if (ret < 0)
        goto error;

    hwctx->internal->cuda_device = -1;
    for (i = 0; i < device_count; i++) {
        MUdevice dev;
        MUuuid uuid;

        ret = CHECK_MU(mu->muDeviceGet(&dev, i));
        if (ret < 0)
            goto error;

        ret = CHECK_MU(mu->muDeviceGetUuid(&uuid, dev));
        if (ret < 0)
            goto error;

        if (memcmp(src_uuid, uuid.bytes, sizeof (uuid.bytes)) == 0) {
            hwctx->internal->cuda_device = dev;
            break;
        }
    }

    if (hwctx->internal->cuda_device == -1) {
        av_log(device_ctx, AV_LOG_ERROR, "Could not derive CUDA device.\n");
        goto error;
    }

    ret = cuda_context_init(device_ctx, flags);
    if (ret < 0)
        goto error;

    return 0;

error:
    cuda_device_uninit(device_ctx);
    return ret;
}

const HWContextType ff_hwcontext_type_musa = {
    .type                 = AV_HWDEVICE_TYPE_MUSA,
    .name                 = "MUSA",

    .device_hwctx_size    = sizeof(MUSADeviceContext),
    .frames_hwctx_size    = sizeof(MUSAFramesContext),

    .device_create        = cuda_device_create,
    .device_derive        = cuda_device_derive,
    .device_init          = cuda_device_init,
    .device_uninit        = cuda_device_uninit,
    .frames_get_constraints = cuda_frames_get_constraints,
    .frames_init          = cuda_frames_init,
    .frames_get_buffer    = cuda_get_buffer,
    .transfer_get_formats = cuda_transfer_get_formats,
    .transfer_data_to     = cuda_transfer_data,
    .transfer_data_from   = cuda_transfer_data,
    .map_to               = cuda_map_to,
    .map_from             = cuda_map_from,
    .frames_derive_to     = cuda_frames_derive,
    .frames_derive_from   = cuda_frames_derive,

    .pix_fmts             = (const enum AVPixelFormat[]){ AV_PIX_FMT_MUSA, AV_PIX_FMT_NONE },
};
