/*
 * Chinese AVS2-Video (GY/T 299.1-2016 or IEEE 1857.4-2018) decoder.
 * Copyright (c) 2022 JianfengZheng <jianfeng.zheng@mthreads.com>
 * Copyright (C) 2022 Zhao Zhili, <zhilizhao@tencent.com>
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
 * @file
 * Chinese AVS2-Video (GY/T 299.1-2016 or IEEE 1857.4-2018) definitions
 * @author JianfengZheng <jianfeng.zheng@mthreads.com>
 */

#ifndef AVCODEC_CAVS2_H
#define AVCODEC_CAVS2_H

#include "libavutil/rational.h"
#include "libavutil/frame.h"
#include "avcodec.h"

#define AVS2_MAX_REF_COUNT      7       /* max reference frame number */
#define AVS2_MAX_DPB_COUNT      16      /* max DPB count including current frame */
#define AVS2_MAX_RCS_COUNT      32      /* max number of RCS */
#define AVS2_MINI_SIZE          8       /* Annex B.2 of GY/T 299.1-2016 */

#define AVS2_SLICE_MAX_START_CODE    0x000001AF

enum {
    AVS2_SEQ_START_CODE         = 0xB0,
    AVS2_SEQ_END_CODE           = 0xB1,
    AVS2_USER_DATA_START_CODE   = 0xB2,
    AVS2_INTRA_PIC_START_CODE   = 0xB3,
    // reserved                 = 0xB4,
    AVS2_EXTENSION_START_CODE   = 0xB5,
    AVS2_INTER_PIC_START_CODE   = 0xB6,
};

#define AVS2_ISPIC(x)  ((x) == AVS2_INTRA_PIC_START_CODE || (x) == AVS2_INTER_PIC_START_CODE)
#define AVS2_ISUNIT(x) ((x) == AVS2_SEQ_START_CODE || AVS2_ISPIC(x))

enum AVS2StartCode {
    AVS2_STC_SEQ_HEADER = 0x000001B0,   /* sequence header  start code */
    AVS2_STC_SEQ_END    = 0x000001B1,   /* sequence end     start code */
    AVS2_STC_USER_DATA  = 0x000001B2,   /* user data        start code */
    AVS2_STC_INTRA_PIC  = 0x000001B3,   /* intra picture    start code */
    AVS2_STC_EXTENSION  = 0x000001B5,   /* extension        start code */
    AVS2_STC_INTER_PIC  = 0x000001B6,   /* inter picture    start code */
    AVS2_STC_VIDEO_EDIT = 0x000001B7,   /* video edit       start code */
    AVS2_STC_SLICE_MIN  = 0x00000100,   /* min slice        start code */
    AVS2_STC_SLICE_MAX  = 0x0000018F    /* max slice        start code */
};

static inline int ff_avs2_valid_slice_stc(uint32_t stc) {
    return (stc >= AVS2_STC_SLICE_MIN && stc <= AVS2_STC_SLICE_MAX);
}

static inline int ff_avs2_valid_start_code(uint32_t stc) {
    return (stc >= AVS2_STC_SEQ_HEADER && stc <= AVS2_STC_VIDEO_EDIT) || 
            ff_avs2_valid_slice_stc(stc);
}

enum AVS2ExtType {
    AVS2_EXT_SEQ_DISPLAY    = 0b0010,
    AVS2_EXT_TEMPORAL_SCALE = 0b0011,
    AVS2_EXT_COPYRIGHT      = 0b0100,
    AVS2_EXT_PIC_DISPLAY    = 0b0111,
    AVS2_EXT_MASTERING      = 0b1010,   /* mastering_display_and_content_metadata_extension */
    AVS2_EXT_CAMERA_PARAM   = 0b1011,
    AVS2_EXT_ROI_PARAM      = 0b1100,
};

enum Avs2Profile {
    AVS2_PROFILE_MAIN_PIC   = 0x12,
    AVS2_PROFILE_MAIN       = 0x20,
    AVS2_PROFILE_MAIN10     = 0x22,
    AVS2_PROFILE_MAIN_10    = 0x22,
};

extern const AVRational ff_avs2_frame_rate_tab[16];


enum Avs2Level {
    AVS2_LEVEL_FORBIDDEN    = 0x00,

    AVS2_LEVEL_2_0_15       = 0x10,     /* 352X288, 1500Kbps */
    AVS2_LEVEL_2_0_30       = 0x12,     /* 352X288, 2000Kbps */
    AVS2_LEVEL_2_0_60       = 0x14,     /* 352X288, 2500Kbps */

    AVS2_LEVEL_4_0_30       = 0x20,     /* 720x576,  6Mbps, 30fps */
    AVS2_LEVEL_4_0_60       = 0x22,     /* 720x576, 10Mbps, 60fps */

    AVS2_LEVEL_6_0_30       = 0x40,     /* 2048x1152,  12Mbps,  30fps */
    AVS2_LEVEL_6_2_30       = 0x42,     /* 2048x1152,  30Mbps,  30fps */
    AVS2_LEVEL_6_0_60       = 0x44,     /* 2048x1152,  20Mbps,  60fps */
    AVS2_LEVEL_6_2_60       = 0x46,     /* 2048x1152,  50Mbps,  60fps */
    AVS2_LEVEL_6_0_120      = 0x48,     /* 2048x1152,  25Mbps, 120fps */
    AVS2_LEVEL_6_2_120      = 0x4a,     /* 2048x1152, 100Mbps, 120fps */

    AVS2_LEVEL_8_0_30       = 0x50,     /* 4096x2036,  25Mbps,  30fps */
    AVS2_LEVEL_8_2_30       = 0x52,     /* 4096x2036, 100Mbps,  30fps */
    AVS2_LEVEL_8_0_60       = 0x54,     /* 4096x2036,  40Mbps,  60fps */
    AVS2_LEVEL_8_2_60       = 0x56,     /* 4096x2036, 160Mbps,  60fps */
    AVS2_LEVEL_8_0_120      = 0x58,     /* 4096x2036,  60Mbps, 120fps */
    AVS2_LEVEL_8_2_120      = 0x5a,     /* 4096x2036, 240Mbps, 120fps */

    AVS2_LEVEL_10_0_30      = 0x60,     /* 8192x4608,  60Mbps,  30fps */
    AVS2_LEVEL_10_2_30      = 0x62,     /* 8192x4608, 240Mbps,  30fps */
    AVS2_LEVEL_10_0_60      = 0x64,     /* 8192x4608, 120Mbps,  60fps */
    AVS2_LEVEL_10_2_60      = 0x66,     /* 8192x4608, 480Mbps,  60fps */
    AVS2_LEVEL_10_0_120     = 0x68,     /* 8192x4608, 240Mbps, 120fps */
    AVS2_LEVEL_10_2_120     = 0x6a,     /* 8192x4608, 800Mbps, 120fps */
};

typedef struct AVS2LevelLimit {
    enum Avs2Level level;

    int         width;
    int         height;
    int         frame_rate;
    int         nb_slice;
    uint64_t    sample_rate;
    uint64_t    bit_rate;
    uint64_t    bbv_size;
} AVS2LevelLimit;

enum Avs2ChromaFormat {
    AVS2_CHROMA_YUV_400 = 0,    /* not supported */
    AVS2_CHROMA_YUV_420 = 1,
    AVS2_CHROMA_YUV_422 = 2,    /* not supported */
};

enum Avs2AspectRatio {
    AVS2_SAR_1_1        = 1,    /* SAR 1:1 */
    AVS2_DAR_4_3        = 2,    /* DAR 4:3 */
    AVS2_DAR_16_9       = 3,    /* DAR 16:9 */
    AVS2_DAR_221_100    = 4,    /* DAR 2.21:1 */
};

enum Avs2FrameRate {
    AVS2_FR_23_976      = 1,    /* 24000/1001=23.976 */
    AVS2_FR_24          = 2,
    AVS2_FR_25          = 3,
    AVS2_FR_29_970      = 4,    /* 30000/1001=29.970 */
    AVS2_FR_30          = 5,
    AVS2_FR_50          = 6,
    AVS2_FR_59_940      = 7,    /* 60000/1001=59.940 */
    AVS2_FR_60          = 8,
    AVS2_FR_100         = 9,
    AVS2_FR_120         = 10,
    AVS2_FR_200         = 11,
    AVS2_FR_240         = 12,
    AVS2_FR_300         = 13,

    AVS2_FR_MIN         = AVS2_FR_23_976,
    AVS2_FR_MAX         = AVS2_FR_300,
};

enum Avs2PicCodingType {
    AVS2_PCT_P       = 0b01,
    AVS2_PCT_B       = 0b10,
    AVS2_PCT_F       = 0b11,
};

enum Avs2PicType {
    AVS2_PIC_UNKNOWN = -1,
    AVS2_PIC_I       = 0,     // AVS2_PCT_I, ScenePicFlag:0
    AVS2_PIC_G       = 5,     // AVS2_PCT_I, ScenePicFlag:1, SceneOutFlag:1
    AVS2_PIC_GB      = 6,     // AVS2_PCT_I, ScenePicFlag:1, SceneOutFlag:0

    AVS2_PIC_P       = 1,     // AVS2_PCT_P, ScenePredFlag:0
    AVS2_PIC_S       = 4,     // AVS2_PCT_P, ScenePredFlag:1

    AVS2_PIC_B       = 2,     // AVS2_PCT_B
    AVS2_PIC_F       = 3,     // AVS2_PCT_F
};

enum Avs2FrameStructure {
    AVS2_FIELD_SEPARATED    = 0,
    AVS2_FIELD_INTERLEAVED  = 1,
};

/* Weight Quant Matrix */
typedef struct AVS2WQMatrix {
    uint8_t         m44[16];
    uint8_t         m88[64];
} AVS2WQMatrix;

/* reference configuration set */
typedef struct AVS2RefCfgSet {
    int     b_ref_by_others;        /* referenced by others */
    int     n_ref;                  /* number of reference picture */
    int     ref_delta_doi[8];       /* delta doi (decode_order_index) of ref pic */
    int     n_rm;                   /* number of removed picture */
    int     rm_delta_doi[8];        /* delta doi (decode_order_index) of removed pic */         
} AVS2RefCfgSet;


typedef struct AVS2SeqDisplayExt {
    uint32_t    extension_id;
    int         video_format;

    uint32_t    b_full_range    : 4;
    uint32_t    b_color_desc    : 4;
    uint32_t    color_primaries : 8;
    uint32_t    color_transfer  : 8;
    uint32_t    color_matrix    : 8;

    uint32_t    display_w       : 16;
    uint32_t    display_h       : 16;

    int         b_td_mode;
    int         td_packing_mode;
    int         b_view_reverse;
} AVS2SeqDisplayExt;

typedef struct AVS2TemporalScaleExt {
    uint32_t    extension_id;
    int         n_level;
    struct {
        AVRational  framerate;
        int64_t     bitrate;
    } level[8];
} AVS2TemporalScaleExt;

typedef struct AVS2CopyrightExt {
    uint32_t    extension_id;
    int         b_flag      : 1;
    int         b_original  : 1;
    int         copy_id;
    uint64_t    copy_number;
} AVS2CopyrightExt;

typedef struct AVS2PicDisplayExt {
    uint32_t    extension_id;
    int         n_offset;
    int32_t     offset[3][2];       /* offset[][0:h, 1:v]*/
} AVS2PicDisplayExt;

typedef struct AVS2SeqHeader {
    int     profile_id;             /* profile ID, enum AVS2Profile */
    int     level_id;               /* level   ID, enum AVS2Level */
    int     b_progressive;          /* progressive sequence (0: interlace, 1: progressive) */
    int     b_field_coding;         /* field coded sequence */
    int     width;                  /* image width */
    int     height;                 /* image height */
    int     chroma_format;          /* chroma format(1: 4:2:0, 2: 4:2:2) */
    int     sample_bit_depth;       /* sample precision, 8 / 10 */
    int     output_bit_depth;       /* encoded precision, 8 / 10 */
    int     aspect_ratio_code;      /* enum Avs2AspectRatio */
    int     frame_rate_code;        /* frame rate code, mpeg12 [1...8] */
    int64_t bitrate;                /* bitrate (bps) */
    int     b_low_delay;            /* has no b frames */
    int     b_has_temporal_id;      /* temporal id exist flag */
    int     bbv_buffer_size;
    int     log2_lcu_size;          /* largest coding block size */
    int     b_enable_wq;            /* weight quant enable flag */
    AVS2WQMatrix    wqm;            /* weighted quantization matrix */

    int     b_disable_scene_pic;
    int     b_multi_hypothesis_skip;
    int     b_dual_hypothesis_prediction;
    int     b_weighted_skip;
    int     b_amp;                  /* enable asymmetric_motion_partitions */
    int     b_nsqt;                 /* enable nonsquare_quadtree_transform */
    int     b_nsip;                 /* enable nonsquare_intra_prediction */
    int     b_2nd_transform;        /* enable secondary_transform  */
    int     b_sao;                  /* enable sample_adaptive_offset */
    int     b_alf;                  /* enable adaptive_loop_filter */
    int     b_pmvr;
    int             n_rcs;          /* num of reference_configuration_set */
    AVS2RefCfgSet   rcs[AVS2_MAX_RCS_COUNT+1];
    int     output_reorder_delay;
    int     b_cross_slice_loop_filter;
} AVS2SeqHeader;

typedef struct AVS2AlfParam {
    uint8_t b_enable[3];        // for YUV separate
    struct {
        int n_filter;
        int region_distance[16];
        int16_t coeff[16][9];
    } luma;
    struct {
        int16_t coeff[9];
    } chroma[2];
} AVS2AlfParam;

typedef struct AVS2PicHeader {
    int         b_intra;

    uint32_t    bbv_delay;

    union {
        struct /* intra_data */ {
            uint32_t b_time_code    : 1;
            uint32_t time_code_hh   : 5;    /* time code hours */
            uint32_t time_code_mm   : 6;    /* time code minutes */
            uint32_t time_code_ss   : 6;    /* time code seconds */
            uint32_t time_code_ff   : 6;    /* time code frames */

            uint16_t b_scene_pic;
            uint16_t b_scene_pic_output;
        };
        struct /* inter_data */ {
            int pic_coding_type;
            int b_scene_pred;
            int b_scene_ref;
            int b_random_access;            /* random accesss decodable */
        };
    };

    int         doi;                        /* decode_order_index */
    int         temporal_id;
    int         output_delay;               /* picture_output_delay */
    int         b_use_rcs;
    int         rcs_index;
    int         bbv_check_times;

    int         b_progressive_frame;
    int         b_picture_structure;        /* enum Avs2FrameStructure */
    int         b_top_field_first;
    int         b_repeat_first_field;
    int         b_top_field_picture;

    int         b_fixed_qp;
    int         pic_qp;

    /* loop filter */
    int         b_disable_lf;
    int         b_lf_param;
    int         lf_alpha_offset;
    int         lf_beta_offset;

    /* quant param */
    int         b_no_chroma_quant_param;
    int         cb_quant_delta;
    int         cr_quant_delta;

    int         b_enable_pic_wq;
    int         wq_data_index;
    int         wq_param_index;
    int         wq_model;
    int         wq_param_delta[2][6];
    AVS2WQMatrix    wqm;

    /**
     * @brief processed alf coeff: 0-15:luma, 16:cb, 17:cr
     * @see ff_avs2_process_alf_param()
     */
    int8_t      alf_coeff[18][9];
    int8_t      b_alf_enable[3];    // 0:Y, 1:U, 2:V
} AVS2PicHeader;

typedef struct AVS2SliceHeader {
    int         lcu_x;
    int         lcu_y;
    uint32_t    b_fixed_qp  : 16;
    uint32_t    slice_qp    : 16;
    uint8_t     b_sao[3];

    int         aec_byte_offset;    /* aec data offset in AVS2EsUnit */     
} AVS2SlcHeader;

/* element stream unit */
typedef struct AVS2EsUnit {
    uint32_t    start_code;
    size_t      data_start;         /* position right after start code */
    size_t      data_len;
} AVS2EsUnit;


typedef struct AVS2PacketSplit {
    AVS2EsUnit    *units;
    int         nb_units;
    int         nb_alloc;
} AVS2PacketSplit;

#define AVS2_DPB_MARK_NULL          (0)
#define AVS2_DPB_MARK_USED          (1 << 1)
///! the frame maybe referenced by others
#define AVS2_DPB_MARK_REF           (1 << 3)
///! the frame is not referenced any more
#define AVS2_DPB_MARK_UNREF         (1 << 4)
///! the frame could be output
#define AVS2_DPB_MARK_OUTPUTABLE    (1 << 5)
///! the frame has been output
#define AVS2_DPB_MARK_OUTPUTED      (1 << 6)

#define IS_DPB_FRAME_UNUSED(frm)    ((frm)->dpb_marks == AVS2_DPB_MARK_NULL)
#define IS_DPB_FRAME_INUSE(frm)     ((frm)->dpb_marks != AVS2_DPB_MARK_NULL)
#define IS_DPB_FRAME_MARK_AS_REF(frm)   ((frm)->dpb_marks & AVS2_DPB_MARK_REF)
#define IS_DPB_FRAME_OUTPUTABLE(frm)    (((frm)->dpb_marks & AVS2_DPB_MARK_OUTPUTABLE) \
                                        && !((frm)->dpb_marks & AVS2_DPB_MARK_OUTPUTED))
#define IS_DPB_FRAME_REMOVABLE(frm) (((frm)->dpb_marks & AVS2_DPB_MARK_OUTPUTED) \
                                    && ((frm)->dpb_marks & AVS2_DPB_MARK_UNREF))

typedef struct AVS2Frame AVS2Frame;
struct AVS2Frame {
    AVS2PicHeader       pic_header;

    int                 poi;
    int                 pic_type;           // enum AVS2_PIC_TYPE
    int                 n_slice;

    AVFrame             *frame;
    AVBufferRef         *hwaccel_priv_buf;
    void                *hwaccel_picture_private;

    uint32_t            dpb_marks;          // A combination of AVS2_DPB_MARK_XXX

    /**
     * reference picture list
     */
    int                 b_ref;
    int                 n_ref;
    int16_t             ref_doi[AVS2_MAX_REF_COUNT];
    int16_t             ref_poi[AVS2_MAX_REF_COUNT];
};

typedef struct AVS2Context
{
    const AVClass       *class;
    AVCodecContext      *avctx;
    enum AVPixelFormat  pix_fmt;

    int                 width;
    int                 height;
    int                 b_got_seq;
    AVS2SeqHeader       seq;
    AVS2PicHeader       pic;
    AVS2SlcHeader       slc;
    AVS2PacketSplit     pkt_split;

    /**
     * @brief Decoding picture buffer. See ff_avs2_dpb_xxx()
     */
    AVS2Frame           DPB[AVS2_MAX_DPB_COUNT];
    AVS2Frame           *curr_frame;

    AVS2SeqDisplayExt       seq_display_ext;
    AVS2TemporalScaleExt    tempo_scale_ext;
    AVS2CopyrightExt        seq_copyright_ext;
    AVS2PicDisplayExt       pic_display_ext;
    AVS2CopyrightExt        pic_copyright_ext;
} AVS2Context;

static inline int ff_avs2_get_min_cu_width(AVS2SeqHeader *seq) {
    return (seq->width + AVS2_MINI_SIZE - 1) / AVS2_MINI_SIZE;
}
static inline int ff_avs2_get_min_cu_height(AVS2SeqHeader *seq) {
    return (seq->height + AVS2_MINI_SIZE - 1) / AVS2_MINI_SIZE;
}
void ff_avs_get_cu_align_size(AVS2SeqHeader *seq, int *w, int *h);
int ff_avs2_get_max_dpb_size(AVS2SeqHeader *seq);
AVS2LevelLimit const *ff_ava2_get_level_desc(int level); 
void ff_avs2_set_default_wqm(AVS2WQMatrix* wqm);
void ff_avs2_set_default_seq_header(AVS2SeqHeader *seq);
void ff_avs2_set_default_pic_header(AVS2SeqHeader *seq, AVS2PicHeader *pic, int b_intra);
AVRational ff_avs2_frame_rate_c2q(int fr_code);
AVRational ff_avs2_get_sar(AVS2SeqHeader* seq);

static inline int ff_avs2_is_valid_qp(AVS2SeqHeader *seq, int qp) {
    return qp >= 0 && qp <= (63 + 8 * (seq->sample_bit_depth - 8));
}
static inline int ff_avs2_get_pic_poi(AVS2SeqHeader* seq, AVS2PicHeader *pic) {
    /* Spec. 9.2.2 */
    return pic->doi + pic->output_delay - seq->output_reorder_delay;
}

int ff_avs2_get_pic_type(AVS2PicHeader *pic);
const char* ff_avs2_pic_type_to_str(int /*enum Avs2PicType*/ type);
const char* ff_avs2_get_pic_type_str(AVS2PicHeader *pic);

/**
 * Split an input packet into element stream units.
 */
int ff_avs2_packet_split(AVS2PacketSplit *pkt, const uint8_t *data, int size, void *logctx);
/**
 * Free all the allocated memory in the packet.
 */
void ff_avs2_packet_uninit(AVS2PacketSplit *pkt);

int ff_avs2_remove_pseudo_code(uint8_t *dst, const uint8_t *src, int size);

#define AVS2_CHECK_RET(ret) if (ret < 0) { \
    av_log(h, AV_LOG_ERROR, "AVS2_CHECK_RET(%d) at line:%d, file:%s\n", ret, __LINE__, __FILE__); \
    return ret; \
}

#endif /* AVCODEC_CAVS2_H */
