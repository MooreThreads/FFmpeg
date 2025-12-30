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

#ifndef AVCODEC_MTENC_H
#define AVCODEC_MTENC_H

#include "config.h"

#if CONFIG_D3D11VA
#define COBJMACROS
#include "libavutil/hwcontext_d3d11va.h"
#else
typedef void ID3D11Device;
typedef void ID3D11DeviceContext;
#endif

#include "ffmtcodec/mtEncodeAPI.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "hwconfig.h"

#include "avcodec.h"

#define MAX_REGISTERED_FRAMES 64
#define RC_MODE_DEPRECATED 0x800000
#define RCD(rc_mode) ((rc_mode) | RC_MODE_DEPRECATED)

#define MTENCAPI_CHECK_VERSION(major, minor) \
    ((major) < MTENCAPI_MAJOR_VERSION || ((major) == MTENCAPI_MAJOR_VERSION && (minor) <= MTENCAPI_MINOR_VERSION))

// SDK 8.1 compile time feature checks
#if MTENCAPI_CHECK_VERSION(8, 1)
#define MTENC_HAVE_BFRAME_REF_MODE
#define MTENC_HAVE_QP_MAP_MODE
#endif

// SDK 9.0 compile time feature checks
#if MTENCAPI_CHECK_VERSION(9, 0)
#define MTENC_HAVE_HEVC_BFRAME_REF_MODE
#endif

// SDK 9.1 compile time feature checks
#if MTENCAPI_CHECK_VERSION(9, 1)
#define MTENC_HAVE_MULTIPLE_REF_FRAMES
#define MTENC_HAVE_CUSTREAM_PTR
#define MTENC_HAVE_GETLASTERRORSTRING
#endif

// SDK 10.0 compile time feature checks
#if MTENCAPI_CHECK_VERSION(10, 0)
#define MTENC_HAVE_NEW_PRESETS
#define MTENC_HAVE_MULTIPASS
#define MTENC_HAVE_LDKFS
#define MTENC_HAVE_H264_LVL6
#endif

typedef struct MtencSurface
{
    MT_ENC_INPUT_PTR input_buffer;    // data can be sent directly to encode
    void *input_surface;            // input data with raw format. Need to use map function.
    int width;
    int height;
    int pitch;

    MT_ENC_OUTPUT_PTR output_surface;
    MT_ENC_BUFFER_FORMAT format;
    void *completion_event;
} MtencSurface;

typedef MTENCSTATUS(MTENCAPI * MTEncodeAPIGetMaxSupportedVersion_Type)(uint32_t*);
typedef MTENCSTATUS(MTENCAPI * MTEncodeAPICreateInstance_Type)(MT_ENCODE_API_FUNCTION_LIST*);

typedef struct MtencFunctions
{
    MTEncodeAPIGetMaxSupportedVersion_Type  pMtEncodeAPIGetMaxSupportedVersion;
    MTEncodeAPICreateInstance_Type          pMTEncodeAPICreateInstance;
}MtencFunctions;

typedef struct MtencDynLoadFunctions
{
    MtencFunctions mtenc_dl;
    MT_ENCODE_API_FUNCTION_LIST mtenc_funcs;
    void* mtenc_module;
} MtencDynLoadFunctions;

enum {
    PRESET_DEFAULT = 0,
    PRESET_SLOW,
    PRESET_MEDIUM,
    PRESET_FAST,
    PRESET_HP,
    PRESET_HQ,
    PRESET_BD ,
    PRESET_LOW_LATENCY_DEFAULT ,
    PRESET_LOW_LATENCY_HQ ,
    PRESET_LOW_LATENCY_HP,
    PRESET_LOSSLESS_DEFAULT,
    PRESET_LOSSLESS_HP,
#ifdef MTENC_HAVE_NEW_PRESETS
    PRESET_P1,
    PRESET_P2,
    PRESET_P3,
    PRESET_P4,
    PRESET_P5,
    PRESET_P6,
    PRESET_P7,
#endif
};

enum {
    MT_ENC_HEVC_PROFILE_MAIN,
    MT_ENC_HEVC_PROFILE_MAIN_10,
    MT_ENC_HEVC_PROFILE_REXT,
};

enum {
    MTENC_LOWLATENCY = 1,
    MTENC_LOSSLESS   = 2,
    MTENC_ONE_PASS   = 4,
    MTENC_TWO_PASSES = 8,

    MTENC_DEPRECATED_PRESET = 0x8000,
};

enum {
    LIST_DEVICES = -2,
    ANY_DEVICE,
};

typedef struct MtencContext
{
    AVClass *avclass;

    MtencDynLoadFunctions mtenc_dload_funcs;

    MT_ENC_INIT_PARAMS init_encode_params;
    MT_ENC_CONFIG encode_config;
    ID3D11Device *d3d11_device;
    ID3D11DeviceContext* d3d11_context;

    AVFrame *frame;

    int nb_surfaces;
    MtencSurface *surfaces;
    void *system_surface;

    AVFifo *unused_surface_queue;
    AVFifo *output_surface_queue;
    AVFifo *output_surface_ready_queue;

    MT_ENC_SEI_PAYLOAD* sei_data;
    int sei_data_size;
    
    struct {
        void *ptr;
        int ptr_index;
        int mapped;
        MT_ENC_MAP_RESOURCE in_map;
    } registered_frames[MAX_REGISTERED_FRAMES];
    int nb_registered_frames;

    /* the actual data pixel format, different from
     * AVCodecContext.pix_fmt when using hwaccel frames on input */
    enum AVPixelFormat data_pix_fmt;

    int support_dyn_bitrate;

    void *mtencoder;

    int preset;
    int profile;
    int level;
    int tier;
    int rc;
    int cbr;
    int device;
    int flags;
    int async_depth;
    int forced_idr;
    int init_qp_p;
    int init_qp_b;
    int init_qp_i;
    int cqp;
    int bdepth;
    int coder;
    int a53_cc;
    int s12m_tc;
    int intra_refresh_mode;
    int intra_refresh_arg;
    int refs;
    int nb_temporal_layers;
    int slice_mode;
    int extra_sei;
    int tile_rows;
    int tile_cols;
    int timing_info;
#if CONFIG_D3D11VA
    AVD3D11VADeviceContext* d3d11vaDeviceContext;
#endif

} MtencContext;

int ff_mtenc_encode_init(AVCodecContext *avctx);

int ff_mtenc_encode_close(AVCodecContext *avctx);

int ff_mtenc_receive_packet(AVCodecContext *avctx, AVPacket *pkt);

void ff_mtenc_encode_flush(AVCodecContext *avctx);

extern const enum AVPixelFormat ff_mtenc_pix_fmts[];
extern const AVCodecHWConfigInternal *const ff_mtenc_hw_configs[];

#endif /* AVCODEC_MTENC_H */
