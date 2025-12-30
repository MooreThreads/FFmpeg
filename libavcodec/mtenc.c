/*
 * H.264/HEVC hardware encoding using moore threads mtenc
 * Copyright (c) 2022 MooreThreads
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

#include "config.h"
#include "config_components.h"

#include "mtenc.h"
#include "hevc_sei.h"

#if CONFIG_AV1_MTENC_ENCODER
#include "av1.h"
#endif

#include "compat/w32dlfcn.h"
#include "libavutil/pixfmt.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "atsc_a53.h"
#include "codec_desc.h"
#include "encode.h"
#include "internal.h"
#include "packet_internal.h"

#if CONFIG_D3D11VA
#include "libavutil/thread.h"

typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID riid, void **ppFactory);
static AVOnce functions_loaded = AV_ONCE_INIT;
static PFN_CREATE_DXGI_FACTORY mCreateDXGIFactory;
static PFN_D3D11_CREATE_DEVICE mD3D11CreateDevice;
static const uint32_t MT_VENDOR_ID = 0x1ed5;
#endif

const enum AVPixelFormat ff_mtenc_pix_fmts[] = {
    //AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    //AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010LE,
#if CONFIG_D3D11VA
    AV_PIX_FMT_D3D11,
#endif
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_NONE
};

const AVCodecHWConfigInternal *const ff_mtenc_hw_configs[] = {
#if CONFIG_D3D11VA
    HW_CONFIG_ENCODER_FRAMES(D3D11, D3D11VA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  D3D11VA),
#endif
    NULL,
};

static const struct {
    MTENCSTATUS mterr;
    int         averr;
    const char *desc;
} mtenc_errors[] = {
    { MT_ENC_SUCCESS,                      0,                "success"                  },
    { MT_ENC_ERR_NO_ENCODE_DEVICE,         AVERROR(ENOENT),  "no encode device"         },
    { MT_ENC_ERR_UNSUPPORTED_DEVICE,       AVERROR(ENOSYS),  "unsupported device"       },
    { MT_ENC_ERR_INVALID_PARAM,            AVERROR(EINVAL),  "invalid param"            },
    { MT_ENC_ERR_OUT_OF_MEMORY,            AVERROR(ENOMEM),  "out of memory"            },
    { MT_ENC_ERR_ENCODER_NOT_INITIALIZED,  AVERROR(EINVAL),  "encoder not initialized"  },
    { MT_ENC_ERR_INVALID_VERSION,          AVERROR(EINVAL),  "invalid version"          },
    { MT_ENC_ERR_MAP_FAILED,               AVERROR(EIO),     "map failed"               },
    { MT_ENC_ERR_NEED_MORE_INPUT,          AVERROR(EAGAIN),  "need more input"          },
    { MT_ENC_ERR_ENCODER_BUSY,             AVERROR(EAGAIN),  "encoder busy"             },
    { MT_ENC_ERR_GENERIC,                  AVERROR_UNKNOWN,  "generic error"            },
    { MT_ENC_ERR_RESOURCE_NOT_MAPPED,      AVERROR(EBADF),   "resource not mapped"      },
};

#if CONFIG_D3D11VA
static void load_functions(void)
{
    HANDLE d3dlib, dxgilib;
    // We let these "leak" - this is fine, as unloading has no great benefit, and
    // Windows will mark a DLL as loaded forever if its internal refcount overflows
    // from too many LoadLibrary calls.
    d3dlib  = dlopen("d3d11.dll", 0);
    dxgilib = dlopen("dxgi.dll", 0);
    if (!d3dlib || !dxgilib)
    {
        return;
    }

    mD3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE) GetProcAddress(d3dlib, "D3D11CreateDevice");
    mCreateDXGIFactory = (PFN_CREATE_DXGI_FACTORY) GetProcAddress(dxgilib, "CreateDXGIFactory1");
}

static int hw_device_create(AVCodecContext *avctx)
{
    IDXGIFactory1 *pDXGIFactory;
    IDXGIAdapter *pAdapter = NULL;
    HRESULT hr;
    MtencContext *ctx = avctx->priv_data;
    if (ff_thread_once(&functions_loaded, load_functions) != 0)
    {
        return AVERROR_UNKNOWN;
    }

    if (!mD3D11CreateDevice || !mCreateDXGIFactory)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to load D3D11 library or its functions\n");
        return AVERROR_UNKNOWN;
    }

    if (!ctx->d3d11vaDeviceContext)
    {
        ctx->d3d11vaDeviceContext = av_mallocz(sizeof(AVD3D11VADeviceContext));
        if (!ctx->d3d11vaDeviceContext)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to alloc mem for AVD3D11VADeviceContext\n");
            return AVERROR(ENOMEM);
        }
    }

    hr = mCreateDXGIFactory(&IID_IDXGIFactory1, (void **)&pDXGIFactory);
    if (FAILED(hr))
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to create dxgi factory (%lx)\n", (long)hr);
        return AVERROR_UNKNOWN;
    }

    for (int i = 0; IDXGIFactory1_EnumAdapters(pDXGIFactory, i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC desc;
        hr = IDXGIAdapter_GetDesc(pAdapter, &desc);
        if (!FAILED(hr))
        {
            av_log(avctx, AV_LOG_DEBUG, "Find device %d - %04x:%04x (%ls).\n",
                i, desc.VendorId, desc.DeviceId, desc.Description);
            if (desc.VendorId == MT_VENDOR_ID)
            {
                av_log(avctx, AV_LOG_INFO, "Using device %d - %04x:%04x (%ls).\n",
                    i, desc.VendorId, desc.DeviceId, desc.Description);
                break;
            }
        }
        IDXGIAdapter_Release(pAdapter);
        pAdapter = NULL;
    }
    IDXGIFactory1_Release(pDXGIFactory);
    pDXGIFactory = NULL;

    if (!pAdapter)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to find MT GPU by vendor id:%04x\n", MT_VENDOR_ID);
        return AVERROR_UNKNOWN;
    }

    hr = mD3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN , NULL, 0, NULL, 0,
                   D3D11_SDK_VERSION, &ctx->d3d11vaDeviceContext->device, NULL, &ctx->d3d11vaDeviceContext->device_context);

    IDXGIAdapter_Release(pAdapter);
    pAdapter = NULL;

    if (FAILED(hr))
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to create Direct3D device (%lx)\n", (long)hr);
        return AVERROR_UNKNOWN;
    }
    return 0;
}

static void hw_device_destroy(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    if (ctx->d3d11vaDeviceContext)
    {
        if (ctx->d3d11vaDeviceContext->device)
        {
            ID3D11Device_Release(ctx->d3d11vaDeviceContext->device);
            ctx->d3d11vaDeviceContext->device = NULL;
        }
        if (ctx->d3d11vaDeviceContext->device_context)
        {
            ID3D11DeviceContext_Release(ctx->d3d11vaDeviceContext->device_context);
            ctx->d3d11vaDeviceContext->device_context = NULL;
        }
        av_freep(&ctx->d3d11vaDeviceContext);
    }
}
#endif

static int mtenc_map_error(MTENCSTATUS err, const char **desc)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(mtenc_errors); i++) {
        if (mtenc_errors[i].mterr == err) {
            if (desc)
                *desc = mtenc_errors[i].desc;
            return mtenc_errors[i].averr;
        }
    }

    if (desc)
        *desc = "unknown error";

    return AVERROR_UNKNOWN;
}

static int mtenc_print_error(AVCodecContext *avctx, MTENCSTATUS err, const char *error_string)
{
    const char *desc;
    int ret = mtenc_map_error(err, &desc);

    av_log(avctx, AV_LOG_ERROR, "%s: %s (%d)\n", error_string, desc, err);

    return ret;
}

static void mtenc_print_driver_requirement(AVCodecContext *avctx, int level)
{
#if MTENCAPI_CHECK_VERSION(1, 1)
    const char *minver = "(unknown)";
#elif MTENCAPI_CHECK_VERSION(0, 0)
    const char *minver = "0.0";
#endif

    av_log(avctx, level, "The minimum required mt driver for mtenc is %s or newer\n", minver);
}

static av_cold int mtenc_load_functions(MtencDynLoadFunctions *pFunc, AVCodecContext *avctx)
{
    if(!pFunc || !avctx)
    {
        return mtenc_print_error(avctx, MT_ENC_ERR_INVALID_PARAM, "Input context error.");
    }

# if defined(_WIN32)
# if defined(_WIN64)
    pFunc->mtenc_module = LoadLibrary(TEXT("mtencodeapi64.dll"));
#else
    pFunc->mtenc_module = LoadLibrary(TEXT("mtencodeapi32.dll"));
#endif
#else
    pFunc->mtenc_module = dlopen("libencode_musa.so", RTLD_LAZY);
#endif
    if(!pFunc->mtenc_module)
    {
        return mtenc_print_error(avctx, MT_ENC_ERR_INVALID_PARAM, "Failed to loaded mtenc library.");
    }
    av_log(avctx, AV_LOG_VERBOSE, "mtenc_module: %p\n", pFunc->mtenc_module);

# if defined(_WIN32)
    pFunc->mtenc_dl.pMtEncodeAPIGetMaxSupportedVersion =
        (MTEncodeAPIGetMaxSupportedVersion_Type)GetProcAddress(pFunc->mtenc_module, "MTEncodeAPIGetMaxSupportedVersion");
    pFunc->mtenc_dl.pMTEncodeAPICreateInstance =
        (MTEncodeAPICreateInstance_Type)GetProcAddress(pFunc->mtenc_module, "MTEncodeAPICreateInstance");
#else
    pFunc->mtenc_dl.pMtEncodeAPIGetMaxSupportedVersion =
        (MTEncodeAPIGetMaxSupportedVersion_Type)dlsym(pFunc->mtenc_module, "MTEncodeAPIGetMaxSupportedVersion");
    pFunc->mtenc_dl.pMTEncodeAPICreateInstance =
        (MTEncodeAPICreateInstance_Type)dlsym(pFunc->mtenc_module, "MTEncodeAPICreateInstance");
#endif
    if(!pFunc->mtenc_dl.pMtEncodeAPIGetMaxSupportedVersion || !pFunc->mtenc_dl.pMTEncodeAPICreateInstance)
    {
        return mtenc_print_error(avctx, MT_ENC_ERR_INVALID_PARAM, "Failed to loaded mtenc functions.");
    }

    return 0;
}

static av_cold int mtenc_load_libraries(AVCodecContext *avctx)
{
    MtencContext *ctx            = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MTENCSTATUS err;
    uint32_t mtenc_max_ver;
    int ret;

    ret = mtenc_load_functions(dl_fn, avctx);
    if (ret < 0) {
        mtenc_print_driver_requirement(avctx, AV_LOG_ERROR);
        return ret;
    }

    err = dl_fn->mtenc_dl.pMtEncodeAPIGetMaxSupportedVersion(&mtenc_max_ver);
    if (err != MT_ENC_SUCCESS)
        return mtenc_print_error(avctx, err, "Failed to query mtenc max version");

    av_log(avctx, AV_LOG_VERBOSE, "support max mtenc version %d.%d.%d [0x%X]\n",
           (mtenc_max_ver >> 24) & 0xFF,
           (mtenc_max_ver >> 16) & 0xFF,
           mtenc_max_ver & 0xFFFF,
           mtenc_max_ver);

    if (((MTENCAPI_MAJOR_VERSION << 24) | (MTENCAPI_MINOR_VERSION << 16)) > mtenc_max_ver) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support the required mtenc API version. "
               "Required: %d.%d Found: %d.%d\n",
               MTENCAPI_MAJOR_VERSION, MTENCAPI_MINOR_VERSION,
               mtenc_max_ver >> 4, mtenc_max_ver & 0xf);
        mtenc_print_driver_requirement(avctx, AV_LOG_ERROR);
        return AVERROR(ENOSYS);
    }

    dl_fn->mtenc_funcs.version = MTENCAPI_VERSION;

    err = dl_fn->mtenc_dl.pMTEncodeAPICreateInstance(&dl_fn->mtenc_funcs);
    if (err != MT_ENC_SUCCESS)
        return mtenc_print_error(avctx, err, "Failed to create mtenc instance");

    av_log(avctx, AV_LOG_VERBOSE, "mtenc initialized successfully\n");

    return 0;
}

static av_cold int mtenc_open_session(AVCodecContext *avctx)
{
    MT_ENC_CREATE_ENCODER_PARAMS params = { MTENCAPI_VERSION };
    MtencContext *ctx = avctx->priv_data;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &ctx->mtenc_dload_funcs.mtenc_funcs;
    MTENCSTATUS ret;

    av_log(avctx, AV_LOG_VERBOSE, "ctx->d3d11_device: %p\n", ctx->d3d11_device);
    if(ctx->d3d11_device)
    {
        params.device     = ctx->d3d11_device;
        params.deviceType = MT_ENC_DEVICE_TYPE_DIRECTX;
    } else {
        params.device     = NULL;
        params.deviceType = MT_ENC_DEVICE_TYPE_OPENGL;
    }


    ret = p_mtenc->mtEncCreateEncoder(&params, &ctx->mtencoder);
    if (ret != MT_ENC_SUCCESS) {
        ctx->mtencoder = NULL;
        return mtenc_print_error(avctx, ret, "OpenEncodeSession failed");
    }

    av_log(avctx, AV_LOG_VERBOSE, "open ctx->mtencoder: %p\n", ctx->mtencoder);

    return 0;
}

static av_cold int mtenc_setup_device(AVCodecContext *avctx)
 {
    MtencContext *ctx = avctx->priv_data;
#if CONFIG_D3D11VA
    AVD3D11VADeviceContext *d3d11_device_hwctx = NULL;
#endif
    int ret = 0;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        ctx->init_encode_params.encodeID = MT_ENC_CODEC_ID_H264;
        break;
    case AV_CODEC_ID_HEVC:
        ctx->init_encode_params.encodeID = MT_ENC_CODEC_ID_HEVC;
        break;
#if CONFIG_AV1_MTENC_ENCODER
    case AV_CODEC_ID_AV1:
        ctx->init_encode_params.encodeID = MT_ENC_CODEC_ID_AV1;
        break;
#endif
    default:
        return AVERROR_BUG;
    }

    ctx->init_encode_params.presetID = MT_ENC_PRESET_ID_DEFAULT;

    if (ctx->flags & MTENC_DEPRECATED_PRESET)
        av_log(avctx, AV_LOG_WARNING, "The selected preset is deprecated. Use p1 to p7 + -tune or fast/medium/slow.\n");

    av_log(avctx, AV_LOG_INFO, "avctx->pix_fmt=%d(%s), avctx->hw_frames_ctx = 0x%p, avctx->hw_device_ctx = 0x%p.\n",
           avctx->pix_fmt, av_get_pix_fmt_name(avctx->pix_fmt), avctx->hw_frames_ctx, avctx->hw_device_ctx);

#if CONFIG_D3D11VA
    if (avctx->pix_fmt != AV_PIX_FMT_NV12 && avctx->pix_fmt != AV_PIX_FMT_D3D11 &&
            avctx->pix_fmt != AV_PIX_FMT_P010LE && avctx->pix_fmt != AV_PIX_FMT_RGBA && avctx->pix_fmt != AV_PIX_FMT_BGRA)
#else
    if (avctx->pix_fmt != AV_PIX_FMT_NV12 && avctx->pix_fmt != AV_PIX_FMT_P010LE &&
        avctx->pix_fmt != AV_PIX_FMT_RGBA && avctx->pix_fmt != AV_PIX_FMT_BGRA)
#endif
    {
        av_log(avctx, AV_LOG_ERROR, "Not supported picture format:%d\n", avctx->pix_fmt);
        return AVERROR(EINVAL);
    }

#if CONFIG_D3D11VA
    if (avctx->hw_frames_ctx)
    {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        d3d11_device_hwctx = frames_ctx->device_ctx->hwctx;
    }
    else if (avctx->hw_device_ctx)
    {
        AVHWDeviceContext *hwdev_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;
        if (hwdev_ctx->type == AV_HWDEVICE_TYPE_D3D11VA)
        {
            d3d11_device_hwctx = hwdev_ctx->hwctx;
        }
        else
        {
            av_log(avctx, AV_LOG_ERROR, "hwdev_ctx->type=%d, should be %d(AV_HWDEVICE_TYPE_D3D11VA)\n", hwdev_ctx->type, AV_HWDEVICE_TYPE_D3D11VA);
            return AVERROR(EINVAL);
        }
    }
    else
    {
        if (hw_device_create(avctx) < 0)
        {
            av_log(avctx, AV_LOG_ERROR, "Create D3D11 device failed\n");
            return AVERROR(EINVAL);
        }
        d3d11_device_hwctx = ctx->d3d11vaDeviceContext;
    }

    if (d3d11_device_hwctx)
    {
        ctx->d3d11_device = d3d11_device_hwctx->device;
        ID3D11Device_AddRef(ctx->d3d11_device);
        ctx->d3d11_context = d3d11_device_hwctx->device_context;
        ID3D11DeviceContext_AddRef(ctx->d3d11_context);
    }
    else
        return AVERROR(EINVAL);
#else
    // FIXME
    //return AVERROR(EINVAL);
#endif

    ret = mtenc_open_session(avctx);
    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "mtenc_open_session failed, error:0x%x\n", ret);

    return ret;
}

static MT_ENC_BUFFER_FORMAT mtenc_map_buffer_format(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_NV12:
        return MT_ENC_BUFFER_FORMAT_NV12;
    case AV_PIX_FMT_RGBA:
        return MT_ENC_BUFFER_FORMAT_RGBA;
    case AV_PIX_FMT_BGRA:
        return MT_ENC_BUFFER_FORMAT_BGRA;
    case AV_PIX_FMT_P010LE:
        return MT_ENC_BUFFER_FORMAT_P010_LE;
    default:
        return MT_ENC_BUFFER_FORMAT_UNDEFINED;
    }
}

static av_cold void set_constqp(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    MT_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

#if CONFIG_AV1_MTENC_ENCODER
    int qmax = avctx->codec->id == AV_CODEC_ID_AV1 ? 255 : 51;
#else
    int qmax = 51;
#endif

    rc->rateControlMode = ctx->rc;
    if (ctx->init_qp_p >= 0) {
        rc->constQP.qpInterP = ctx->init_qp_p;
        if (ctx->init_qp_i >= 0 && ctx->init_qp_b >= 0) {
            rc->constQP.qpIntra = ctx->init_qp_i;
            rc->constQP.qpInterB = ctx->init_qp_b;
        } else if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
            rc->constQP.qpIntra = av_clip(
                rc->constQP.qpInterP * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, qmax);
            rc->constQP.qpInterB = av_clip(
                rc->constQP.qpInterP * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, qmax);
        } else {
            rc->constQP.qpIntra = rc->constQP.qpInterP;
            rc->constQP.qpInterB = rc->constQP.qpInterP;
        }
    } else if (ctx->cqp >= 0) {
        rc->constQP.qpInterP = rc->constQP.qpInterB = rc->constQP.qpIntra = ctx->cqp;
        // if (avctx->b_quant_factor != 0.0)
        //     rc->constQP.qpInterB = av_clip(ctx->cqp * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, qmax);
        // if (avctx->i_quant_factor != 0.0)
        //     rc->constQP.qpIntra = av_clip(ctx->cqp * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, qmax);
    }

    avctx->qmin = -1;
    avctx->qmax = -1;
}

static av_cold void set_rc(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    MT_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;
    int qp_inter_p;

#if CONFIG_AV1_MTENC_ENCODER
    int qmax = avctx->codec->id == AV_CODEC_ID_AV1 ? 255 : 51;
#else
    int qmax = 51;
#endif

    if (avctx->qmin >= 0 && avctx->qmax >= 0) {
        rc->enableMinQP = 1;
        rc->enableMaxQP = 1;

        rc->minQP.qpInterB = avctx->qmin;
        rc->minQP.qpInterP = avctx->qmin;
        rc->minQP.qpIntra  = avctx->qmin;

        rc->maxQP.qpInterB = avctx->qmax;
        rc->maxQP.qpInterP = avctx->qmax;
        rc->maxQP.qpIntra = avctx->qmax;

        qp_inter_p = (avctx->qmax + 3 * avctx->qmin) / 4; // biased towards Qmin
    } else if (avctx->qmin >= 0) {
        rc->enableMinQP = 1;

        rc->minQP.qpInterB = avctx->qmin;
        rc->minQP.qpInterP = avctx->qmin;
        rc->minQP.qpIntra = avctx->qmin;

        qp_inter_p = avctx->qmin;
    } else {
        qp_inter_p = 26; // default to 26
    }

    rc->enableInitialRCQP = 1;

    if (ctx->init_qp_p < 0) {
        rc->initialRCQP.qpInterP  = qp_inter_p;
    } else {
        rc->initialRCQP.qpInterP = ctx->init_qp_p;
    }

    if (ctx->init_qp_i < 0) {
        if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
            rc->initialRCQP.qpIntra = av_clip(
                rc->initialRCQP.qpInterP * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, qmax);
        } else {
            rc->initialRCQP.qpIntra = rc->initialRCQP.qpInterP;
        }
    } else {
        rc->initialRCQP.qpIntra = ctx->init_qp_i;
    }

    if (ctx->init_qp_b < 0) {
        if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
            rc->initialRCQP.qpInterB = av_clip(
                rc->initialRCQP.qpInterP * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, qmax);
        } else {
            rc->initialRCQP.qpInterB = rc->initialRCQP.qpInterP;
        }
    } else {
        rc->initialRCQP.qpInterB = ctx->init_qp_b;
    }
}

static av_cold void set_lossless(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    MT_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    rc->rateControlMode = MT_ENC_PARAMS_RC_CONSTQP;
    rc->constQP.qpInterB = 0;
    rc->constQP.qpInterP = 0;
    rc->constQP.qpIntra  = 0;

    avctx->qmin = -1;
    avctx->qmax = -1;
}

static void mtenc_override_rate_control(AVCodecContext *avctx)
{
    MtencContext *ctx    = avctx->priv_data;
    MT_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    switch (ctx->rc) {
    case MT_ENC_PARAMS_RC_CONSTQP:
        set_constqp(avctx);
        av_log(avctx, AV_LOG_INFO, "CQP mode. cqp:%d, qp-pbi:%d-%d-%d\n",ctx->cqp, ctx->init_qp_p,ctx->init_qp_b,ctx->init_qp_i);
        break;
    case MT_ENC_PARAMS_RC_CBR:
        set_rc(avctx);
        rc->rateControlMode = ctx->rc;
        av_log(avctx, AV_LOG_INFO, "CBR mode. avrage bitrate:%ld\n", (long int)avctx->bit_rate);
        break;
    case MT_ENC_PARAMS_RC_VBR:
        set_rc(avctx);
        rc->rateControlMode = ctx->rc;
        av_log(avctx, AV_LOG_INFO, "VBR mode. bitrate:%ld(avrage), %ld(min), %ld(max), \n",
            (long int)avctx->bit_rate, (long int)avctx->rc_min_rate, (long int)avctx->rc_max_rate);
        break;
    default:
        break;
    }
}

static av_cold int mtenc_recalc_surfaces(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    // default minimum of 4 surfaces
    // multiply by 2 for number of MTENCs on gpu (hardcode to 2)
    // another multiply by 2 to avoid blocking next PBB group
    // int nb_surfaces = FFMAX(4, ctx->encode_config.frameIntervalP * 2 * 2);

    // if (ctx->encode_config.frameIntervalP > 1 && ctx->nb_surfaces < nb_surfaces && ctx->nb_surfaces > 0)
    // {
    //     av_log(avctx, AV_LOG_WARNING,
    //            "Defined b-frame requires more surfaces, "
    //            "increasing used surfaces %d -> %d\n",
    //            ctx->nb_surfaces, nb_surfaces);
    //     ctx->nb_surfaces = FFMAX(ctx->nb_surfaces, nb_surfaces);
    // }
    // else if (ctx->nb_surfaces <= 0)
    //     ctx->nb_surfaces = nb_surfaces;

    // ctx->nb_surfaces = FFMAX(1, FFMIN(MAX_REGISTERED_FRAMES, ctx->nb_surfaces));
    // ctx->async_depth = FFMIN(ctx->async_depth, ctx->nb_surfaces - 2);

    unsigned int extraOutputDelay = 0;
    unsigned int extraEncoderBufferSize = 0;

    if (ctx->bdepth > 1 && ctx->encode_config.frameIntervalP > 1)
    {
        extraOutputDelay = ctx->encode_config.frameIntervalP / 2;
        extraEncoderBufferSize = 4;
    }
    else if (ctx->encode_config.frameIntervalP > 1)
    {
        extraOutputDelay = 1;
        extraEncoderBufferSize = 2;
    }
    else
    {
        extraOutputDelay = 0;
        extraEncoderBufferSize = 1;
    }

    ctx->async_depth = ctx->encode_config.frameIntervalP + extraOutputDelay;
    ctx->nb_surfaces = ctx->async_depth + extraEncoderBufferSize;

    av_log(avctx, AV_LOG_INFO, "nb_surfaces:%d, async_depth:%d\n", ctx->nb_surfaces, ctx->async_depth);

    return 0;
}

static av_cold void mtenc_setup_rate_control(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;

    if (avctx->global_quality > 0)
        av_log(avctx, AV_LOG_WARNING, "Using global_quality with mtenc is deprecated. Use qp instead.\n");

    if (ctx->cqp < 0 && avctx->global_quality > 0)
        ctx->cqp = avctx->global_quality;

    if (avctx->bit_rate > 0) {
        ctx->encode_config.rcParams.averageBitRate = avctx->bit_rate;
    }
    if (avctx->rc_max_rate > 0) {
        ctx->encode_config.rcParams.maxBitRate = avctx->rc_max_rate;
    }
    else
    {
        ctx->encode_config.rcParams.maxBitRate = avctx->bit_rate * 2;
    }
    if (avctx->rc_min_rate > 0) {
        //ctx->encode_config.rcParams.minBitRate = avctx->rc_min_rate;
    }

#ifdef MTENC_HAVE_MULTIPASS
    ctx->encode_config.rcParams.multiPass = ctx->multipass;

    if (ctx->flags & mtenc_ONE_PASS)
        ctx->encode_config.rcParams.multiPass = MT_ENC_MULTI_PASS_DISABLED;
    if (ctx->flags & mtenc_TWO_PASSES || ctx->twopass > 0)
        ctx->encode_config.rcParams.multiPass = MT_ENC_TWO_PASS_FULL_RESOLUTION;

    if (ctx->rc < 0) {
        if (ctx->cbr) {
            ctx->rc = MT_ENC_PARAMS_RC_CBR;
        } else if (ctx->cqp >= 0) {
            ctx->rc = MT_ENC_PARAMS_RC_CONSTQP;
        } else if (ctx->quality >= 0.0f) {
            ctx->rc = MT_ENC_PARAMS_RC_VBR;
        }
    }
#else
    if (ctx->rc < 0) {
        if (ctx->cbr) {
            ctx->rc = MT_ENC_PARAMS_RC_CBR;
        } else if (ctx->cqp >= 0) {
            ctx->rc = MT_ENC_PARAMS_RC_CONSTQP;
        }
    }
#endif

    if (ctx->rc >= 0 && ctx->rc & RC_MODE_DEPRECATED) {
        av_log(avctx, AV_LOG_WARNING, "Specified rc mode is deprecated.\n");
        av_log(avctx, AV_LOG_WARNING, "Use -rc constqp/cbr/vbr, -tune and -multipass instead.\n");

        ctx->rc &= ~RC_MODE_DEPRECATED;
    }

#ifdef MTENC_HAVE_LDKFS
    if (ctx->ldkfs)
         ctx->encode_config.rcParams.lowDelayKeyFrameScale = ctx->ldkfs;
#endif

    if (ctx->flags & MTENC_LOSSLESS) {
        set_lossless(avctx);
    } else if (ctx->rc >= 0) {
        mtenc_override_rate_control(avctx);
    } else {
        ctx->encode_config.rcParams.rateControlMode = MT_ENC_PARAMS_RC_VBR;
        set_rc(avctx);
    }

    // if (avctx->rc_buffer_size > 0) {
    //     ctx->encode_config.rcParams.vbvBufferSize = avctx->rc_buffer_size;
    // } else if (ctx->encode_config.rcParams.averageBitRate > 0) {
    //     avctx->rc_buffer_size = ctx->encode_config.rcParams.vbvBufferSize = 2 * ctx->encode_config.rcParams.averageBitRate;
    // }

    // if (ctx->aq) {
    //     ctx->encode_config.rcParams.enableAQ   = 1;
    //     ctx->encode_config.rcParams.aqStrength = ctx->aq_strength;
    //     av_log(avctx, AV_LOG_VERBOSE, "AQ enabled.\n");
    // }

    // if (ctx->temporal_aq) {
    //     ctx->encode_config.rcParams.enableTemporalAQ = 1;
    //     av_log(avctx, AV_LOG_VERBOSE, "Temporal AQ enabled.\n");
    // }

    // if (ctx->rc_lookahead > 0) {
    //     int lkd_bound = FFMIN(ctx->nb_surfaces, ctx->async_depth) -
    //                     ctx->encode_config.frameIntervalP - 4;

    //     if (lkd_bound < 0) {
    //         av_log(avctx, AV_LOG_WARNING,
    //                "Lookahead not enabled. Increase buffer delay (-delay).\n");
    //     } else {
    //         ctx->encode_config.rcParams.enableLookahead = 1;
    //         ctx->encode_config.rcParams.lookaheadDepth  = av_clip(ctx->rc_lookahead, 0, lkd_bound);
    //         ctx->encode_config.rcParams.disableIadapt   = ctx->no_scenecut;
    //         ctx->encode_config.rcParams.disableBadapt   = !ctx->b_adapt;
    //         av_log(avctx, AV_LOG_VERBOSE,
    //                "Lookahead enabled: depth %d, scenecut %s, B-adapt %s.\n",
    //                ctx->encode_config.rcParams.lookaheadDepth,
    //                ctx->encode_config.rcParams.disableIadapt ? "disabled" : "enabled",
    //                ctx->encode_config.rcParams.disableBadapt ? "disabled" : "enabled");
    //     }
    // }

    // if (ctx->strict_gop) {
    //     ctx->encode_config.rcParams.strictGOPTarget = 1;
    //     av_log(avctx, AV_LOG_VERBOSE, "Strict GOP target enabled.\n");
    // }

    // if (ctx->nonref_p)
    //     ctx->encode_config.rcParams.enableNonRefP = 1;

    // if (ctx->zerolatency)
    //     ctx->encode_config.rcParams.zeroReorderDelay = 1;

    // if (ctx->quality) {
    //     //convert from float to fixed point 8.8
    //     int tmp_quality = (int)(ctx->quality * 256.0f);
    //     ctx->encode_config.rcParams.targetQuality = (uint8_t)(tmp_quality >> 8);
    //     ctx->encode_config.rcParams.targetQualityLSB = (uint8_t)(tmp_quality & 0xff);

    //     av_log(avctx, AV_LOG_VERBOSE, "CQ(%d) mode enabled.\n", tmp_quality);

    //     //CQ mode shall discard avg bitrate & honor max bitrate;
    //     ctx->encode_config.rcParams.averageBitRate = avctx->bit_rate = 0;
    //     ctx->encode_config.rcParams.maxBitRate = avctx->rc_max_rate;
    // }
}

static av_cold int mtenc_setup_h264_config(AVCodecContext *avctx)
{
    MtencContext *ctx                      = avctx->priv_data;
    MT_ENC_CONFIG *cc                      = &ctx->encode_config;
    MT_ENC_CONFIG_H264 *h264               = &cc->encodeCodecConfig.h264Config;
    MT_ENC_CONFIG_H264_VUI_PARAMETERS *vui = &h264->h264VUIParameters;

    vui->colourMatrix = (ctx->data_pix_fmt == AV_PIX_FMT_GBRP || ctx->data_pix_fmt == AV_PIX_FMT_GBRP16) ? AVCOL_SPC_RGB : avctx->colorspace;
    vui->colourPrimaries = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;
    vui->videoFullRangeFlag = (avctx->color_range == AVCOL_RANGE_JPEG
        || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ420P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ422P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ444P);

    vui->colourDescriptionPresentFlag =
        (vui->colourMatrix != 2 || vui->colourPrimaries != 2 || vui->transferCharacteristics != 2);

    vui->videoSignalTypePresentFlag =
        (vui->colourDescriptionPresentFlag
        || vui->videoFormat != 5
        || vui->videoFullRangeFlag != 0);

    if (avctx->gop_size >= 0) {
        h264->idrPeriod = cc->gopLength;
    }

    cc->bDepth = ctx->bdepth;
    cc->intraRefreshMode = ctx->intra_refresh_mode;
    cc->intraRefreshArg = ctx->intra_refresh_arg;
    cc->refs = ctx->refs;
    cc->repeatHeaders = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    cc->encodeCodecConfig.h264Config.nbTemporalLayers = ctx->nb_temporal_layers;
    cc->encodeCodecConfig.h264Config.disableSVCPrefixNalu = 0;
    cc->encodeCodecConfig.h264Config.sliceMode = ctx->slice_mode;
    cc->encodeCodecConfig.h264Config.sliceModeData = avctx->slices;

    if (MT_ENC_PARAMS_RC_CBR == cc->rcParams.rateControlMode) {
        h264->outputBufferingPeriodSEI = 1;
    }

    h264->outputPictureTimingSEI = 1;

    if(mtenc_map_buffer_format(ctx->data_pix_fmt) == MT_ENC_BUFFER_FORMAT_P010_LE)
    {
        h264->pixelBitDepthMinus8 = 2;
        ctx->profile = MT_ENC_CODEC_PROFILE_ID_HIGH10_H264;
    }

    switch (ctx->profile)
    {
    case MT_ENC_CODEC_PROFILE_ID_BASELINE_H264:
        cc->profileID = MT_ENC_CODEC_PROFILE_ID_BASELINE_H264;
        avctx->profile = FF_PROFILE_H264_BASELINE;
        break;
    case MT_ENC_CODEC_PROFILE_ID_HIGH_H264:
        cc->profileID = MT_ENC_CODEC_PROFILE_ID_HIGH_H264;
        avctx->profile = FF_PROFILE_H264_HIGH;
        break;
    case MT_ENC_CODEC_PROFILE_ID_HIGH10_H264:
        cc->profileID = MT_ENC_CODEC_PROFILE_ID_HIGH10_H264;
        avctx->profile = FF_PROFILE_H264_HIGH_10;
        break;
    default:
        cc->profileID = MT_ENC_CODEC_PROFILE_ID_MAIN_H264;
        avctx->profile = FF_PROFILE_H264_MAIN;
        break;
    }

    // force setting profile as high444p if input is AV_PIX_FMT_YUV444P
    if (ctx->data_pix_fmt == AV_PIX_FMT_YUV444P) {
        avctx->profile = FF_PROFILE_H264_HIGH_444_PREDICTIVE;
    }

    h264->level = ctx->level;

    return 0;
}

static av_cold int mtenc_setup_hevc_config(AVCodecContext *avctx)
{
    MtencContext *ctx                      = avctx->priv_data;
    MT_ENC_CONFIG *cc                      = &ctx->encode_config;
    MT_ENC_CONFIG_HEVC *hevc               = &cc->encodeCodecConfig.hevcConfig;
    MT_ENC_CONFIG_HEVC_VUI_PARAMETERS *vui = &hevc->hevcVUIParameters;

    vui->colourMatrix = (ctx->data_pix_fmt == AV_PIX_FMT_GBRP || ctx->data_pix_fmt == AV_PIX_FMT_GBRP16) ? AVCOL_SPC_RGB : avctx->colorspace;
    vui->colourPrimaries = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;
    vui->videoFullRangeFlag = (avctx->color_range == AVCOL_RANGE_JPEG
        || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ420P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ422P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ444P);

    vui->colourDescriptionPresentFlag =
        (vui->colourMatrix != 2 || vui->colourPrimaries != 2 || vui->transferCharacteristics != 2);

    vui->videoSignalTypePresentFlag =
        (vui->colourDescriptionPresentFlag
        || vui->videoFormat != 5
        || vui->videoFullRangeFlag != 0);

    if (avctx->gop_size >= 0) {
        hevc->idrPeriod = cc->gopLength;
    }

    cc->bDepth = ctx->bdepth;
    cc->intraRefreshMode = ctx->intra_refresh_mode;
    cc->intraRefreshArg = ctx->intra_refresh_arg;
    cc->refs = ctx->refs;
    cc->repeatHeaders = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    cc->encodeCodecConfig.hevcConfig.sliceMode = ctx->slice_mode;
    cc->encodeCodecConfig.hevcConfig.sliceModeData = avctx->slices;

    if (MT_ENC_PARAMS_RC_CBR == cc->rcParams.rateControlMode) {
        hevc->outputBufferingPeriodSEI = 1;
    }

    hevc->outputPictureTimingSEI = 1;

    if(mtenc_map_buffer_format(ctx->data_pix_fmt) == MT_ENC_BUFFER_FORMAT_P010_LE)
    {
        hevc->pixelBitDepthMinus8 = 2;
        ctx->profile = MT_ENC_CODEC_PROFILE_ID_MAIN_10_HEVC;
    }
    switch (ctx->profile)
    {
    case MT_ENC_CODEC_PROFILE_ID_MAIN_10_HEVC:
        cc->profileID = MT_ENC_CODEC_PROFILE_ID_MAIN_10_HEVC;
        avctx->profile = FF_PROFILE_HEVC_MAIN_10;
        break;
    default:
        cc->profileID = MT_ENC_CODEC_PROFILE_ID_MAIN_HEVC;
        avctx->profile = FF_PROFILE_HEVC_MAIN;
        break;
    }

    hevc->level = ctx->level;
    hevc->tier  = ctx->tier;

    return 0;
}

#if CONFIG_AV1_MTENC_ENCODER
static av_cold int mtenc_setup_av1_config(AVCodecContext *avctx)
{
    MtencContext *ctx                       = avctx->priv_data;
    MT_ENC_CONFIG *cc                       = &ctx->encode_config;
    MT_ENC_CONFIG_AV1 *av1                  = &cc->encodeCodecConfig.av1Config;

    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(ctx->data_pix_fmt);
    const int is_gbrp = (ctx->data_pix_fmt == AV_PIX_FMT_GBRP || ctx->data_pix_fmt == AV_PIX_FMT_GBRP16);

    if ((pixdesc->flags & AV_PIX_FMT_FLAG_RGB) && !is_gbrp) {
        av1->matrixCoefficients = AVCOL_SPC_BT470BG;
        av1->colorPrimaries = avctx->color_primaries;
        av1->transferCharacteristics = avctx->color_trc;
        av1->colorRange = 0;
    } else {
        av1->matrixCoefficients = is_gbrp ? AVCOL_SPC_RGB : avctx->colorspace;
        av1->colorPrimaries = avctx->color_primaries;
        av1->transferCharacteristics = avctx->color_trc;
        av1->colorRange = (avctx->color_range == AVCOL_RANGE_JPEG
            || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ420P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ422P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ444P);
    }

    cc->profileID = MT_ENC_CODEC_PROFILE_ID_MAIN_AV1;
    avctx->profile  = AV_PROFILE_AV1_MAIN;

    cc->bDepth = ctx->bdepth;
    cc->intraRefreshMode = ctx->intra_refresh_mode;
    cc->intraRefreshArg = ctx->intra_refresh_arg;
    cc->refs = ctx->refs;
    /* mp4 encapsulation requires sequence headers to be present on all keyframes for AV1 */
    cc->repeatHeaders = 1;

    av1->idrPeriod = cc->gopLength;

    if (ctx->tile_cols >= 0)
        av1->numTileColumns = ctx->tile_cols;
    if (ctx->tile_rows >= 0)
        av1->numTileRows = ctx->tile_rows;

    av1->level = ctx->level;
    av1->tier = ctx->tier;

    av1->enableTimingInfo = ctx->timing_info;

    if(mtenc_map_buffer_format(ctx->data_pix_fmt) == MT_ENC_BUFFER_FORMAT_P010_LE)
    {
        av1->inputPixelBitDepthMinus8 = 2;
    }

    return 0;
}
#endif

static av_cold int mtenc_setup_codec_config(AVCodecContext *avctx)
{
    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        return mtenc_setup_h264_config(avctx);
    case AV_CODEC_ID_HEVC:
        return mtenc_setup_hevc_config(avctx);
    #if CONFIG_AV1_MTENC_ENCODER
    case AV_CODEC_ID_AV1:
        return mtenc_setup_av1_config(avctx);
    #endif
    /* Earlier switch/case will return if unknown codec is passed. */
    }

    return 0;
}

static av_cold int mtenc_setup_encoder(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &dl_fn->mtenc_funcs;

    MT_ENC_PRESET_CONFIG preset_config = { 0 };
    MTENCSTATUS mt_status = MT_ENC_SUCCESS;
    int res = 0;

    ctx->encode_config.version = MTENCAPI_VERSION;
    ctx->init_encode_params.version = MTENCAPI_VERSION;

    ctx->init_encode_params.encodeHeight = avctx->height;
    ctx->init_encode_params.maxEncodeHeight = avctx->height;
    ctx->init_encode_params.encodeWidth = avctx->width;
    ctx->init_encode_params.maxEncodeWidth = avctx->width;

    ctx->init_encode_params.encodeConfig = &ctx->encode_config;

    preset_config.version = MTENCAPI_VERSION;
    preset_config.presetCfg.version = MTENCAPI_VERSION;

    av_log(avctx, AV_LOG_VERBOSE, "encodeID: %d, presetID: %d\n",
           ctx->init_encode_params.encodeID, ctx->init_encode_params.presetID);
    mt_status = p_mtenc->mtEncGetPresetConfig(ctx->mtencoder,
        ctx->init_encode_params.encodeID,
        ctx->init_encode_params.presetID,
        &preset_config);

    if (mt_status != MT_ENC_SUCCESS)
        return mtenc_print_error(avctx, mt_status, "Cannot get the preset configuration");

    memcpy(&ctx->encode_config, &preset_config.presetCfg, sizeof(ctx->encode_config));

    ctx->encode_config.version = MTENCAPI_VERSION;

    if(avctx->framerate.num > 0) {
        ctx->init_encode_params.frameRateNum = avctx->framerate.num;
        ctx->init_encode_params.frameRateDen = avctx->framerate.den;
    }
    else if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        ctx->init_encode_params.frameRateNum = avctx->framerate.num;
        ctx->init_encode_params.frameRateDen = avctx->framerate.den;
    } else {
        ctx->init_encode_params.frameRateNum = avctx->time_base.den;
FF_DISABLE_DEPRECATION_WARNINGS
        ctx->init_encode_params.frameRateDen = avctx->time_base.num
#if FF_API_TICKS_PER_FRAME
        * avctx->ticks_per_frame
#endif
        ;
FF_ENABLE_DEPRECATION_WARNINGS
    }

    ctx->init_encode_params.enableEncodeAsync = 0;

    if (avctx->gop_size > 0) {
        if (avctx->max_b_frames >= 0) {
            /* 0 is intra-only, 1 is I/P only, 2 is one B-Frame, 3 two B-frames, and so on. */
            ctx->encode_config.frameIntervalP = avctx->max_b_frames + 1;
        }

        ctx->encode_config.gopLength = avctx->gop_size;
    } else if (avctx->gop_size == 0) {
        ctx->encode_config.frameIntervalP = 0;
        ctx->encode_config.gopLength = 1;
    }

    // support refs in encoder
    if (avctx->refs > 0) {
        ctx->encode_config.refs = avctx->refs;
    }

    mtenc_recalc_surfaces(avctx);

    mtenc_setup_rate_control(avctx);

    res = mtenc_setup_codec_config(avctx);
    if (res) {
        av_log(avctx, AV_LOG_WARNING, "setup codec config fail, ret: %d\n", res);
        return res;
    }

    av_log(avctx, AV_LOG_DEBUG, "version: 0x%X, encodeId:%d, profileId:%d, frameIntervalP:%d, rateControlMode:%d, level:%d, qp=%d-%d-%d, bDepth=%d, refs=%d\n",
        ctx->init_encode_params.encodeConfig->version,
        ctx->init_encode_params.encodeID,
        ctx->init_encode_params.encodeConfig->profileID,
        ctx->init_encode_params.encodeConfig->frameIntervalP, ctx->init_encode_params.encodeConfig->rcParams.rateControlMode,
        MT_ENC_CODEC_ID_H264 == ctx->init_encode_params.encodeID ?
        ctx->init_encode_params.encodeConfig->encodeCodecConfig.h264Config.level : ctx->init_encode_params.encodeConfig->encodeCodecConfig.hevcConfig.level,
        ctx->init_encode_params.encodeConfig->rcParams.constQP.qpInterP,
        ctx->init_encode_params.encodeConfig->rcParams.constQP.qpInterB, ctx->init_encode_params.encodeConfig->rcParams.constQP.qpIntra,
        ctx->init_encode_params.encodeConfig->bDepth, ctx->init_encode_params.encodeConfig->refs);

    mt_status = p_mtenc->mtEncInitEncoder(ctx->mtencoder, &ctx->init_encode_params);
    if (mt_status != MT_ENC_SUCCESS) {
        return mtenc_print_error(avctx, mt_status, "InitializeEncoder failed");
    }

    if (ctx->encode_config.frameIntervalP > 1)
        avctx->has_b_frames = 2;

    if (ctx->encode_config.rcParams.averageBitRate > 0)
        avctx->bit_rate = ctx->encode_config.rcParams.averageBitRate;

    return 0;
}

#if CONFIG_D3D11VA
static DXGI_FORMAT mtenc_map_dxgi_format(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_NV12:
        return DXGI_FORMAT_NV12;
    case AV_PIX_FMT_RGBA:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case AV_PIX_FMT_BGRA:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case AV_PIX_FMT_P010LE:
        return DXGI_FORMAT_P010;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

static uint32_t mtenc_get_d3d_bind_flags(DXGI_FORMAT dxgiFmt)
{
    switch (dxgiFmt)
    {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
        return D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
               D3D11_BIND_VIDEO_ENCODER;
    default:
        return 0;
    }
}
#endif

static av_cold int mtenc_alloc_surface(AVCodecContext *avctx, int idx)
{
    MtencContext *ctx = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->mtenc_funcs;
    MtencSurface* tmp_surface = &ctx->surfaces[idx];
    MTENCSTATUS mt_status;
    MT_ENC_CREATE_OUTPUT_BUFFER allocOut = { MTENCAPI_VERSION };
#if CONFIG_D3D11VA
    int ret;
    D3D11_TEXTURE2D_DESC desc = { 0 };
#else
    uint64_t size = 0;
    void *buf = NULL;
#endif

    tmp_surface->format = mtenc_map_buffer_format(ctx->data_pix_fmt);
    av_log(avctx, AV_LOG_VERBOSE, "surface format: %d(0x%X)\n",
           tmp_surface->format, tmp_surface->format);
    if (tmp_surface->format == MT_ENC_BUFFER_FORMAT_UNDEFINED) {
        av_log(avctx, AV_LOG_FATAL, "Invalid input pixel format: %s\n",
                av_get_pix_fmt_name(ctx->data_pix_fmt));
        return AVERROR(EINVAL);
    }

#if CONFIG_D3D11VA
    desc.Width              = avctx->width;
    desc.Height             = avctx->height;
    desc.Format             = mtenc_map_dxgi_format(ctx->data_pix_fmt);
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.SampleDesc.Count   = 1;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    desc.BindFlags          = mtenc_get_d3d_bind_flags(desc.Format);
    desc.CPUAccessFlags     = 0;

    ret = ID3D11Device_CreateTexture2D(ctx->d3d11_device, &desc, NULL,
        (ID3D11Texture2D**)&(tmp_surface->input_surface));
    if (ret < 0)
    {
        av_log(avctx, AV_LOG_ERROR, "create Texture2D failed: 0x%x\n", ret);
    }
    tmp_surface->completion_event = CreateEvent(NULL, 0, 0, NULL);

    tmp_surface->width = desc.Width;
    tmp_surface->height = desc.Height;
    tmp_surface->pitch = desc.Width;
#else
    // FIXME
    if ((tmp_surface->format == MT_ENC_BUFFER_FORMAT_RGBA)
        || (tmp_surface->format == MT_ENC_BUFFER_FORMAT_BGRA)) {
        size = avctx->width * avctx->height * 4;
    } else if (tmp_surface->format == MT_ENC_BUFFER_FORMAT_P010_LE) {
        size = avctx->width * avctx->height * 3;
    } else {
        size = avctx->width * avctx->height * 3 / 2;
    }
    av_log(avctx, AV_LOG_VERBOSE, "alloc buffer size: %ld\n", size);
    buf = malloc(size);
    if (buf) {
        tmp_surface->input_surface = buf;
        tmp_surface->width = avctx->width;
        tmp_surface->height = avctx->height;
        tmp_surface->pitch = avctx->width;
    } else {
        av_log(avctx, AV_LOG_ERROR, "malloc failed, size: %ld\n", size);
    }
#endif

    mt_status = p_nvenc->mtEncCreateOutputBuffer(ctx->mtencoder, &allocOut);
    if (mt_status != MT_ENC_SUCCESS) {
        int err = mtenc_print_error(avctx, mt_status, "CreateOutputBuffer failed");
        return err;
    }

    tmp_surface->output_surface = allocOut.outputBuffer;

    av_fifo_write(ctx->unused_surface_queue, &tmp_surface, 1);

    return 0;
}

static av_cold int mtenc_setup_surfaces(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    int i, ret = 0;
#if CONFIG_D3D11VA
    D3D11_TEXTURE2D_DESC desc = { 0 };
#endif

    ctx->surfaces = av_calloc(ctx->nb_surfaces, sizeof(MtencSurface));
    if (!ctx->surfaces)
        return AVERROR(ENOMEM);

    ctx->unused_surface_queue = av_fifo_alloc2(ctx->nb_surfaces, sizeof(MtencSurface*), 0);
    if (!ctx->unused_surface_queue)
        return AVERROR(ENOMEM);

    ctx->output_surface_queue = av_fifo_alloc2(ctx->nb_surfaces, sizeof(MtencSurface*), 0);
    if (!ctx->output_surface_queue)
        return AVERROR(ENOMEM);
    ctx->output_surface_ready_queue = av_fifo_alloc2(ctx->nb_surfaces, sizeof(MtencSurface*), 0);
    if (!ctx->output_surface_ready_queue)
        return AVERROR(ENOMEM);

    for (i = 0; i < ctx->nb_surfaces; i++) {
        if ((ret = mtenc_alloc_surface(avctx, i)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "alloc surface failed: %d\n", ret);
            break;
        }
    }

#if CONFIG_D3D11VA
    desc.Format    = mtenc_map_dxgi_format(avctx->pix_fmt);
    if(desc.Format != DXGI_FORMAT_UNKNOWN)
    {
        desc.Width     = avctx->width;
        desc.Height    = avctx->height;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_STAGING;
        desc.BindFlags        = 0;
        desc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;

        ret = ID3D11Device_CreateTexture2D(ctx->d3d11_device, &desc, NULL, (ID3D11Texture2D**)&ctx->system_surface);
        if (ret < 0)
        {
            av_log(avctx, AV_LOG_ERROR, "alloc Texture2D failed: 0x%x\n", ret);
        }
    }
#else
    // FIXME
#endif

    return ret;
}

static av_cold int mtenc_setup_extradata(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &dl_fn->mtenc_funcs;
    MTENCSTATUS mt_status;
    uint32_t outSize = 0;
    char tmpHeader[256];
    MT_ENC_SEQUENCE_PARAM_PAYLOAD payload = { 0 };

    av_log(avctx, AV_LOG_INFO, "mtenc_setup_extradata");

    payload.version = MTENCAPI_VERSION;
    payload.spsppsBuffer = tmpHeader;
    payload.inBufferSize = sizeof(tmpHeader);
    payload.outSPSPPSPayloadSize = &outSize;

    mt_status = p_mtenc->mtEncGetSequenceParams(ctx->mtencoder, &payload);
    if (mt_status != MT_ENC_SUCCESS) {
        return mtenc_print_error(avctx, mt_status, "GetSequenceParams failed");
    }

    if(outSize <= 0)
    {
        return mtenc_print_error(avctx, mt_status, "GetSequenceParams return error length");
    }

    avctx->extradata_size = outSize;
    avctx->extradata = av_mallocz(outSize + AV_INPUT_BUFFER_PADDING_SIZE);

    if (!avctx->extradata) {
        return AVERROR(ENOMEM);
    }
    memcpy(avctx->extradata, tmpHeader, outSize);

    return 0;
}

av_cold int ff_mtenc_encode_close(AVCodecContext *avctx)
{
    MtencContext *ctx               = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &dl_fn->mtenc_funcs;
    int i;

    av_log(avctx, AV_LOG_INFO, "ff_mtenc_encode_close\n");
    /* the encoder has to be flushed before it can be closed */
    if (ctx->mtencoder) {
        MT_ENC_PIC_PARAMS params        = { .version        = MTENCAPI_VERSION,
                                            .encodePicFlags = MT_ENC_PIC_FLAG_EOS };

        p_mtenc->mtEncEncodeFrame(ctx->mtencoder, &params);
    }

    av_fifo_freep2(&ctx->output_surface_ready_queue);
    av_fifo_freep2(&ctx->output_surface_queue);
    av_fifo_freep2(&ctx->unused_surface_queue);

    if (ctx->surfaces) {
        for (i = 0; i < ctx->nb_surfaces; ++i) {
            if (ctx->surfaces[i].input_surface)
            {
                #if CONFIG_D3D11VA
                ID3D11Texture2D_Release((ID3D11Texture2D*)(ctx->surfaces[i].input_surface));
                #else
                free(ctx->surfaces[i].input_surface);
                #endif
                ctx->surfaces[i].input_surface = NULL;
            }
#if CONFIG_D3D11VA
            if (ctx->surfaces[i].completion_event)
            {
                CloseHandle(ctx->surfaces[i].completion_event);
                ctx->surfaces[i].completion_event = NULL;
            }
#endif
            p_mtenc->mtEncReleaseOutputBuffer(ctx->mtencoder, ctx->surfaces[i].output_surface);
            ctx->surfaces[i].output_surface = NULL;
        }
    }
    av_freep(&ctx->surfaces);
    ctx->nb_surfaces = 0;

    av_frame_free(&ctx->frame);
    av_freep(&ctx->sei_data);

    if (ctx->mtencoder) {
        av_log(avctx, AV_LOG_VERBOSE, "release ctx->mtencoder: %p\n", ctx->mtencoder);
        p_mtenc->mtEncReleaseEncoder(ctx->mtencoder);
    }
    ctx->mtencoder = NULL;

#if CONFIG_D3D11VA
    if (ctx->system_surface)
    {
        ID3D11Texture2D_Release((ID3D11Texture2D*)ctx->system_surface);
        ctx->system_surface = NULL;
    }

    if (ctx->d3d11_device) {
        ID3D11Device_Release(ctx->d3d11_device);
        ctx->d3d11_device = NULL;
    }
    if (ctx->d3d11_context) {
        ID3D11DeviceContext_Release(ctx->d3d11_context);
        ctx->d3d11_context = NULL;
    }
    hw_device_destroy(avctx);
#endif

    if(dl_fn->mtenc_module)
    {
#ifdef _WIN32
        FreeLibrary(dl_fn->mtenc_module);
#else
        dlclose(dl_fn->mtenc_module);
#endif
    }
    dl_fn->mtenc_module = 0;

    av_log(avctx, AV_LOG_VERBOSE, "mtenc unloaded\n");

    return 0;
}

av_cold int ff_mtenc_encode_init(AVCodecContext *avctx)
{
    MtencContext *ctx = avctx->priv_data;
    int ret;

    av_log(avctx, AV_LOG_INFO, "avctx->pix_fmt=%d(%s), rc:%d, ctx=0x%p, ctx->rc=0x%p\n",
           avctx->pix_fmt, av_get_pix_fmt_name(avctx->pix_fmt), ctx->rc, ctx, &ctx->rc);

    // init params
    ctx->system_surface = NULL;
    ctx->nb_surfaces = 0;
    ctx->cbr = 0;
    ctx->device = ANY_DEVICE;
    ctx->async_depth = INT_MAX;
    ctx->coder = -1;

    // param validation
    ctx->bdepth = FFMIN(ctx->bdepth, av_log2(avctx->max_b_frames) + 1);
    ctx->init_qp_i = av_clip(ctx->init_qp_i, -1, 51);
    ctx->init_qp_p = av_clip(ctx->init_qp_p, -1, 51);
    ctx->init_qp_b = av_clip(ctx->init_qp_b, -1, 51);
    ctx->cqp = av_clip(ctx->cqp, -1, 51);

    if (avctx->pix_fmt == AV_PIX_FMT_D3D11) {
        AVHWFramesContext *frames_ctx;
        if (!avctx->hw_frames_ctx) {
            av_log(avctx, AV_LOG_ERROR,
                   "hw_frames_ctx must be set when using GPU frames as input\n");
            return AVERROR(EINVAL);
        }
        frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        if (frames_ctx->format != avctx->pix_fmt) {
            av_log(avctx, AV_LOG_ERROR,
                   "hw_frames_ctx must match the GPU frame type\n");
            return AVERROR(EINVAL);
        }
        ctx->data_pix_fmt = frames_ctx->sw_format;
    } else {
        ctx->data_pix_fmt = avctx->pix_fmt;
    }
    av_log(avctx, AV_LOG_INFO, "ctx->data_pix_fmt: %d\n", ctx->data_pix_fmt);

    av_log(avctx, AV_LOG_INFO, "alloc frame\n");
    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
        return AVERROR(ENOMEM);
    av_log(avctx, AV_LOG_INFO, "mtenc_load_libraries\n");
    if ((ret = mtenc_load_libraries(avctx)) < 0)
        return ret;
    av_log(avctx, AV_LOG_INFO, "mtenc_setup_device\n");
    if ((ret = mtenc_setup_device(avctx)) < 0)
        return ret;
    av_log(avctx, AV_LOG_INFO, "mtenc_setup_encoder\n");
    if ((ret = mtenc_setup_encoder(avctx)) < 0)
        return ret;
    av_log(avctx, AV_LOG_INFO, "mtenc_setup_surfaces\n");
    if ((ret = mtenc_setup_surfaces(avctx)) < 0)
        return ret;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        if ((ret = mtenc_setup_extradata(avctx)) < 0)
        return ret;
    }

    av_log(avctx, AV_LOG_INFO, "encode gop:%d, bf:%d, qp-bpi:%d-%d-%d\n",ctx->encode_config.gopLength,
        ctx->encode_config.frameIntervalP > 0 ? ctx->encode_config.frameIntervalP - 1 : 0, ctx->encode_config.rcParams.constQP.qpInterB,
        ctx->encode_config.rcParams.constQP.qpInterP,ctx->encode_config.rcParams.constQP.qpIntra);

    return 0;
}

static MtencSurface *get_free_frame(MtencContext *ctx)
{
    MtencSurface *tmp_surf;

    if (av_fifo_read(ctx->unused_surface_queue, &tmp_surf, 1) < 0)
        // queue empty
        return NULL;

    return tmp_surf;
}

#if CONFIG_D3D11VA
static int mtenc_upload_frame_d3d11va(AVCodecContext *avctx, const AVFrame *frame, MtencSurface *mtenc_surface)
{
#ifndef CONFIG_D3D11VA
    av_log(avctx, AV_LOG_ERROR, "Need D3D11VA support\n");
    return AVERROR(EINVAL);
#endif
    MtencContext *ctx = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &dl_fn->mtenc_funcs;

    int ret;
    MTENCSTATUS mt_status;
    int dst_linesize[4];
    uint8_t *dst_data[4];
    //int stride;
    D3D11_MAPPED_SUBRESOURCE map;
    MT_ENC_MAP_RESOURCE mapInputResource = {MTENCAPI_VERSION};

    ID3D11DeviceContext* d3d11_context = ctx->d3d11_context;
    if (!d3d11_context)
    {
        av_log(avctx, AV_LOG_ERROR, "Null d3d11 context\n");
        return AVERROR(EINVAL);
    }

    if (avctx->pix_fmt == AV_PIX_FMT_D3D11)
    {
        intptr_t pindex = (intptr_t)frame->data[1];
        //D3D11_TEXTURE2D_DESC dstDesc = { 0 };
        //D3D11_TEXTURE2D_DESC srcDesc = { 0 };
        //ID3D11Texture2D_GetDesc((ID3D11Texture2D*)mtenc_surface->input_surface, &dstDesc);
        //ID3D11Texture2D_GetDesc((ID3D11Texture2D*)frame->data[0], &srcDesc);
        ID3D11DeviceContext_CopySubresourceRegion(d3d11_context, (ID3D11Resource*)mtenc_surface->input_surface,
             0, 0, 0, 0, (ID3D11Resource*)frame->data[0], pindex, NULL);
        //ID3D11DeviceContext_CopyResource(d3d11_context, mtenc_surface->input_surface, (ID3D11Resource*)frame->data[0]);
        //dumpBmp(hwdev_priv_ctx, frame->data[0]);
    }
    else if (avctx->pix_fmt == AV_PIX_FMT_NV12 || avctx->pix_fmt == AV_PIX_FMT_P010LE ||
        avctx->pix_fmt == AV_PIX_FMT_BGRA || avctx->pix_fmt == AV_PIX_FMT_RGBA)
    {
        // 1 - map MtencContext::system_surface to writable memory
        ret = ID3D11DeviceContext_Map(d3d11_context, ctx->system_surface, 0, D3D11_MAP_WRITE, 0, &map);
        if (ret < 0)
        {
            av_log(avctx, AV_LOG_ERROR, "Map d3d11 resource failed 0x%x.\n", ret);
            goto failed;
        }

        // 2 - copy data from AVFrame to MtencContext::system_surface
        for (int i = 0; i < FF_ARRAY_ELEMS(dst_linesize); i++)
        {
            dst_linesize[i] = map.RowPitch;
        }

        if (frame->format == AV_PIX_FMT_YUV420P)
        {
            dst_linesize[1] = dst_linesize[2] >>= 1;
        }

        ret = av_image_fill_pointers(dst_data, frame->format, mtenc_surface->height, map.pData, dst_linesize);
        if (ret < 0)
        {
            av_log(avctx, AV_LOG_ERROR, "av_image_fill_pointers failed 0x%x. format=%d, height=%d \n",
                ret, frame->format, mtenc_surface->height);
            ID3D11DeviceContext_Unmap(d3d11_context, ctx->system_surface, 0);
            goto failed;
        }

        if (frame->format == AV_PIX_FMT_YUV420P)
            FFSWAP(uint8_t*, dst_data[1], dst_data[2]);

        av_image_copy(dst_data, dst_linesize,
                    (const uint8_t**)frame->data, frame->linesize, frame->format,
                    avctx->width, avctx->height);

        // 3 - ummap MtencContext::system_surface from writable memory
        ID3D11DeviceContext_Unmap(d3d11_context, ctx->system_surface, 0);

        // 4 - copy data from MtencContext::system_surface to MtencSurface::input_surface
        ID3D11DeviceContext_CopyResource(d3d11_context, mtenc_surface->input_surface, ctx->system_surface);
    }
    else
    {
        av_log(avctx, AV_LOG_ERROR, "Not supported pix_fmt: %d\n", avctx->pix_fmt);
        goto failed;
    }

    // map MtencSurface::input_surface to MtencSurface::input_buffer
    mapInputResource.resourceType        = MT_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    mapInputResource.resourceToMap       = mtenc_surface->input_surface;
    mt_status = p_mtenc->mtEncMapResource(ctx->mtencoder, &mapInputResource);
    if (mt_status != MT_ENC_SUCCESS) {
        return mtenc_print_error(avctx, mt_status, "Error mapping an input resource");
    }
    mtenc_surface->input_buffer = mapInputResource.mappedResource;

    return 0;

failed:
    return AVERROR(EINVAL);
}
#else
static int mtenc_upload_frame(AVCodecContext *avctx, const AVFrame *frame, MtencSurface *mtenc_surface)
{
    MtencContext *ctx = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &dl_fn->mtenc_funcs;

    uint64_t size = 0;
    MTENCSTATUS mt_status;
    MT_ENC_MAP_RESOURCE mapInputResource = {MTENCAPI_VERSION};

    if (avctx->pix_fmt == AV_PIX_FMT_NV12 || avctx->pix_fmt == AV_PIX_FMT_P010LE ||
        avctx->pix_fmt == AV_PIX_FMT_BGRA || avctx->pix_fmt == AV_PIX_FMT_RGBA)
    {
        if ((mtenc_surface->format == MT_ENC_BUFFER_FORMAT_RGBA)
            || (mtenc_surface->format == MT_ENC_BUFFER_FORMAT_BGRA)) {
            size = avctx->width * avctx->height * 4;
        } else if (mtenc_surface->format == MT_ENC_BUFFER_FORMAT_P010_LE){
            size = avctx->width * avctx->height * 3;
        } else {
            size = avctx->width * avctx->height * 3 / 2;
        }

        av_image_copy_to_buffer(mtenc_surface->input_surface, size,
                    (const uint8_t**)frame->data, frame->linesize, frame->format,
                                avctx->width, avctx->height, 1);
    }
    else
    {
        av_log(avctx, AV_LOG_ERROR, "Not supported pix_fmt: %d\n", avctx->pix_fmt);
        goto failed;
    }

    // map MtencSurface::input_surface to MtencSurface::input_buffer
    mapInputResource.resourceType        = MT_ENC_INPUT_RESOURCE_TYPE_OPENGL_TEX;
    mapInputResource.resourceToMap       = mtenc_surface->input_surface;
    mt_status = p_mtenc->mtEncMapResource(ctx->mtencoder, &mapInputResource);
    if (mt_status != MT_ENC_SUCCESS) {
        return mtenc_print_error(avctx, mt_status, "Error mapping an input resource");
    }
    mtenc_surface->input_buffer = mapInputResource.mappedResource;

    return 0;

failed:
    return AVERROR(EINVAL);
}
#endif

static inline void timestamp_queue_enqueue(AVFifo* queue, int64_t timestamp)
{
    av_fifo_write(queue, &timestamp, 1);
}

static inline int64_t timestamp_queue_dequeue(AVFifo* queue)
{
    int64_t timestamp = AV_NOPTS_VALUE;

    av_fifo_read(queue, &timestamp, 1);

    return timestamp;
}

static int mtenc_set_timestamp(AVCodecContext *avctx,
                               MT_ENC_LOCK_BUFFER *params,
                               AVPacket *pkt)
{
    //MtencContext *ctx = (MtencContext *)avctx->priv_data;

    pkt->pts = params->outputTimeStamp;
    pkt->dts = params->decodeTimeStamp;
    //av_log(avctx, AV_LOG_INFO, "pts=%lld, dts=%lld\n", pkt->pts, pkt->dts);

    return 0;
}

static int process_output_surface(AVCodecContext *avctx, AVPacket *pkt, MtencSurface *mtenc_surface)
{
    MtencContext *ctx = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &dl_fn->mtenc_funcs;

    MT_ENC_LOCK_BUFFER lock_params = { MTENCAPI_VERSION };
    MTENCSTATUS mt_status;
    int res = 0;
    const int max_wait_time = 500;
    enum AVPictureType pict_type;

#if CONFIG_D3D11VA
    if (mtenc_surface->completion_event && WaitForSingleObject(mtenc_surface->completion_event, 10000) == WAIT_FAILED)
    {
        res = mtenc_print_error(avctx, MT_ENC_ERR_GENERIC, "Failed waiting for the completion event");
        goto error;
    }
#endif

    lock_params.timeoutInMilliseconds = max_wait_time;
    mt_status = p_mtenc->mtEncLockOutputBuffer(ctx->mtencoder, &lock_params);
    if (mt_status != MT_ENC_SUCCESS) {
        res = mtenc_print_error(avctx, mt_status, "Failed locking bitstream buffer");
        goto error;
    }
    if(lock_params.outputBufferSizeInBytes == 0)
    {
        res = mtenc_print_error(avctx, MT_ENC_ERR_GENERIC, "Failed locking bitstream buffer, resolve 0 byte data");
        goto error;
    }

    res = ff_get_encode_buffer(avctx, pkt, lock_params.outputBufferSizeInBytes, 0);

    if (res < 0) {
        p_mtenc->mtEncUnlockOutputBuffer(ctx->mtencoder, lock_params.lockedOutputBuffer);
        goto error;
    }

    memcpy(pkt->data, lock_params.outputBufferPtr, lock_params.outputBufferSizeInBytes);

    mt_status = p_mtenc->mtEncUnlockOutputBuffer(ctx->mtencoder, lock_params.lockedOutputBuffer);
    if (mt_status != MT_ENC_SUCCESS) {
        res = mtenc_print_error(avctx, mt_status, "Failed unlocking bitstream buffer, expect the gates of mordor to open");
        goto error;
    }

    if (mtenc_surface)
    {
        p_mtenc->mtEncUnmapResource(ctx->mtencoder, mtenc_surface->input_buffer);
    }

    switch (lock_params.pictureType) {
    case MT_ENC_PIC_TYPE_IDR:
        pkt->flags |= AV_PKT_FLAG_KEY;
    case MT_ENC_PIC_TYPE_I:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case MT_ENC_PIC_TYPE_P:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    case MT_ENC_PIC_TYPE_B:
        pict_type = AV_PICTURE_TYPE_B;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown picture type encountered, expect the output to be broken.\n");
        av_log(avctx, AV_LOG_ERROR, "Please report this error and include as much information on how to reproduce it as possible.\n");
        res = AVERROR_EXTERNAL;
        goto error;
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = pict_type;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    res = mtenc_set_timestamp(avctx, &lock_params, pkt);

    if (res < 0)
        goto error;

    return 0;

error:
    return res;
}

static int output_ready(AVCodecContext *avctx, int flush)
{
    MtencContext *ctx = avctx->priv_data;
    int nb_ready, nb_pending;

    nb_ready   = av_fifo_can_read(ctx->output_surface_ready_queue);
    nb_pending = av_fifo_can_read(ctx->output_surface_queue);

    if (flush)
        return nb_ready > 0;

    return (nb_ready > 0) && (nb_ready + nb_pending > ctx->async_depth);
}

static int prepare_sei_data_array(AVCodecContext *avctx, const AVFrame *frame)
{
    MtencContext *ctx = avctx->priv_data;
    int sei_count = 0;
    int i, res;

    if (ctx->a53_cc && av_frame_get_side_data(frame, AV_FRAME_DATA_A53_CC)) {
        void *a53_data = NULL;
        size_t a53_size = 0;

        if (ff_alloc_a53_sei(frame, 0, &a53_data, &a53_size) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Not enough memory for closed captions, skipping\n");
        }

        if (a53_data) {
            void *tmp = av_fast_realloc(ctx->sei_data,
                                        &ctx->sei_data_size,
                                        (sei_count + 1) * sizeof(*ctx->sei_data));
            if (!tmp) {
                av_free(a53_data);
                res = AVERROR(ENOMEM);
                goto error;
            } else {
                ctx->sei_data = tmp;
                ctx->sei_data[sei_count].payloadSize = (uint32_t)a53_size;
                ctx->sei_data[sei_count].payload = (uint8_t*)a53_data;
#if CONFIG_AV1_MTENC_ENCODER
                if (avctx->codec->id == AV_CODEC_ID_AV1)
                    ctx->sei_data[sei_count].payloadType = AV1_METADATA_TYPE_ITUT_T35;
                else
#endif
                    ctx->sei_data[sei_count].payloadType = SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35;
                sei_count++;
            }
        }
    }

    if (ctx->s12m_tc && av_frame_get_side_data(frame, AV_FRAME_DATA_S12M_TIMECODE)) {
        void *tc_data = NULL;
        size_t tc_size = 0;

        if (ff_alloc_timecode_sei(frame, avctx->framerate, 0, &tc_data, &tc_size) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Not enough memory for timecode sei, skipping\n");
        }

        if (tc_data) {
            void *tmp = av_fast_realloc(ctx->sei_data,
                                        &ctx->sei_data_size,
                                        (sei_count + 1) * sizeof(*ctx->sei_data));
            if (!tmp) {
                av_free(tc_data);
                res = AVERROR(ENOMEM);
                goto error;
            } else {
                ctx->sei_data = tmp;
                ctx->sei_data[sei_count].payloadSize = (uint32_t)tc_size;
                ctx->sei_data[sei_count].payload = (uint8_t*)tc_data;
#if CONFIG_AV1_MTENC_ENCODER
                if (avctx->codec->id == AV_CODEC_ID_AV1)
                    ctx->sei_data[sei_count].payloadType = AV1_METADATA_TYPE_TIMECODE;
                else
#endif
                    ctx->sei_data[sei_count].payloadType = SEI_TYPE_TIME_CODE;
                sei_count++;
            }
        }
    }

    for (i = 0; i < frame->nb_side_data; i++) {
        AVFrameSideData *side_data = frame->side_data[i];
        void *tmp;

        if (side_data->type != AV_FRAME_DATA_SEI_UNREGISTERED)
            continue;

        tmp = av_fast_realloc(ctx->sei_data,
                              &ctx->sei_data_size,
                              (sei_count + 1) * sizeof(*ctx->sei_data));
        if (!tmp) {
            res = AVERROR(ENOMEM);
            goto error;
        } else {
            ctx->sei_data = tmp;
            ctx->sei_data[sei_count].payloadSize = side_data->size;
            ctx->sei_data[sei_count].payloadType = SEI_TYPE_USER_DATA_UNREGISTERED;
            ctx->sei_data[sei_count].payload = av_memdup(side_data->data, side_data->size);

            if (!ctx->sei_data[sei_count].payload) {
                res = AVERROR(ENOMEM);
                goto error;
            }

            sei_count++;
        }
    }

    return sei_count;

error:
    for (i = 0; i < sei_count; i++)
        av_freep(&(ctx->sei_data[i].payload));

    return res;
}

static void reconfig_encoder(AVCodecContext *avctx, const AVFrame *frame)
{
    MtencContext *ctx = avctx->priv_data;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &ctx->mtenc_dload_funcs.mtenc_funcs;
    MTENCSTATUS ret;

    MT_ENC_RECONFIGURE_PARAMS params = { 0 };
    int reconfig_bitrate = 0;

    params.version = MTENCAPI_VERSION;
    params.reInitParams = ctx->init_encode_params;

    if (ctx->rc != MT_ENC_PARAMS_RC_CONSTQP) {
        if (avctx->bit_rate > 0 && params.reInitParams.encodeConfig->rcParams.averageBitRate != avctx->bit_rate) {
            av_log(avctx, AV_LOG_VERBOSE,
                   "avg bitrate change: %d -> %d\n",
                   params.reInitParams.encodeConfig->rcParams.averageBitRate,
                   (uint32_t)avctx->bit_rate);

            params.reInitParams.encodeConfig->rcParams.averageBitRate = avctx->bit_rate;
            reconfig_bitrate = 1;
        }

        if (avctx->rc_max_rate > 0 && ctx->encode_config.rcParams.maxBitRate != avctx->rc_max_rate) {
            av_log(avctx, AV_LOG_VERBOSE,
                   "max bitrate change: %d -> %d\n",
                   params.reInitParams.encodeConfig->rcParams.maxBitRate,
                   (uint32_t)avctx->rc_max_rate);

            params.reInitParams.encodeConfig->rcParams.maxBitRate = avctx->rc_max_rate;
            reconfig_bitrate = 1;
        }
    }

    if (reconfig_bitrate) {
        ret = p_mtenc->mtEncReconfigureEncoder(ctx->mtencoder, &params);
        if (ret != MT_ENC_SUCCESS) {
            mtenc_print_error(avctx, ret, "failed to reconfigure mtenc");
        } else if (reconfig_bitrate) {
                ctx->encode_config.rcParams.averageBitRate = params.reInitParams.encodeConfig->rcParams.averageBitRate;
                ctx->encode_config.rcParams.maxBitRate = params.reInitParams.encodeConfig->rcParams.maxBitRate;
        }
    }
}

static int mtenc_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    MTENCSTATUS mt_status;
    MtencSurface *tmp_out_surf, *in_surf;
    int res;
    int sei_count = 0;
    AVFrameSideData* sd;

    MtencContext *ctx = avctx->priv_data;
    MtencDynLoadFunctions *dl_fn = &ctx->mtenc_dload_funcs;
    MT_ENCODE_API_FUNCTION_LIST *p_mtenc = &dl_fn->mtenc_funcs;

    MT_ENC_PIC_PARAMS pic_params = { MTENCAPI_VERSION };

#if CONFIG_D3D11VA
    if (!ctx->d3d11_device || !ctx->mtencoder)
#else
    // FIXME
    if (!ctx->mtencoder)
#endif
        return AVERROR(EINVAL);

    if (frame && frame->buf[0]) {
        in_surf = get_free_frame(ctx);
        if (!in_surf)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to get free frame\n");
            return AVERROR(ENOMEM);
        }

        reconfig_encoder(avctx, frame);

        #if CONFIG_D3D11VA
        // copy the input data to a surface construct
        res = mtenc_upload_frame_d3d11va(avctx, frame, in_surf);
        #else
        // FIXME
        res = mtenc_upload_frame(avctx, frame, in_surf);
        #endif

        if (res)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to upload frame\n");
            return res;
        }

        // roi settings
        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);
        if(sd)
        {
            MT_ENC_CONFIG *cc                      = &ctx->encode_config;
            int bit_depth_minus_8 = 0;
            int qp_range = 51;
            const AVRegionOfInterest *roi = (const AVRegionOfInterest*)sd->data;
            uint32_t roi_size = roi->self_size;

            switch (ctx->init_encode_params.encodeID)
            {
                case MT_ENC_CODEC_ID_HEVC:
                    bit_depth_minus_8 = cc->encodeCodecConfig.hevcConfig.pixelBitDepthMinus8;
                    break;
                case MT_ENC_CODEC_ID_AV1:
                    bit_depth_minus_8 = cc->encodeCodecConfig.av1Config.inputPixelBitDepthMinus8;
                    break;
                default:
                    bit_depth_minus_8 = cc->encodeCodecConfig.h264Config.pixelBitDepthMinus8;
                    break;
            }
            qp_range = 51 + 6 * bit_depth_minus_8;

            if (!roi_size || sd->size % roi_size != 0)
            {
                av_log(ctx, AV_LOG_ERROR, "Invalid AVRegionOfInterest.self_size.\n");
                return AVERROR(EINVAL);
            }
            pic_params.roiNumber = sd->size / roi_size;

            pic_params.roi = av_malloc_array(pic_params.roiNumber, sizeof(MT_ENCODER_ROI));
            if(!pic_params.roi)
            {
                return AVERROR(ENOMEM);
            }

            for (int i = 0; i < pic_params.roiNumber; i++)
            {
                float qoffset;
                roi = (const AVRegionOfInterest*)(sd->data + roi_size * i);

                if (roi->qoffset.den == 0)
                {
                    av_free(pic_params.roi);
                    av_log(ctx, AV_LOG_ERROR, "AVRegionOfInterest.qoffset.den must not be zero.\n");
                    return AVERROR(EINVAL);
                }
                qoffset = roi->qoffset.num * 1.0f / roi->qoffset.den;
                qoffset = av_clipf(qoffset * qp_range, -qp_range, +qp_range);

                pic_params.roi[i].roi_value = (int8_t)qoffset;
                pic_params.roi[i].roi_rectangle.x = roi->left;
                pic_params.roi[i].roi_rectangle.y = roi->top;
                pic_params.roi[i].roi_rectangle.width = roi->right - roi->left;
                pic_params.roi[i].roi_rectangle.height = roi->bottom - roi->top;
            }
        }

        pic_params.inputBuffer = in_surf->input_buffer;
        pic_params.bufferFmt = in_surf->format;
        pic_params.inputWidth = in_surf->width;
        pic_params.inputHeight = in_surf->height;
        pic_params.inputPitch = in_surf->pitch;
        pic_params.outputBuffer = in_surf->output_surface;
        pic_params.completionEvent = in_surf->completion_event;

        if (ctx->forced_idr >= 0 && frame->pict_type == AV_PICTURE_TYPE_I) {
            pic_params.encodePicFlags =
                ctx->forced_idr ? MT_ENC_PIC_FLAG_FORCEIDR : 0;
        } else {
            pic_params.encodePicFlags = 0;
        }
        pic_params.inputTimeStamp = frame->pts;

        if (ctx->extra_sei) {
            res = prepare_sei_data_array(avctx, frame);
            if (res < 0)
                return res;
            sei_count = res;
        }

        if (sei_count > 0) {
            pic_params.seiPayloadArray = ctx->sei_data;
            pic_params.seiPayloadArrayCnt = sei_count;
        }
    } else {
        pic_params.encodePicFlags = MT_ENC_PIC_FLAG_EOS;
    }

    // encode an frame
    mt_status = p_mtenc->mtEncEncodeFrame(ctx->mtencoder, &pic_params);

    for (int i = 0; i < sei_count; i++)
        av_freep(&(ctx->sei_data[i].payload));

    if(pic_params.roiNumber > 0 && pic_params.roi)
    {
        av_free(pic_params.roi);
        pic_params.roi = NULL;
    }

    if (mt_status != MT_ENC_SUCCESS &&
        mt_status != MT_ENC_ERR_NEED_MORE_INPUT)
        return mtenc_print_error(avctx, mt_status, "EncodePicture failed!");

    if (frame && frame->buf[0]) {
        av_fifo_write(ctx->output_surface_queue, &in_surf, 1);
    }

    /* all the pending buffers are now ready for output */
    if (mt_status == MT_ENC_SUCCESS) {
        while (av_fifo_read(ctx->output_surface_queue, &tmp_out_surf, 1) >= 0) {
            av_fifo_write(ctx->output_surface_ready_queue, &tmp_out_surf, 1);
        }
    }

    return 0;
}

int ff_mtenc_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    MtencSurface *tmp_out_surf;
    int res;
    static int send_frame_count = 0, recv_frame_count = 0;
    MtencContext *ctx = avctx->priv_data;

    AVFrame *frame = ctx->frame;

#if CONFIG_D3D11VA
    if (!ctx->d3d11_device || !ctx->mtencoder)
#else
    // FIXME
    if (!ctx->mtencoder)
#endif
        return AVERROR(EINVAL);

    if (!frame->buf[0]) {
        // get input data
        res = ff_encode_get_frame(avctx, frame);
        if (res < 0 && res != AVERROR_EOF)
        {
            if(res != AVERROR(EAGAIN))
            {
                av_log(avctx, AV_LOG_WARNING, "ff_encode_get_frame ret: %d\n", res);
            }
            return res;
        }
    }

    // encode a frame
    res = mtenc_send_frame(avctx, frame);
    if (res < 0) {
        av_log(avctx, AV_LOG_WARNING, "mtenc_send_frame ret: %d\n", res);
        if (res != AVERROR(EAGAIN)) {
            return res;
        }
    } else {
        av_frame_unref(frame);
    }

    send_frame_count++;
    if (output_ready(avctx, avctx->internal->draining)) {
        //av_log(avctx, AV_LOG_DEBUG, "Send frame %d, recv frame %d\n", send_frame_count, recv_frame_count);
        av_fifo_read(ctx->output_surface_ready_queue, &tmp_out_surf, 1);

        // feed the output data to a AVPacket struct
        res = process_output_surface(avctx, pkt, tmp_out_surf);

        if (res)
        {
            av_log(avctx, AV_LOG_ERROR, "Error processing output surface, res=%d\n", res);
            return res;
        }
        av_fifo_write(ctx->unused_surface_queue, &tmp_out_surf, 1);
        recv_frame_count++;
    } else if (avctx->internal->draining) {
        return AVERROR_EOF;
    } else {
        return AVERROR(EAGAIN);
    }

    return 0;
}

av_cold void ff_mtenc_encode_flush(AVCodecContext *avctx)
{
    //MtencContext *ctx = avctx->priv_data;
    av_log(avctx, AV_LOG_INFO, "ff_mtenc_encode_flush\n");
    mtenc_send_frame(avctx, NULL);
}
