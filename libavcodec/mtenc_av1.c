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

#include "libavutil/internal.h"

#include "avcodec.h"
#include "codec_internal.h"

#include "mtenc.h"

#define OFFSET(x) offsetof(MtencContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

#define MT_MAX_TILE_ROWS_AV1 16
#define MT_MAX_TILE_COLS_AV1 16

static const AVOption options[] = {
    { "profile",      "Set the encoding profile",           OFFSET(profile),      AV_OPT_TYPE_INT,   { .i64 = MT_ENC_CODEC_PROFILE_ID_AUTOSELECT }, MT_ENC_CODEC_PROFILE_ID_AUTOSELECT, MT_ENC_CODEC_PROFILE_ID_MAIN_AV1, VE, "profile" },
    { "auto",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_CODEC_PROFILE_ID_AUTOSELECT },  0, 0, VE, "profile" },
    { "main",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_CODEC_PROFILE_ID_MAIN_AV1 },    0, 0, VE, "profile" },
    { "level",        "Set the encoding level restriction", OFFSET(level),        AV_OPT_TYPE_INT,   { .i64 = MT_ENC_LEVEL_AV1_AUTOSELECT }, MT_ENC_LEVEL_AV1_2, MT_ENC_LEVEL_AV1_AUTOSELECT, VE, .unit = "level" },
    { "auto",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_AUTOSELECT },  0, 0, VE,  .unit = "level" },
    { "2",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_2 },           0, 0, VE,  .unit = "level" },
    { "2.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_2 },           0, 0, VE,  .unit = "level" },
    { "2.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_21 },          0, 0, VE,  .unit = "level" },
    { "2.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_22 },          0, 0, VE,  .unit = "level" },
    { "2.3",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_23 },          0, 0, VE,  .unit = "level" },
    { "3",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_3 },           0, 0, VE,  .unit = "level" },
    { "3.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_3 },           0, 0, VE,  .unit = "level" },
    { "3.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_31 },          0, 0, VE,  .unit = "level" },
    { "3.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_32 },          0, 0, VE,  .unit = "level" },
    { "3.3",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_33 },          0, 0, VE,  .unit = "level" },
    { "4",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_4 },           0, 0, VE,  .unit = "level" },
    { "4.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_4 },           0, 0, VE,  .unit = "level" },
    { "4.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_41 },          0, 0, VE,  .unit = "level" },
    { "4.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_42 },          0, 0, VE,  .unit = "level" },
    { "4.3",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_43 },          0, 0, VE,  .unit = "level" },
    { "5",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_5 },           0, 0, VE,  .unit = "level" },
    { "5.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_5 },           0, 0, VE,  .unit = "level" },
    { "5.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_51 },          0, 0, VE,  .unit = "level" },
    { "5.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_52 },          0, 0, VE,  .unit = "level" },
    { "5.3",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_53 },          0, 0, VE,  .unit = "level" },
    { "6",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_6 },           0, 0, VE,  .unit = "level" },
    { "6.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_6 },           0, 0, VE,  .unit = "level" },
    { "6.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_61 },          0, 0, VE,  .unit = "level" },
    { "6.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_62 },          0, 0, VE,  .unit = "level" },
    { "6.3",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_63 },          0, 0, VE,  .unit = "level" },
    { "7",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_7 },           0, 0, VE,  .unit = "level" },
    { "7.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_7 },           0, 0, VE,  .unit = "level" },
    { "7.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_71 },          0, 0, VE,  .unit = "level" },
    { "7.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_72 },          0, 0, VE,  .unit = "level" },
    { "7.3",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_LEVEL_AV1_73 },          0, 0, VE,  .unit = "level" },
    { "tier",         "Set the encoding tier",              OFFSET(tier),         AV_OPT_TYPE_INT,   { .i64 = MT_ENC_TIER_AV1_0 }, MT_ENC_TIER_AV1_0, MT_ENC_TIER_AV1_1, VE, .unit = "tier"},
    { "0",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_TIER_AV1_0 }, 0, 0, VE, .unit = "tier" },
    { "1",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_TIER_AV1_1 }, 0, 0, VE, .unit = "tier" },
    { "rc_mode",      "Set rate control mode",              OFFSET(rc),           AV_OPT_TYPE_INT,   { .i64 = -1 },                                  -1, INT_MAX, VE, "rc" },
    { "CQP",          "Constant-quality",                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_PARAMS_RC_CONSTQP },                   0, 0, VE, "rc" },
    { "VBR",          "Variable-bitrate",                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_PARAMS_RC_VBR },                       0, 0, VE, "rc" },
    { "CBR",          "Constant-bitrate",                   0,                    AV_OPT_TYPE_CONST, { .i64 = MT_ENC_PARAMS_RC_CBR },                       0, 0, VE, "rc" },
    { "constqp",      "Constant quantization parameter rate control method",
                                                            OFFSET(cqp),          AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 255, VE },
    { "b_depth",      "Maximum B-frame reference depth",    OFFSET(bdepth),       AV_OPT_TYPE_INT,   { .i64 = 1  }, 1, INT_MAX, VE },
    { "refs",         "Specifies the reference number of the P-frame, which ranges from 1 to 2. The default value is 1.",
                                                            OFFSET(refs),         AV_OPT_TYPE_INT,   { .i64 = 1 },                               1, 2,                               VE, "refs" },
    { "tile-rows",    "Number of tile rows to encode with", OFFSET(tile_rows),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, MT_MAX_TILE_ROWS_AV1, VE },
    { "tile-columns", "Number of tile columns to encode with", OFFSET(tile_cols), AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, MT_MAX_TILE_COLS_AV1, VE },
    { "init_qpP",     "Initial QP value for P frame",       OFFSET(init_qp_p),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 255, VE },
    { "init_qpB",     "Initial QP value for B frame",       OFFSET(init_qp_b),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 255, VE },
    { "init_qpI",     "Initial QP value for I frame",       OFFSET(init_qp_i),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 255, VE },
    { "forced-idr",   "If forcing keyframes, force them as IDR frames.",
                                                            OFFSET(forced_idr),   AV_OPT_TYPE_BOOL,  { .i64 = 0 }, -1, 1, VE },
    { "timing-info",  "Include timing info in sequence/frame headers",
                                                            OFFSET(timing_info),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "extra_sei",    "Pass on extra SEI data (e.g. a53 cc) to be included in the bitstream",
                                                            OFFSET(extra_sei),    AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1, VE },
    { "a53cc",        "Use A53 Closed Captions (if available)", OFFSET(a53_cc),   AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1, VE },
    { "s12m_tc",      "Use timecode (if available)",        OFFSET(s12m_tc),      AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1, VE },
    { NULL }
};

static const FFCodecDefault defaults[] = {
    { "b", "2M" },
    { "qmin", "-1" },
    { "qmax", "-1" },
    { "qdiff", "-1" },
    { "qblur", "-1" },
    { "qcomp", "-1" },
    { "g", "-1" },
    { "bf", "-1" },
    { "refs", "0" },
    { NULL },
};

static const AVClass av1_mtenc_class = {
    .class_name = "av1_mtenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_av1_mtenc_encoder = {
    .p.name         = "av1_mtenc",
    CODEC_LONG_NAME("MOORE THREADS MTENC av1 encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .init           = ff_mtenc_encode_init,
    FF_CODEC_RECEIVE_PACKET_CB(ff_mtenc_receive_packet),
    .close          = ff_mtenc_encode_close,
    .flush          = ff_mtenc_encode_flush,
    .priv_data_size = sizeof(MtencContext),
    .p.priv_class   = &av1_mtenc_class,
    .defaults       = defaults,
    .p.pix_fmts     = ff_mtenc_pix_fmts,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_ENCODER_FLUSH | AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name = "mtenc",
    .hw_configs     = ff_mtenc_hw_configs,
};
