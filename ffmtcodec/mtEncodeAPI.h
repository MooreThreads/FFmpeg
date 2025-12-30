/***************************************************************************************************
 * Copyright @2020-2024 Moore Threads Technology Co., Ltd ("Moore Threads"). All
 * rights reserved.
 *
 * The Software (including the source codes, the associated documents) is PROPRIETARY to
 * Moore Threads and is being provided as confidential information of Moore Threads. The disclosure
 * of the Software to any third party without the express written consent of Moore Threads is prohibited.
 *
 * Permission is hereby granted to the company accessing to this Software by authorized GitHub account
 * to use, copy the Software, and to incorporate this Software in the company's products as agreed by Moore Threads
 * and the company.
 *
 * NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE LICENSE
 * AGREEMENT, MOORE THREADS MAKES NO REPRESENTATION ABOUT ANY WARRANTIES,
 * INCLUDING BUT NOT LIMITED TO THE SUITABILITY OF THE SOFTWARE FOR ANY
 * PURPOSE. IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF
 * ANY KIND. MOORE THREADS DISCLAIMS ALL WARRANTIES WITH REGARD TO THE
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
 * NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE
 * LICENSE AGREEMENT, IN NO EVENT SHALL MOORE THREADS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THE SOFTWARE.
 ***************************************************************************************************/

#ifndef _MT_ENCODEAPI_H_
#define _MT_ENCODEAPI_H_

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define MTENCAPI                  __stdcall
#else
#define MTENCAPI
#endif

#define MTENCAPI_MAJOR_VERSION    1
#define MTENCAPI_MINOR_VERSION    7
#define MTENCAPI_PATCH_VERSION    1

#define MTENCAPI_VERSION          ((MTENCAPI_MAJOR_VERSION << 24) | (MTENCAPI_MINOR_VERSION << 16) | MTENCAPI_PATCH_VERSION) /**< The MTEncodeAPI version. \ingroup ENCODER_STRUCTURE*/

#define MTENC_INFINITE_GOPLENGTH  0xFFFFFFFF /**< An infinite GOP length. \ingroup ENCODER_STRUCTURE*/

typedef void* MT_ENC_INPUT_PTR;   /**< The MTEncodeAPI input buffer. \ingroup ENCODER_STRUCTURE*/
typedef void* MT_ENC_OUTPUT_PTR;  /**< The MTEncodeAPI output buffer. \ingroup ENCODER_STRUCTURE*/

/**
 * \mainpage MT Video Codec SDK
 *
 * \section mt_encode MTEncode
 * Moore Threads GPUs contain a hardware-based H.264/HEVC video encoder (hereafter referred to as MTEncode).
 * The MTEncode hardware takes YUV as input and generates an H.264/HEVC compliant video bitstream.
 * The MTEncodeAPI functions, structures, and other parameters are exposed in mtEncodeAPI.h.
 * Broadly, the encoding flow consists of the following steps:
 * 1. Initializes the encoder.
 * 2. Sets up the desired encoding parameters.
 * 3. Allocates input/output buffers.
 * 4. Copies frames to the input buffers and reads bitstreams from the output buffers.
 * 5. Cleans up - releases all allocated input/output buffers.
 * 6. Releases the encoder.
 *
 *
 * \code
 * // pseudocode
 * // open encoder
 * hModule = Load library(mtencodeapi64.dll/libencode_musa.so);
 * pfCreateInstance = Get Address of "MTEncodeAPICreateInstance";
 * MT_ENCODE_API_FUNCTION_LIST m_mtEnc = {};
 * pfCreateInstance(&m_mtEnc);
 * m_mtEnc.mtEncCreateEncoder(&createEncoderParams, &m_hEncoder)
 *
 * // get preset configuration
 * MT_ENC_PRESET_CONFIG presetConfig = {};
 * m_mtEnc.mtEncGetPresetConfig(m_hEncoder, ...);
 * // configuration parameters here...
 *
 * // init encoder
 * m_mtEnc.mtEncInitEncoder(m_hEncoder, ...)
 *
 * // create input and output buffers
 * m_mtEnc.mtEncCreateOutputBuffer(m_hEncoder, ...)
 *
 * while (1) {
 *     // fill data into an input buffer
 *
 *     // encode frame
 *     m_mtEnc.mtEncEncodeFrame(m_hEncoder, ...)
 *
 *     // get bitstream
 *     m_mtEnc.mtEncLockOutputBuffer(m_hEncoder, ...)
 *     m_mtEnc.mtEncUnlockOutputBuffer(m_hEncoder, ...)
 * }
 *
 * // release resource & destroy encoder
 * m_mtEnc.mtEncReleaseOutputBuffer(m_hEncoder, ...)
 * m_mtEnc.mtEncReleaseEncoder(m_hEncoder)
 * Free Library(hModule)
 *
 * \endcode
 *
 */

/**
 * \addtogroup ENCODER_STRUCTURE MTEncodeAPI data structures
 * @{
 */

/**
* Codec IDs supported by the MTEncodeAPI interface.
*/
typedef enum _MT_ENC_CODEC_ID
{
    MT_ENC_CODEC_ID_H264                  = 0x1, /**< The H.264 codec ID. */
    MT_ENC_CODEC_ID_HEVC                  = 0x2, /**< The HEVC codec ID. */
    MT_ENC_CODEC_ID_AV1                   = 0x3, /**< The AV1 codec ID. Need libva >= 2.16 on Linux */
} MT_ENC_CODEC_ID;

/**
* Profile IDs supported by the MTEncodeAPI interface.
*/
typedef enum _MT_ENC_CODEC_PROFILE_ID
{
    MT_ENC_CODEC_PROFILE_ID_AUTOSELECT    = 0x1,
    MT_ENC_CODEC_PROFILE_ID_BASELINE_H264 = 0x2,
    MT_ENC_CODEC_PROFILE_ID_MAIN_H264     = 0x3,
    MT_ENC_CODEC_PROFILE_ID_HIGH_H264     = 0x4,
    MT_ENC_CODEC_PROFILE_ID_HIGH10_H264   = 0x5,

    MT_ENC_CODEC_PROFILE_ID_MAIN_HEVC     = 0x10,
    MT_ENC_CODEC_PROFILE_ID_MAIN_10_HEVC  = 0x11,

    MT_ENC_CODEC_PROFILE_ID_MAIN_AV1      = 0x20,

} MT_ENC_CODEC_PROFILE_ID;

/**
* Preset IDs supported by the MTEncodeAPI interface.
*/
typedef enum _MT_ENC_PRESET_ID
{
    MT_ENC_PRESET_ID_DEFAULT              = 0x1, /**< The default preset ID. */
} MT_ENC_PRESET_ID;

/**
 * Rate control (RC) modes.
 */
typedef enum _MT_ENC_PARAMS_RC_MODE
{
    MT_ENC_PARAMS_RC_CONSTQP              = 0x0,  /**< The constant QP mode. */
    MT_ENC_PARAMS_RC_CBR                  = 0x1,  /**< The constant bit rate (CBR) mode. */
    MT_ENC_PARAMS_RC_VBR                  = 0x2,  /**< The variable bit rate (VBR) mode. */
} MT_ENC_PARAMS_RC_MODE;

/**
 * Input picture types.
 */
typedef enum _MT_ENC_PIC_TYPE
{
    MT_ENC_PIC_TYPE_I                     = 0x0,  /**< An intra-predicted picture.*/
    MT_ENC_PIC_TYPE_P                     = 0x1,  /**< A forward-predicted picture.*/
    MT_ENC_PIC_TYPE_B                     = 0x2,  /**< A bi-directionally predicted picture. */
    MT_ENC_PIC_TYPE_IDR                   = 0x3,  /**< An instantaneous decoding refresh (IDR) picture. */
    MT_ENC_PIC_TYPE_UNKNOWN               = 0xFF  /**< An unknown type. */
} MT_ENC_PIC_TYPE;

/**
 * Input buffer formats.
 */
typedef enum _MT_ENC_BUFFER_FORMAT
{
    MT_ENC_BUFFER_FORMAT_UNDEFINED        = 0x00000000,  /**< The undefined buffer format. */
    MT_ENC_BUFFER_FORMAT_NV12             = 0x00000001,  /**< Semi-planar YUV. The Y plane is followed by the interleaved UV plane. */
    MT_ENC_BUFFER_FORMAT_BGRA             = 0x10000000,  /**< 32-bit BGRA that uses 8 bits for storing each channel. */
    MT_ENC_BUFFER_FORMAT_RGBA             = 0x01000000,  /**< 32-bit RGBA that uses 8 bits for storing each channel. */
    MT_ENC_BUFFER_FORMAT_P010_LE          = 0x00010000,  /**< Like NV12, with 10 bits per pixel (bpp) per component, data in the high bits, zeros in the low bits, little-endian. */
} MT_ENC_BUFFER_FORMAT;

/**
 * Input buffer types. Used only on Linux.
 */
typedef enum _MT_ENC_BUFFER_TYPE
{
    MT_ENC_BUFFER_TYPE_SYSTEM_MEMORY    = 0x0,           /**< The buffer is system memory. */
    MT_ENC_BUFFER_TYPE_FD               = 0x1,           /**< The buffer is fd handle. */ 
} MT_ENC_BUFFER_TYPE;

/**
 * Encoding levels.
 */
typedef enum _MT_ENC_LEVEL
{
    MT_ENC_LEVEL_AUTOSELECT               = 0x0,

    MT_ENC_LEVEL_H264_1                   = 10,
    MT_ENC_LEVEL_H264_11                  = 11,
    MT_ENC_LEVEL_H264_12                  = 12,
    MT_ENC_LEVEL_H264_13                  = 13,
    MT_ENC_LEVEL_H264_2                   = 20,
    MT_ENC_LEVEL_H264_21                  = 21,
    MT_ENC_LEVEL_H264_22                  = 22,
    MT_ENC_LEVEL_H264_3                   = 30,
    MT_ENC_LEVEL_H264_31                  = 31,
    MT_ENC_LEVEL_H264_32                  = 32,
    MT_ENC_LEVEL_H264_4                   = 40,
    MT_ENC_LEVEL_H264_41                  = 41,
    MT_ENC_LEVEL_H264_42                  = 42,
    MT_ENC_LEVEL_H264_5                   = 50,
    MT_ENC_LEVEL_H264_51                  = 51,
    MT_ENC_LEVEL_H264_52                  = 52,

    MT_ENC_LEVEL_HEVC_1                   = 30,
    MT_ENC_LEVEL_HEVC_2                   = 60,
    MT_ENC_LEVEL_HEVC_21                  = 63,
    MT_ENC_LEVEL_HEVC_3                   = 90,
    MT_ENC_LEVEL_HEVC_31                  = 93,
    MT_ENC_LEVEL_HEVC_4                   = 120,
    MT_ENC_LEVEL_HEVC_41                  = 123,
    MT_ENC_LEVEL_HEVC_5                   = 150,
    MT_ENC_LEVEL_HEVC_51                  = 153,
    MT_ENC_LEVEL_HEVC_52                  = 156,
    MT_ENC_LEVEL_HEVC_6                   = 180,
    MT_ENC_LEVEL_HEVC_61                  = 183,
    MT_ENC_LEVEL_HEVC_62                  = 186,

    MT_ENC_TIER_HEVC_MAIN                 = 0,
    MT_ENC_TIER_HEVC_HIGH                 = 1,

    MT_ENC_LEVEL_AV1_2              = 0,
    MT_ENC_LEVEL_AV1_21             = 1,
    MT_ENC_LEVEL_AV1_22             = 2,
    MT_ENC_LEVEL_AV1_23             = 3,
    MT_ENC_LEVEL_AV1_3              = 4,
    MT_ENC_LEVEL_AV1_31             = 5,
    MT_ENC_LEVEL_AV1_32             = 6,
    MT_ENC_LEVEL_AV1_33             = 7,
    MT_ENC_LEVEL_AV1_4              = 8,
    MT_ENC_LEVEL_AV1_41             = 9,
    MT_ENC_LEVEL_AV1_42             = 10,
    MT_ENC_LEVEL_AV1_43             = 11,
    MT_ENC_LEVEL_AV1_5              = 12,
    MT_ENC_LEVEL_AV1_51             = 13,
    MT_ENC_LEVEL_AV1_52             = 14,
    MT_ENC_LEVEL_AV1_53             = 15,
    MT_ENC_LEVEL_AV1_6              = 16,
    MT_ENC_LEVEL_AV1_61             = 17,
    MT_ENC_LEVEL_AV1_62             = 18,
    MT_ENC_LEVEL_AV1_63             = 19,
    MT_ENC_LEVEL_AV1_7              = 20,
    MT_ENC_LEVEL_AV1_71             = 21,
    MT_ENC_LEVEL_AV1_72             = 22,
    MT_ENC_LEVEL_AV1_73             = 23,
    MT_ENC_LEVEL_AV1_AUTOSELECT         ,

    MT_ENC_TIER_AV1_0               = 0,
    MT_ENC_TIER_AV1_1               = 1

} MT_ENC_LEVEL;

/**
 * Enums related to fields in VUI parameters.
 */
typedef enum _MT_ENC_VUI_VIDEO_FORMAT
{
    MT_ENC_VUI_VIDEO_FORMAT_COMPONENT   = 0,
    MT_ENC_VUI_VIDEO_FORMAT_PAL         = 1,
    MT_ENC_VUI_VIDEO_FORMAT_NTSC        = 2,
    MT_ENC_VUI_VIDEO_FORMAT_SECAM       = 3,
    MT_ENC_VUI_VIDEO_FORMAT_MAC         = 4,
    MT_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED = 5,
} MT_ENC_VUI_VIDEO_FORMAT;

typedef enum _MT_ENC_VUI_COLOR_PRIMARIES
{
    MT_ENC_VUI_COLOR_PRIMARIES_UNDEFINED   = 0,
    MT_ENC_VUI_COLOR_PRIMARIES_BT709       = 1,
    MT_ENC_VUI_COLOR_PRIMARIES_UNSPECIFIED = 2,
    MT_ENC_VUI_COLOR_PRIMARIES_RESERVED    = 3,
    MT_ENC_VUI_COLOR_PRIMARIES_BT470M      = 4,
    MT_ENC_VUI_COLOR_PRIMARIES_BT470BG     = 5,
    MT_ENC_VUI_COLOR_PRIMARIES_SMPTE170M   = 6,
    MT_ENC_VUI_COLOR_PRIMARIES_SMPTE240M   = 7,
    MT_ENC_VUI_COLOR_PRIMARIES_FILM        = 8,
    MT_ENC_VUI_COLOR_PRIMARIES_BT2020      = 9,
    MT_ENC_VUI_COLOR_PRIMARIES_SMPTE428    = 10,
    MT_ENC_VUI_COLOR_PRIMARIES_SMPTE431    = 11,
    MT_ENC_VUI_COLOR_PRIMARIES_SMPTE432    = 12,
    MT_ENC_VUI_COLOR_PRIMARIES_JEDEC_P22   = 22,
} MT_ENC_VUI_COLOR_PRIMARIES;

typedef enum _MT_ENC_VUI_TRANSFER_CHARACTERISTIC
{
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_UNDEFINED    = 0,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709        = 1,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_UNSPECIFIED  = 2,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_RESERVED     = 3,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_BT470M       = 4,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_BT470BG      = 5,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE170M    = 6,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE240M    = 7,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_LINEAR       = 8,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_LOG          = 9,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_LOG_SQRT     = 10,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_IEC61966_2_4 = 11,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_BT1361_ECG   = 12,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_SRGB         = 13,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_BT2020_10    = 14,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_BT2020_12    = 15,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE2084    = 16,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE428     = 17,
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC_ARIB_STD_B67 = 18,
} MT_ENC_VUI_TRANSFER_CHARACTERISTIC;

typedef enum _MT_ENC_VUI_MATRIX_COEFFS
{
    MT_ENC_VUI_MATRIX_COEFFS_RGB         = 0,
    MT_ENC_VUI_MATRIX_COEFFS_BT709       = 1,
    MT_ENC_VUI_MATRIX_COEFFS_UNSPECIFIED = 2,
    MT_ENC_VUI_MATRIX_COEFFS_RESERVED    = 3,
    MT_ENC_VUI_MATRIX_COEFFS_FCC         = 4,
    MT_ENC_VUI_MATRIX_COEFFS_BT470BG     = 5,
    MT_ENC_VUI_MATRIX_COEFFS_SMPTE170M   = 6,
    MT_ENC_VUI_MATRIX_COEFFS_SMPTE240M   = 7,
    MT_ENC_VUI_MATRIX_COEFFS_YCGCO       = 8,
    MT_ENC_VUI_MATRIX_COEFFS_BT2020_NCL  = 9,
    MT_ENC_VUI_MATRIX_COEFFS_BT2020_CL   = 10,
    MT_ENC_VUI_MATRIX_COEFFS_SMPTE2085   = 11,
} MT_ENC_VUI_MATRIX_COEFFS;

/**
 * Error codes.
 */
typedef enum _MTENCSTATUS
{
    /**
     * This indicates that the API call returned with no errors.
     */
    MT_ENC_SUCCESS,

    /**
     * Devices capable of encoding are not detected or invalid.
     */
    MT_ENC_ERR_NO_ENCODE_DEVICE,

    /**
     * The devices are not supported.
     */
    MT_ENC_ERR_UNSUPPORTED_DEVICE,

    /**
     * At least one parameter value that is passed to the API is invalid.
     */
    MT_ENC_ERR_INVALID_PARAM,

    /**
     * Out of memory.
     */
    MT_ENC_ERR_OUT_OF_MEMORY,

    /**
     * The encoder has not been initialized with
     * ::MTEncInitEncoder() or initialization failed.
     * The client cannot allocate input or output buffers or
     * do any encoding-related operation until it successfully initializes the encoder.
     */
    MT_ENC_ERR_ENCODER_NOT_INITIALIZED,

    /**
     * The client uses an invalid version.
     */
    MT_ENC_ERR_INVALID_VERSION,

    /**
     * ::MTEncMapResource() failed to map the input resource that the client provides.
     */
    MT_ENC_ERR_MAP_FAILED,

    /**
     * The client is attempting to unmap a resource
     * that has not been successfully mapped.
     */
    MT_ENC_ERR_RESOURCE_NOT_MAPPED,

    /**
     * The encoder requires more input buffers to produce an output
     * buffer. If this error is returned from ::MTEncEncodeFrame(), this
     * is not a fatal error. If the client is encoding using B-frames,
     * ::MTEncEncodeFrame() might be buffering the input frames for re-ordering.
     * A client that operates in the synchronous mode cannot call ::MTEncLockOutputBuffer()
     * on the output bitstream buffers if ::MTEncEncodeFrame() returns the
     * ::MT_ENC_ERR_NEED_MORE_INPUT error code.
     * The client must keep providing input frames until the encoder returns
     * ::MT_ENC_SUCCESS. After receiving ::MT_ENC_SUCCESS, the client can call
     * ::MTEncLockOutputBuffer() on the output buffers in the same order in which
     * it has called ::MTEncEncodeFrame().
     */
    MT_ENC_ERR_NEED_MORE_INPUT,

    /**
     * The hardware encoder is busy encoding and cannot encode
     * the input. The client should call ::MTEncEncodeFrame() again after a few
     * milliseconds.
     */
    MT_ENC_ERR_ENCODER_BUSY,

    /**
     * An unknown internal error has occurred.
     */
    MT_ENC_ERR_GENERIC,

} MTENCSTATUS;

/**
 * Picture encoding flags.
 */
typedef enum _MT_ENC_PIC_FLAGS
{
    MT_ENC_PIC_FLAG_FORCEIDR              = 0x2,  /**< Encodes the current picture as an IDR picture.
                                                     This flag is only valid when the picture type decision is taken by the encoder. */
    MT_ENC_PIC_FLAG_EOS                   = 0x8,  /**< Indicates the end of the input stream. */
} MT_ENC_PIC_FLAGS;

/**
 *  Input resource types.
 */
typedef enum _MT_ENC_INPUT_RESOURCE_TYPE
{
    MT_ENC_INPUT_RESOURCE_TYPE_DIRECTX          = 0x0,  /**< The input resource type is a DirectX surface. */
    MT_ENC_INPUT_RESOURCE_TYPE_OPENGL_TEX       = 0x1,  /**< The input resource type is a OpenGL texture. */
    MT_ENC_INPUT_RESOURCE_TYPE_VULKAN_IMAGE_TILING_OPTIMAL     = 0x2,  /**< The input resource type is a Vulkan image with tiling set as optimal. */
    MT_ENC_INPUT_RESOURCE_TYPE_VULKAN_IMAGE_TILING_LINEAR      = 0x3, /**< The input resource type is a Vulkan image with tiling set as linear. */
    MT_ENC_INPUT_RESOURCE_TYPE_VULKAN_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT = 0x4, /**< The input resource type is a Vulkan image with tiling set as DRM format modifier. */
    MT_ENC_INPUT_RESOURCE_TYPE_MUSADEVICEPTR    = 0x5,  /**< The input resource type is musa device memory. */
} MT_ENC_INPUT_RESOURCE_TYPE;

/**
 *  Encoder device types.
 */
typedef enum _MT_ENC_DEVICE_TYPE
{
    MT_ENC_DEVICE_TYPE_DIRECTX                  = 0x0,  /**< The encoder device type is a DirectX device.
                                                         This device type can only be used with Windows. */
    MT_ENC_DEVICE_TYPE_OPENGL                   = 0x1,  /**< The encoder device type is an OpenGL device.
                                                         This device type can only be used with Linux. */
    MT_ENC_DEVICE_TYPE_VULKAN                   = 0x2,  /**< The encoder device type is an Vulkan device. */
    MT_ENC_DEVICE_TYPE_MUSA                     = 0x3, /**< The encoder device type is an musa device. */
} MT_ENC_DEVICE_TYPE;

/**
 * Encoder capabilities enumeration.
 */
typedef enum _MT_ENC_CAPS
{
    /**
     * Maximum output width supported.
     */
    MT_ENC_CAPS_WIDTH_MAX,

    /**
     * Maximum output height supported.
     */
    MT_ENC_CAPS_HEIGHT_MAX,

    /**
     * Minimum input width supported.
     */
    MT_ENC_CAPS_WIDTH_MIN,

    /**
     * Minimum input height supported.
     */
    MT_ENC_CAPS_HEIGHT_MIN,

    /**
     * qp delta map block size supported.
     */
    MT_ENC_CAPS_BLOCK_SIZE,
} MT_ENC_CAPS;

/**
 * Creation parameters for output bitstream buffers.
 */
typedef struct _MT_ENC_CREATE_OUTPUT_BUFFER
{
    uint32_t              version;                      /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    uint32_t              reserved;                     /**< [in]: Reserved and must be set to 0. */
    MT_ENC_OUTPUT_PTR     outputBuffer;                 /**< [out]: A pointer to the output buffer. */
    uint32_t              reserved1[62];                /**< [in]: Reserved and must be set to 0. */
    void*                 reserved2[63];                /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_CREATE_OUTPUT_BUFFER;

/**
 * QP values for frames.
 */
typedef struct _MT_ENC_QP
{
    uint32_t        qpInterP; /**< [in]: The quantization parameter (QP) value for P-frame encoding. */
    uint32_t        qpInterB; /**< [in]: The QP value for B-frame encoding. */
    uint32_t        qpIntra;  /**< [in]: The QP value for I-frame encoding. */
} MT_ENC_QP;

/**
* Rate control configuration parameters.
 */
typedef struct _MT_ENC_RC_PARAMS
{
    uint32_t                        version;
    MT_ENC_PARAMS_RC_MODE           rateControlMode;                             /**< [in]: Specifies the rate control mode. Check support for various rate control modes using ::MT_ENC_PARAMS_RC_MODE caps. */
    MT_ENC_QP                       constQP;                                     /**< [in]: Specifies the initial QP to be used for encoding. The values are used for all frames if in the constant QP mode. MT_ENC_QP::qpIntra is used for all frames so far.*/
    uint32_t                        averageBitRate;                              /**< [in]: Specifies the average bit rate in bits per second (bps) used for encoding. */
    uint32_t                        maxBitRate;                                  /**< [in]: Specifies the maximum bitrate for the encoded output. This is used for VBR and ignored for CBR. */
    uint32_t                        enableMinQP              :1;                 /**< [in]: Set this to 1 if minimum QP used for rate control. */
    uint32_t                        enableMaxQP              :1;                 /**< [in]: Set this to 1 if maximum QP used for rate control. */
    uint32_t                        enableInitialRCQP        :1;                 /**< [in]: Set this to 1 if user supplied initial QP is used for rate control. */
    uint32_t                        reservedBitFields        :29;                /**< [in]: Reserved bitfields and must be set to 0 */
    MT_ENC_QP                       minQP;                                       /**< [in]: Specifies the minimum QP used for rate control. Client must set enableMinQP to 1. MT_ENC_QP::qpIntra is used for all frames so far.*/
    MT_ENC_QP                       maxQP;                                       /**< [in]: Specifies the maximum QP used for rate control. Client must set enableMaxQP to 1. MT_ENC_QP::qpIntra is used for all frames so far.*/
    MT_ENC_QP                       initialRCQP;                                 /**< [in]: Specifies the initial QP hint used for rate control. The parameter is just used as hint to influence the QP difference between I,P and B frames.
                                                                                            Client must set enableInitialRCQP to 1. MT_ENC_QP::qpIntra is used for all frames so far.*/
    uint32_t                        vbvBufferSize;                               /* < [in]: Specifies vbv buffer size.*/
    uint32_t                        vbvBufferInitialOccupancy;                   /* < [in]: Number of bits which should be loaded into the rc buffer before decoding starts.*/
    uint32_t                        reserved1[45];                               /**< [in]: Reserved and must be set to 0. */
    void*                           reserved2[64];                               /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_RC_PARAMS;

/**
 *  User SEI message
 */
typedef struct _MT_ENC_SEI_PAYLOAD
{
   uint32_t                        payloadSize;                                 /**< [in] SEI payload size in bytes. SEI payload must be byte aligned, as described in Annex D */
   uint32_t                        payloadType;                                 /**< [in] SEI payload types and syntax can be found in Annex D of the H.264 Specification. */
   uint8_t*                        payload;                                     /**< [in] pointer to user data */
} MT_ENC_SEI_PAYLOAD;

#define MAX_NUM_CLOCK_TS    3

/**
* Clock Timestamp set parameters
* For H264, this structure is used to populate Picture Timing SEI when MT_ENC_CONFIG_H264::enableTimeCode is set to 1.
* For HEVC, this structure is used to populate Time Code SEI when MT_ENC_CONFIG_HEVC::enableTimeCodeSEI is set to 1.
* For more details, refer to Annex D of ITU-T Specification.
*/

typedef struct _MT_ENC_CLOCK_TIMESTAMP_SET
{
    uint32_t        countingType            : 1;    /**< [in] Specifies the 'counting_type' */
    uint32_t        discontinuityFlag       : 1;    /**< [in] Specifies the 'discontinuity_flag' */
    uint32_t        cntDroppedFrames        : 1;    /**< [in] Specifies the 'cnt_dropped_flag' */
    uint32_t        nFrames                 : 8;    /**< [in] Specifies the value of 'n_frames' */
    uint32_t        secondsValue            : 6;    /**< [in] Specifies the 'seconds_value' */
    uint32_t        minutesValue            : 6;    /**< [in] Specifies the 'minutes_value' */
    uint32_t        hoursValue              : 5;    /**< [in] Specifies the 'hours_value' */
    uint32_t        reserved2               : 4;    /**< [in] Reserved and must be set to 0 */
    uint32_t        timeOffset;                     /**< [in] Specifies the 'time_offset_value' */
} MT_ENC_CLOCK_TIMESTAMP_SET;

/**
 * Display picture structure
 * Currently, this enum is only used for deciding the number of clock timestamp sets in Picture Timing SEI / Time Code SEI
 * Otherwise, this has no impact on encoder behavior
 */
typedef enum _MT_ENC_DISPLAY_PIC_STRUCT
{
    MT_ENC_PIC_STRUCT_DISPLAY_FRAME             = 0x00,                 /**< Field encoding top field first */
    MT_ENC_PIC_STRUCT_DISPLAY_FIELD_TOP_BOTTOM  = 0x01,                 /**< Field encoding top field first */
    MT_ENC_PIC_STRUCT_DISPLAY_FIELD_BOTTOM_TOP  = 0x02,                 /**< Field encoding bottom field first */
    MT_ENC_PIC_STRUCT_DISPLAY_FRAME_DOUBLING    = 0x07,                 /**< Frame doubling */
    MT_ENC_PIC_STRUCT_DISPLAY_FRAME_TRIPLING    = 0x08                  /**< Field tripling */
} MT_ENC_DISPLAY_PIC_STRUCT;

typedef struct _MT_ENC_TIME_CODE
{
    MT_ENC_DISPLAY_PIC_STRUCT       displayPicStruct;                   /**< [in] Display picStruct */
    MT_ENC_CLOCK_TIMESTAMP_SET      clockTimestamp[MAX_NUM_CLOCK_TS];   /**< [in] Clock Timestamp set */
} MT_ENC_TIME_CODE;

/**
 * H264 Video Usability Info parameters
 */
typedef struct _MT_ENC_CONFIG_H264_VUI_PARAMETERS
{
    uint32_t                                overscanInfoPresentFlag;            /**< [in]: If set to 1, it specifies that the overscan info is present. */
    uint32_t                                overscanInfo;                       /**< [in]: Specifies the overscan info (as defined in Annex E of the ITU-T Specification). */
    uint32_t                                videoSignalTypePresentFlag;         /**< [in]: If set to 1, it specifies that the videoFormat, videoFullRangeFlag and colourDescriptionPresentFlag are present. */
    MT_ENC_VUI_VIDEO_FORMAT                 videoFormat;                        /**< [in]: Specifies the source video format (as defined in Annex E of the ITU-T Specification).*/
    uint32_t                                videoFullRangeFlag;                 /**< [in]: Specifies the output range of the luma and chroma samples (as defined in Annex E of the ITU-T Specification). */
    uint32_t                                colourDescriptionPresentFlag;       /**< [in]: If set to 1, it specifies that the colourPrimaries, transferCharacteristics and colourMatrix are present. */
    MT_ENC_VUI_COLOR_PRIMARIES              colourPrimaries;                    /**< [in]: Specifies color primaries for converting to RGB (as defined in Annex E of the ITU-T Specification). */
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC      transferCharacteristics;            /**< [in]: Specifies the opto-electronic transfer characteristics to use (as defined in Annex E of the ITU-T Specification). */
    MT_ENC_VUI_MATRIX_COEFFS                colourMatrix;                       /**< [in]: Specifies the matrix coefficients used in deriving the luma and chroma from the RGB primaries (as defined in Annex E of the ITU-T Specification). */
    uint32_t                                timingInfoPresentFlag;              /**< [in]: If set to 1, it specifies that the timingInfo is present and the 'numUnitInTicks' and 'timeScale' fields are specified by the application.
                                                                                           If not set, the timingInfo may still be present with timing related fields calculated internally based on the frame rate specified by the application. */
    uint32_t                                numUnitInTicks;                     /**< [in]: Specifies the number of time units of the clock (as defined in Annex E of the ITU-T Specification). */
    uint32_t                                timeScale;                          /**< [in]: Specifies the frequency of the clock (as defined in Annex E of the ITU-T Specification). */
    uint32_t                                reserved[16];                       /**< [in]: Reserved and must be set to 0. */
} MT_ENC_CONFIG_H264_VUI_PARAMETERS;

typedef MT_ENC_CONFIG_H264_VUI_PARAMETERS MT_ENC_CONFIG_HEVC_VUI_PARAMETERS;

/**
  * H.264 encoder configuration parameters.
 */
typedef struct _MT_ENC_CONFIG_H264
{
    uint32_t                                    level;                          /**< [in]: Specifies the encoding level. The client is recommended to set this to \c MT_ENC_LEVEL_AUTOSELECT to enable the MTEncodeAPI interface to select the correct level. */
    uint32_t                                    idrPeriod;                      /**< [in]: Specifies the IDR interval. If not set, this is made equal to MT_ENC_CONFIG::gopLength. A low-latency application client can set the IDR interval to #MTENC_INFINITE_GOPLENGTH so that IDR frames are not inserted automatically. */
    MT_ENC_CONFIG_H264_VUI_PARAMETERS           h264VUIParameters;              /**< [in]: Specifies the H.264 video usability info parameters. */
    uint32_t                                    nbTemporalLayers;               /**< [in]: Specifies the number of temproal layers (1~4).Default is 1 */
    uint32_t                                    disableSVCPrefixNalu;           /**< [in]: Set to 1 to disable writing of SVC Prefix NALU preceding each slice in bitstream.Default is 0 */
    uint32_t                                    sliceMode;                      /**< [in]: This parameter in conjunction with sliceModeData specifies the way in which the picture is divided into slices
                                                                                           sliceMode = 0 MB row based slices, sliceMode = 1, numSlices in Picture
                                                                                           When forceIntraRefreshWithFrameCnt is set it will have priority over sliceMode setting
                                                                                           When sliceMode == 0 and sliceModeData == 0 whole picture will be coded with one slice */
    uint32_t                                    sliceModeData;                  /**< [in]: Specifies the parameter needed for sliceMode. For:
                                                                                           sliceMode = 0, sliceModeData specifies # of MB rows in each slice (except last slice)
                                                                                           sliceMode = 1, sliceModeData specifies number of slices in the picture. Driver will divide picture into slices optimally */
    uint32_t                                    outputBufferingPeriodSEI  :1;   /**< [in]: Set to 1 to write SEI buffering period syntax in the bitstream */
    uint32_t                                    outputPictureTimingSEI    :1;   /**< [in]: Set to 1 to write SEI picture timing syntax in the bitstream. */
    uint32_t                                    enableTimeCode            :1;   /**< [in]: Set to 1 to enable writing of clock timestamp sets in picture timing SEI. */
    uint32_t                                    enableScalabilityInfoSEI  :1;   /**< [in]: Set to 1 to enable writing of Scalability Information SEI message preceding each IDR picture in bitstream
                                                                                    Applicable only when temporal SVC is enabled (MT_ENC_CONFIG_H264::nbTemporalLayers > 1). */
    uint32_t                                    pixelBitDepthMinus8       :3;   /**< [in]: Specifies the pixel bit depth minus 8. Should be set to 0 for 8-bit input, 2 for 10-bit input.*/
    uint32_t                                    reservedBitFields         :25;  /**< [in]: Reserved bitfields and must be set to 0 */
    
    uint32_t                                    reserved1[221];                 /**< [in]: Reserved and must be set to 0. */
    void*                                       reserved2[64];                  /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_CONFIG_H264;

/**
  * HEVC encoder configuration parameters.
 */
typedef struct _MT_ENC_CONFIG_HEVC
{
    uint32_t                                level;                                  /**< [in]: Specifies the encoding level. The client is recommended to set this to \c MT_ENC_LEVEL_AUTOSELECT to enable the MTEncodeAPI interface to select the correct level. */
    uint32_t                                tier;                                   /**< [in]: Specifies the level tier of the encoded bitstream. */
    uint32_t                                idrPeriod;                              /**< [in]: Specifies the IDR interval. If not set, this is made equal to MT_ENC_CONFIG::gopLength. A low-latency application client can set the IDR interval to #MTENC_INFINITE_GOPLENGTH so that IDR frames are not inserted automatically. */
    uint32_t                                reservedP1                     : 11;    /**< [in]: Reserved and must be set to 0. */
    uint32_t                                pixelBitDepthMinus8            : 3;     /**< [in]: Specifies the pixel bit depth minus 8. Should be set to 0 for 8-bit input, 2 for 10-bit input.*/
    uint32_t                                reservedP2                     : 18;    /**< [in]: Reserved and must be set to 0. */
    MT_ENC_CONFIG_HEVC_VUI_PARAMETERS       hevcVUIParameters;                      /**< [in]: Specifies the HEVC video usability info parameters. */
    uint32_t                                sliceMode;                              /**< [in]: This parameter in conjunction with sliceModeData specifies the way in which the picture is divided into slices
                                                                                               sliceMode = 0 CTU row based slices, sliceMode = 1, numSlices in Picture
                                                                                               When forceIntraRefreshWithFrameCnt is set it will have priority over sliceMode setting
                                                                                               When sliceMode == 0 and sliceModeData == 0 whole picture will be coded with one slice */
    uint32_t                                sliceModeData;                          /**< [in]: Specifies the parameter needed for sliceMode. For:
                                                                                               sliceMode = 0, sliceModeData specifies # of CTU rows in each slice (except last slice)
                                                                                               sliceMode = 1, sliceModeData specifies number of slices in the picture. Driver will divide picture into slices optimally */
    uint32_t                                outputBufferingPeriodSEI  :1;           /**< [in]: Set to 1 to write SEI buffering period syntax in the bitstream */
    uint32_t                                outputPictureTimingSEI    :1;           /**< [in]: Set to 1 to write SEI picture timing syntax in the bitstream. */
    uint32_t                                outputTimeCodeSEI         :1;           /**< [in]: Set 1 to write SEI time code syntax in the bitstream. */
    uint32_t                                reservedBitFields         :29;          /**< [in]: Reserved bitfields and must be set to 0 */
    uint32_t                                reserved1[221];                         /**< [in]: Reserved and must be set to 0. */
    void*                                   reserved2[64];                          /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_CONFIG_HEVC;

/**
* \struct _MT_ENC_CONFIG_AV1
* AV1 encoder configuration parameters to be set during initialization.
*/
typedef struct _MT_ENC_CONFIG_AV1
{
    uint32_t level;                                                 /**< [in]: Specifies the level of the encoded bitstream.*/
    uint32_t tier;                                                  /**< [in]: Specifies the level tier of the encoded bitstream.*/
    uint32_t idrPeriod;                                             /**< [in]: Specifies the IDR/Key frame interval. If not set, this is made equal to MT_ENC_CONFIG::gopLength. A low-latency application client can set the IDR interval to #MTENC_INFINITE_GOPLENGTH so that IDR frames are not inserted automatically. */
    uint32_t inputPixelBitDepthMinus8       : 3;                    /**< [in]: Specifies pixel bit depth minus 8 of video input. Should be set to 0 for 8 bit input, 2 for 10 bit input.*/
    uint32_t enableTimingInfo               : 1;                    /**< [in]: Set 1 to write Timing Info into sequence/frame headers */
    uint32_t reserved                       : 28;                   /**< [in]: Reserved bitfields.*/
    uint32_t numTileColumns;                                        /**< [in]: Only support uniformed tile mode now. The picture will be uniformly divided into numTileColumns tile columns. If numTileColumns is not a power of 2,
                                                                               it will be rounded down to the next power of 2 value. If numTileColumns == 0, the picture will be coded with the smallest number of vertical tiles as allowed by standard.*/
    uint32_t numTileRows;                                           /**< [in]: Only support uniformed tile mode now. The picture will be uniformly divided into numTileRows tile rows. If numTileRows is not a power of 2,
                                                                               it will be rounded down to the next power of 2 value. If numTileRows == 0, the picture will be coded with the smallest number of horizontal tiles as allowed by standard.*/
    MT_ENC_VUI_COLOR_PRIMARIES colorPrimaries;                      /**< [in]: as defined in section of ISO/IEC 23091-4/ITU-T H.273 */
    MT_ENC_VUI_TRANSFER_CHARACTERISTIC transferCharacteristics;     /**< [in]: as defined in section of ISO/IEC 23091-4/ITU-T H.273 */
    MT_ENC_VUI_MATRIX_COEFFS matrixCoefficients;                    /**< [in]: as defined in section of ISO/IEC 23091-4/ITU-T H.273 */
    uint32_t colorRange;                                            /**< [in]: 0: studio swing representation - 1: full swing representation */
    uint32_t chromaSamplePosition;                                  /**< [in]: 0: unknown
                                                                               1: Horizontally collocated with luma (0,0) sample, between two vertical samples
                                                                               2: Co-located with luma (0,0) sample */
    uint32_t reserved1[245];                                        /**< [in]: Reserved and must be set to 0.*/
    void*    reserved2[64];                                         /**< [in]: Reserved and must be set to NULL */
} MT_ENC_CONFIG_AV1;

/**
 * Codec-specific encoder configuration parameters to be set during initialization.
 */
typedef struct _MT_ENC_CODEC_CONFIG
{
    MT_ENC_CONFIG_H264        h264Config;                /**< [in]: Specifies the H.264-specific encoder configuration. */
    MT_ENC_CONFIG_HEVC        hevcConfig;                /**< [in]: Specifies the HEVC-specific encoder configuration. */
    MT_ENC_CONFIG_AV1         av1Config;                 /**< [in]: Specifies the AV1-specific encoder configuration. */
    uint32_t                  reserved1[1024];           /**< [in]: Reserved and must be set to 0. */
    void*                     reserved2[256];            /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_CODEC_CONFIG;

typedef enum _MT_INTRA_REFRESH_MODE
{
    MT_INTRA_REFRESH_MODE_NONE,
    MT_INTRA_REFRESH_MODE_ROW,
    MT_INTRA_REFRESH_MODE_COLUMN
} MT_INTRA_REFRESH_MODE;

/**
 * Encoder configuration parameters to be set during initialization.
 */
typedef struct _MT_ENC_CONFIG
{
    uint32_t                        version;                                     /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    MT_ENC_CODEC_PROFILE_ID         profileID;                                   /**< [in]: Specifies the codec profile ID. If the client specifies \c MT_ENC_CODEC_PROFILE_ID_AUTOSELECT, the MTEncodeAPI interface will select the appropriate codec profile. */
    uint32_t                        gopLength;                                   /**< [in]: Specifies the number of pictures in one GOP. A low-latency application client can set \p gopLength to #MTENC_INFINITE_GOPLENGTH so that keyframes are not inserted automatically. */
    int32_t                         frameIntervalP;                              /**< [in]: Specifies the GOP pattern as follows: \p frameIntervalP == 0: I, 1: IPP, 2: IBP, 3: IBBP. If \p gopLength is set to #MTENC_INFINITE_GOPLENGTH, \p frameIntervalP should be set to 1. */
    MT_ENC_RC_PARAMS                rcParams;                                    /**< [in]: Specifies the rate control parameters for the current encoder. */
    MT_ENC_CODEC_CONFIG             encodeCodecConfig;                           /**< [in]: Specifies the codec-specific configuration parameters by using this union. */
    uint32_t                        bDepth;                                      /**< [in]: Specifies the maximum depth of hierarchical B-frames, which ranges from 1 to 3. The default value is 1. When \p bDepth is greater than 1, the number of successive B-frames should be 3 to 7. */
    uint32_t                        refs;                                        /**< [in]: Specifies the reference number of the P-frame, which ranges from 1 to 2. The default value is 1. */
    MT_INTRA_REFRESH_MODE           intraRefreshMode;                            /**< [in]: Specifies gradual intra refresh mode.0:No intra refresh mode 1:Row refresh 2:Column refresh. */
    uint32_t                        intraRefreshArg;                             /**< [in]: Specifies the number (1~511) of consecutive intra CTU/MB rows/columns. Valid when \p intraRefreshMode is 1 or 2. */
    uint32_t                        repeatHeaders;                               /**< [in]: Set to 1 to enable writing SPS and PPS (and VPS for HEVC) headers with every keyframe. */
    uint32_t                        reserved1[247];                              /**< [in]: Reserved and must be set to 0. */
    void*                           reserved2[64];                               /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_CONFIG;

/**
 * Encoder initialization parameters.
 */
typedef struct _MT_ENC_INIT_PARAMS
{
    uint32_t                                   version;                         /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    MT_ENC_CODEC_ID                            encodeID;                        /**< [in]: Specifies the encode ID for which the encoder is being created. ::MTEncInitEncoder() fails if this is not set or set to an unsupported value. */
    MT_ENC_PRESET_ID                           presetID;                        /**< [in]: Specifies the preset ID for encoding. If the preset ID is set, the preset configuration is applied before any other parameters. */
    uint32_t                                   encodeWidth;                     /**< [in]: Specifies the encode width. If not set, ::MTEncInitEncoder() fails. */
    uint32_t                                   encodeHeight;                    /**< [in]: Specifies the encode height. If not set, ::MTEncInitEncoder() fails. */
    uint32_t                                   frameRateNum;                    /**< [in]: Specifies the numerator for the frame rate used for encoding in frames per second (FPS). Frame rate = \p frameRateNum / \p frameRateDen. */
    uint32_t                                   frameRateDen;                    /**< [in]: Specifies the denominator for the frame rate used for encoding in frames per second. Frame rate = \p frameRateNum / \p frameRateDen. */
    uint32_t                                   enableEncodeAsync;               /**< [in]: Set this to 1 to enable asynchronous mode and is expected to use events to get picture completion notification. */
    MT_ENC_CONFIG*                             encodeConfig;                    /**< [in]: Specifies the advanced codec-specific structure.\n
                                                                                           If the client sends a valid codec configuration structure, it overrides the parameters set by MT_ENC_INIT_PARAMS::presetID. If set to NULL, the MTEncodeAPI interface uses the MT_ENC_INIT_PARAMS::presetID to set the codec-specific parameters.\n
                                                                                           The client can also optionally query the MTEncodeAPI interface to get codec-specific parameters for a preset ID using ::MTEncGetPresetConfig(). It can then modify (if required) some of the codec configuration parameters and send down a custom configuration structure as part of ::_MT_ENC_INIT_PARAMS.\n
                                                                                           Even in this case the client is recommended to pass the same preset ID it has used in ::MTEncGetPresetConfig() to query the configuration structure as MT_ENC_INIT_PARAMS::presetID. This does not override the custom configuration structure but is used to determine other encoder HW-specific parameters not exposed in the API. */
    uint32_t                                   maxEncodeWidth;                  /**< [in]: The maximum encode width to be allowed for the current encoder.
                                                                                           The client should allocate output buffers according to this dimension for dynamic resolution changes. If set to 0, the encoder does not allow dynamic resolution changes. */
    uint32_t                                   maxEncodeHeight;                 /**< [in]: The maximum encode height to be allowed for current encoder.
                                                                                           The client should allocate output buffers according to this dimension for dynamic resolution changes. If set to 0, the encoder does not allow dynamic resolution changes. */
    const char*                                deviceName;                      /**< [in]: The device name to encode (for example: /dev/dri/renderD128. SDK uses /dev/dri/renderD128 if set to NULL). Used only on Linux. */
    uint32_t                                   reserved1[246];                  /**< [in]: Reserved and must be set to 0. */
    void*                                      reserved2[62];                   /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_INIT_PARAMS;

/**
 * Encode session reconfiguration parameters.
 */
typedef struct _MT_ENC_RECONFIGURE_PARAMS
{
    uint32_t                                    version;                        /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    uint32_t                                    reserved;                       /**< [in]: Reserved and must be set to 0. */
    MT_ENC_INIT_PARAMS                          reInitParams;                   /**< [in]: Encoder session re-initialization parameters.*/
} MT_ENC_RECONFIGURE_PARAMS;

/**
  * The encoder preset configuration structure.
 */
typedef struct _MT_ENC_PRESET_CONFIG
{
    uint32_t      version;                               /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    uint32_t      reserved;                              /**< [in]: Reserved and must be set to 0. */
    MT_ENC_CONFIG presetCfg;                             /**< [out]: A preset configuration structure returned by the MTEncodeAPI interface. */
    uint32_t      reserved1[254];                        /**< [in]: Reserved and must be set to 0. */
    void*         reserved2[64];                         /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_PRESET_CONFIG;

/**
 *  A rectangle.
 */
typedef struct _MT_RECTANGLE
{
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} MT_RECTANGLE;

/**
 * Encoding region-of-interest (ROI). \n
 *
 * The ROI set through this structure is applicable only to the
 * current frame or field, so it must be sent for every frame or field to be applied.
 * The encoder will use the ROI information to adjust the QP values of the MBs that fall within the ROIs.
 */
typedef struct _MT_ENCODER_ROI
{
    /**
     * \brief Defines the ROI boundary in pixels.
     *
     *  The driver will map it to appropriate
     *  codec coding units. It is relative to frame coordinates for the frame case and
     *  to field coordinates for the field case.
     */
    MT_RECTANGLE roi_rectangle;
    /**
     * \brief The ROI value.
     * \ref roi_value always specifies the ROI QP when _MT_ENC_RC_PARAMS::rateControlMode != ::MT_ENC_PARAMS_RC_CONSTQP.
     */
    int8_t roi_value;
    int8_t reserved;
} MT_ENCODER_ROI;

/**

 * Picture encoding parameters, sent on a frame-by-frame basis.
 */
typedef struct _MT_ENC_PIC_PARAMS
{
    uint32_t                                    version;                        /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    uint32_t                                    inputWidth;                     /**< [in]: Specifies the input buffer width. */
    uint32_t                                    inputHeight;                    /**< [in]: Specifies the input buffer height. */
    uint32_t                                    inputPitch;                     /**< [in]: Specifies the input buffer pitch. If the pitch value is unknown, set this to \p inputWidth. */
    uint32_t                                    encodePicFlags;                 /**< [in]: Specifies bit-wise OR`ed encode pic flags. See ::MT_ENC_PIC_FLAGS. */
    MT_ENC_BUFFER_FORMAT                        bufferFmt;                      /**< [in]: Specifies the input buffer format. */
    MT_ENC_INPUT_PTR                            inputBuffer;                    /**< [in]: Specifies an input buffer pointer. The client must use the pointer obtained from ::MTEncMapResource().*/
    MT_ENC_OUTPUT_PTR                           outputBuffer;                   /**< [in]: Specifies a pointer to the output buffer. The client should use the pointer obtained from ::MTEncCreateOutputBuffer(). */
    void*                                       completionEvent;                /**< [in]: Specifies an event to be signaled on the completion of encoding
                                                                                           this frame when operating in the asynchronous mode.
                                                                                           Each output buffer should be associated with a distinct event pointer. */
    uint32_t                                    inputBufferType : 3;            /**< [in]: Specifies the input buffer type. See ::MT_ENC_BUFFER_TYPE. Used only on Linux. */
    uint32_t                                    reservedBitFields : 29;         /**< [in]: Reserved and must be set to 0. */
    uint32_t                                    roiNumber;                      /**< [in]: Specifies the input ROI array number. */
    MT_ENCODER_ROI*                             roi;                            /**< [in]: Specifies the input ROI array. */
    uint64_t                                    inputTimeStamp;                 /**< [in]: Specifies presentation timestamp associated with the input picture. */
    uint64_t                                    inputDuration;                  /**< [in]: Specifies duration of the input picture */
    int8_t*                                     qpDeltaMap;                     /**< [in]: Specifies the pointer to signed byte array containing value per MB for H264, per CTB for HEVC in raster scan order for the current picture.
                                                                                            qpDeltaMap specifies QP modifier per MB for H264, per CTB for HEVC. This QP modifier will be applied on top of the QP chosen by rate control.
                                                                                            Need libva >= 2.18 on Linux*/
    uint32_t                                    qpDeltaMapSize;                 /**< [in]: Specifies the size in bytes of qpDeltaMap surface allocated by client and pointed to by MT_ENC_PIC_PARAMS::qpDeltaMap. Surface (array) should be picWidthInMbs * picHeightInMbs for H264, picWidthInCtbs * picHeightInCtbs for HEVC*/
    uint32_t                                    seiPayloadArrayCnt;             /**< [in]: Specifies the number of elements allocated in  seiPayloadArray array. */
    MT_ENC_SEI_PAYLOAD*                         seiPayloadArray;                /**< [in]: Array of SEI payloads which will be inserted for this frame. */
    MT_ENC_TIME_CODE                            timeCode;                       /**< [in]: Specifies the clock timestamp sets used in picture timing SEI. Applicable only when MT_ENC_CONFIG_H264::enableTimeCode or MT_ENC_CONFIG_HEVC::enableTimeCodeSEI is set to 1. */
    uint32_t                                    reserved1[235];                 /**< [in]: Reserved and must be set to 0. */
    void*                                       reserved2[58];                  /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_PIC_PARAMS;

/**
 * Bitstream buffer lock parameters.
 */
typedef struct _MT_ENC_LOCK_BUFFER
{
    uint32_t                version;                        /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    uint32_t                frameIdx;                       /**< [out]: The frame index for which the buffer is being retrieved. */
    MT_ENC_PIC_TYPE         pictureType;                    /**< [out]: The picture type of the encoded picture. */
    uint32_t                outputBufferSizeInBytes;        /**< [out]: The actual number of bytes generated and copied to the memory pointed by \p outputBufferPtr. */
    void*                   outputBufferPtr;                /**< [out]: A pointer to the generated output buffer. */
    void*                   lockedOutputBuffer;             /**< [out]: A pointer to the output buffer being locked. */
    uint32_t                timeoutInMilliseconds;          /**< [in]: Maximum waiting time while locking the buffer. */
    uint32_t                reserved;                       /**< [in]: Reserved and must be set to 0. */
    uint64_t                outputTimeStamp;                /**< [out]: Presentation timestamp associated with the encoded output. */
    uint64_t                outputDuration;                 /**< [out]: Presentation duration associated with the encoded output. */
    int64_t                 decodeTimeStamp;                /**< [out]: Decoding timestamp associated with the encoded output.*/
    uint32_t                reserved1[244];                 /**< [in]: Reserved and must be set to 0. */
    void*                   reserved2[62];                  /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_LOCK_BUFFER;

/**
 * Maps an input resource to the MT encoder input buffer.
 */
typedef struct _MT_ENC_MAP_RESOURCE
{
    uint32_t                   version;                   /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    MT_ENC_INPUT_RESOURCE_TYPE resourceType;              /**< [in]: The type of the resource to be mapped. */
    MT_ENC_INPUT_PTR           resourceToMap;             /**< [in]: Handle to the resource that is being mapped. */
    MT_ENC_INPUT_PTR           mappedResource;            /**< [out]: Mapped pointer corresponding to \p resourceToMap. This pointer must be used in MT_ENC_PIC_PARAMS::inputBuffer in ::MTEncEncodeFrame(). */
    uint32_t                   reserved1[254];            /**< [in]: Reserved and must be set to 0. */
    void*                      reserved2[62];             /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_MAP_RESOURCE;

/**
 * Encoder creation parameters.
 */
typedef struct _MT_ENC_CREATE_ENCODER_PARAMS
{
    uint32_t            version;                          /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    MT_ENC_DEVICE_TYPE  deviceType;                       /**< [in]: The device type. */
    void*               device;                           /**< [in]: A pointer to the client device. */
    uint32_t            reserved1[254];                   /**< [in]: Reserved and must be set to 0. */
    void*               reserved2[63];                    /**< [in]: Reserved and must be set to NULL. */
} MT_ENC_CREATE_ENCODER_PARAMS;


/**
 * Input struct for querying Encoding capabilities.
 */
typedef struct _MT_ENC_CAPS_PARAM
{
    uint32_t version;                                  /**< [in]: The struct version. Must be set to ::MT_ENC_CAPS_PARAM_VER. */
    MT_ENC_CAPS  capsToQuery;                          /**< [in]: Specifies the encode capability to query. Client should pass a member of the ::MT_ENC_CAPS enum. */
    uint32_t reserved[62];                             /**< [in]: Reserved and must be set to 0. */
} MT_ENC_CAPS_PARAM;

/**
 * \struct _MT_ENC_SEQUENCE_PARAM_PAYLOAD
 * Sequence and picture paramaters payload.
 */
typedef struct _MT_ENC_SEQUENCE_PARAM_PAYLOAD
{
    uint32_t            version;                         /**< [in]: The struct version. Must be set to #MTENCAPI_VERSION. */
    uint32_t            inBufferSize;                    /**< [in]:  Specifies the size of the spsppsBuffer provided by the client */
    void*               spsppsBuffer;                    /**< [in]:  Specifies bitstream header pointer of size MT_ENC_SEQUENCE_PARAM_PAYLOAD::inBufferSize.
                                                                     It is the client's responsibility to manage this memory. */
    uint32_t*           outSPSPPSPayloadSize;            /**< [out]: Size of the sequence and picture header in bytes. */
    uint32_t            reserved [254];                  /**< [in]:  Reserved and must be set to 0 */
    void*               reserved2[62];                   /**< [in]:  Reserved and must be set to NULL */
} MT_ENC_SEQUENCE_PARAM_PAYLOAD;

/** @} */ /* END ENCODER_STRUCTURE */

/**
 * \addtogroup ENCODE_FUNC MTEncodeAPI functions
 * @{
 */

// MTEncCreateEncoder
/**
 * \brief Creates an encoder.
 *
 * Creates an encoder and returns a pointer to the encoder interface in
 * the \p **encoder parameter. The client should start the encoding process by calling
 * this function first.
 * The client must pass a pointer to the device in the \p *device parameter.
 * If the creation of encoder fails, the client must call ::MTEncReleaseEncoder()
 * before exiting.
 *
 * \param [in] createEncoderParams
 *    A pointer to ::MT_ENC_CREATE_ENCODER_PARAMS.
 * \param [out] encoder
 *    The encoder pointer to the MTEncodeAPI interface.
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_UNSUPPORTED_DEVICE \n
 * ::MT_ENC_ERR_GENERIC \n
 *
 */
MTENCSTATUS MTENCAPI MTEncCreateEncoder                     (MT_ENC_CREATE_ENCODER_PARAMS *createEncoderParams, void** encoder);

// MTEncGetPresetConfig
/**
 * \brief Returns a preset configuration structure supported for a given preset ID.
 *
 * The preset configuration structure can be modified by the client depending
 * on its use case and then used to initialize the encoder using
 * ::MTEncInitEncoder().
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in] encodeID
 *   The encode ID, corresponding to which the supported presets are to be
 *   retrieved.
 * \param [in] presetID
 *   The preset ID, corresponding to which the encoding configurations are to be
 *   retrieved.
 * \param [out] presetConfig
 *   The requested preset encoder attribute set. For more information, see ::_MT_ENC_CONFIG.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncGetPresetConfig               (void* encoder, MT_ENC_CODEC_ID encodeID, MT_ENC_PRESET_ID presetID, MT_ENC_PRESET_CONFIG* presetConfig);

// MTEncInitEncoder
/**
 * \brief Initializes the encoder.
 *
 * This function must be used to initialize the encoder. The initialization parameters
 * are passed using \p *initParams. The client must send the following
 * fields of the _MT_ENC_INIT_PARAMS structure with a valid value.
 * - MT_ENC_INIT_PARAMS::encodeID
 * - MT_ENC_INIT_PARAMS::encodeWidth
 * - MT_ENC_INIT_PARAMS::encodeHeight
 *
 * The client can pass a preset ID directly to the MTEncodeAPI interface using
 * MT_ENC_INIT_PARAMS::presetID. If the client does not pass
 * MT_ENC_INIT_PARAMS::encodeConfig, the codec-specific parameters
 * will be selected based on the preset ID.
 * If the client passes a custom ::_MT_ENC_CONFIG structure through
 * MT_ENC_INIT_PARAMS::encodeConfig, it will override the codec-specific parameters
 * based on the preset ID. It is recommended that even if the client passes a custom configuration,
 * it should also send a preset ID. In this case, the preset ID passed by the client
 * will not override any of the custom configuration parameters programmed by the client,
 * it is only used as a hint by the MTEncodeAPI interface to determine certain encoder parameters
 * which are not exposed to the client.
 *
 * The encoder supports the following operation modes:
 * - The asynchronous mode
 * - The synchronous mode
 *
 * The client can select asynchronous or synchronous mode by setting \p
 * enableEncodeAsync in ::_MT_ENC_INIT_PARAMS to 1 or 0 respectively.
 *\par Asynchronous mode of operation
 * The asynchronous mode can be enabled by setting MT_ENC_INIT_PARAMS::enableEncodeAsync to 1.
 * The client operating in the asynchronous mode must allocate a completion event object
 * for each output buffer and pass the completion event object in the
 * ::MTEncEncodeFrame() function. The client can create another thread and wait on
 * the event object to be signalled by the MTEncodeAPI interface on completion of the
 * encoding process for the output frame. This should unblock the main thread from
 * submitting work to the encoder. When the event is signalled, the client can call
 * MTEncodeAPI interfaces to copy the bitstream data using the ::MTEncLockOutputBuffer()
 * function. This is the preferred mode of operation.
 *
 * NOTE: Asynchronous mode is not supported on Linux.
 *
 *\par Synchronous mode of operation
 * The client can select the synchronous mode by setting MT_ENC_INIT_PARAMS::enableEncodeAsync to 0.
 * The client working in the synchronous mode can work in a single-threaded or multithreaded mode.
 * The client does not need to allocate any event objects. The client can
 * only lock the bitstream data after the MTEncodeAPI interface has returned
 * ::MT_ENC_SUCCESS from encoding pictures. MTEncodeAPI can return the
 * ::MT_ENC_ERR_NEED_MORE_INPUT error code from ::MTEncEncodeFrame(). The
 * client must not lock the output buffer in such a case but should send the next
 * frame for encoding. The client must keep on calling ::MTEncEncodeFrame()
 * until it returns ::MT_ENC_SUCCESS. \n
 * The client must always lock the bitstream data in the order in which it has been submitted.
 *
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in] initParams
 *   For more information, see ::_MT_ENC_INIT_PARAMS.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncInitEncoder                     (void* encoder, MT_ENC_INIT_PARAMS* initParams);

// MTEncReconfigureEncoder
/**
 * \brief Reconfigure an existing encoding session.
 *
 * Reconfigure an existing encoding session.
 * The client should call this API to change/reconfigure the parameter passed during
 * MTEncInitializeEncoder API call.
 * Currently Reconfiguration of following are supported.
 * Change in bitrate.
 * Change in fps.
 *
 * \param [in] encoder
 *   Pointer to the MTEncodeAPI interface.
 *
 * \param [in] reInitEncodeParams
 *    Pointer to a ::MT_ENC_RECONFIGURE_PARAMS structure.
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_GENERIC \n
 *
 */
MTENCSTATUS MTENCAPI MTEncReconfigureEncoder                   (void *encoder, MT_ENC_RECONFIGURE_PARAMS* reInitEncodeParams);

// MTEncCreateOutputBuffer
/**
 * \brief Allocates an output bitstream buffer
 *
 * This function is used to allocate an output bitstream buffer and returns a
 * MT_ENC_OUTPUT_PTR to bitstream  buffer to the client in
 * MT_ENC_CREATE_OUTPUT_BUFFER::outputBuffer.
 * The client can only call this function after the encoder has been
 * initialized using ::MTEncInitEncoder(). The minimum number of output
 * buffers allocated by the client must be at least (2 * _MT_ENC_CONFIG::frameIntervalP + 1).
 * The client can only access the output bitsteam data by locking
 * \p outputBuffer using ::MTEncLockOutputBuffer().
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in,out] createOutputBufferParams
 *   A pointer to ::MT_ENC_CREATE_OUTPUT_BUFFER.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_ENCODER_NOT_INITIALIZED \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncCreateOutputBuffer                 (void* encoder, MT_ENC_CREATE_OUTPUT_BUFFER* createOutputBufferParams);

// MTEncReleaseOutputBuffer
/**
 * \brief Releases an output buffer.
 *
 * This function is used to release the output bitstream buffer allocated using
 * ::MTEncCreateOutputBuffer(). The client must release the output
 * bitstream buffer using this function before destroying the encoder.
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in] outputBuffer
 *   A pointer to the bitstream buffer being released.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_ENCODER_NOT_INITIALIZED \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncReleaseOutputBuffer                (void* encoder, MT_ENC_OUTPUT_PTR outputBuffer);

// MTEncEncodeFrame
/**
 * \brief Submits an input picture for encoding.
 *
 * This function is used to submit an input picture buffer for encoding. The
 * encoding parameters are passed using \p *encodePicParams which is a pointer
 * to ::_MT_ENC_PIC_PARAMS.
 *
 * The MTEncodeAPI interface might return the ::MT_ENC_ERR_NEED_MORE_INPUT error code
 * but the client must not treat it as a fatal error. The MTEncodeAPI
 * interface might not be able to submit an input picture buffer for encoding
 * immediately due to re-ordering for B-frames. The MTEncodeAPI interface cannot
 * submit the input picture which is decided to be encoded as a B-frame as it waits
 * for backward reference from temporally subsequent frames. This input picture
 * is buffered internally and waits for more input pictures to arrive. The client
 * must not call ::MTEncLockOutputBuffer() on the output buffers whose
 * ::MTEncEncodeFrame() returns ::MT_ENC_ERR_NEED_MORE_INPUT. The client must
 * wait for the MTEncodeAPI interface to return ::MT_ENC_SUCCESS before locking the
 * output buffer to read the encoded bitstream data.
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in,out] encodePicParams
 *   A pointer to the ::_MT_ENC_PIC_PARAMS structure.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_ENCODER_BUSY \n
 * ::MT_ENC_ERR_NEED_MORE_INPUT \n
 * ::MT_ENC_ERR_ENCODER_NOT_INITIALIZED \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncEncodeFrame                         (void* encoder, MT_ENC_PIC_PARAMS* encodePicParams);

// MTEncLockOutputBuffer
/**
 * \brief Locks output buffer.
 *
 * This function is used to lock the buffer to read the encoded data.
 * The client can only access the encoded data by calling this function.
 * A pointer to client accessible encoded data is returned in the
 * MT_ENC_LOCK_BUFFER::outputBufferPtr field. The size of the encoded data
 * in the output buffer is returned in MT_ENC_LOCK_BUFFER::outputBufferSizeInBytes.
 * The MTEncodeAPI interface also returns the output picture type
 * of the encoded frame in MT_ENC_LOCK_BUFFER::pictureType.
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in,out] lockOutputBufferParams
 *   A pointer to the ::_MT_ENC_LOCK_BUFFER structure.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_ENCODER_NOT_INITIALIZED \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncLockOutputBuffer                         (void* encoder, MT_ENC_LOCK_BUFFER* lockOutputBufferParams);

// MTEncUnlockOutputBuffer
/**
 * \brief Unlocks the output buffer.
 *
 * This function is used to unlock the output buffer after the client
 * has read the encoded data from the output buffer. The client must call this function
 * to unlock the output buffer which it has previously locked using ::MTEncLockOutputBuffer()
 * . Using a locked buffer in ::MTEncEncodeFrame() will cause
 * the function to fail.
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in,out] outputBuffer
 *   The bitstream buffer pointer that is being unlocked.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_ENCODER_NOT_INITIALIZED \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncUnlockOutputBuffer                       (void* encoder, MT_ENC_OUTPUT_PTR outputBuffer);

// MTEncMapResource
/**
 * \brief Maps an externally created input resource pointer for encoding.
 *
 * Maps an externally allocated input resource using and returns an MT_ENC_INPUT_PTR
 * which can be used for encoding in ::MTEncEncodeFrame(). The
 * mapped resource is returned in MT_ENC_MAP_RESOURCE::mappedResource.
 * This function provides synchronization guarantee that any graphics or compute
 * work submitted on the input buffer is completed before the buffer is used for encoding.
 * The client should not access any input buffer while they are being mapped by the encoder.
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in,out] mapInputResParams
 *   A pointer to ::_MT_ENC_MAP_RESOURCE.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_ENCODER_NOT_INITIALIZED \n
 * ::MT_ENC_ERR_MAP_FAILED \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncMapResource                         (void* encoder, MT_ENC_MAP_RESOURCE* mapInputResParams);

// MTEncUnmapResource
/**
 * \brief Unmaps an MT_ENC_INPUT_PTR which was mapped for encoding.
 *
 *
 * Unmaps an input buffer that was previously mapped using ::MTEncMapResource().
 * The mapping created using ::MTEncMapResource() should be invalidated
 * using this function before the external resource is destroyed by the client. The client
 * must unmap the buffer after ::MTEncLockOutputBuffer() returns successfully for encode
 * work submitted using the mapped input buffer.
 *
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 * \param [in] mappedInputBuffer
 *   A pointer to ::MT_ENC_INPUT_PTR.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_INVALID_VERSION \n
 * ::MT_ENC_ERR_ENCODER_NOT_INITIALIZED \n
 * ::MT_ENC_ERR_RESOURCE_NOT_MAPPED \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncUnmapResource                         (void* encoder, MT_ENC_INPUT_PTR mappedInputBuffer);

// MTEncReleaseEncoder
/**
 * \brief Releases an encoder.
 *
 * Releases the encoder previously created using ::MTEncCreateEncoder().
 * The client must flush the encoder before freeing any resources. To
 * flush the encoder, the client must pass a NULL encode picture packet and either
 * wait for ::MTEncEncodeFrame() to return in the synchronous mode.
 * The client must free all the input and output resources created using
 * MTEncodeAPI before releasing the encoder.
 *
 * \param [in] encoder
 *   A pointer to the MTEncodeAPI interface.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_NO_ENCODE_DEVICE \n
 * ::MT_ENC_ERR_OUT_OF_MEMORY \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncReleaseEncoder                        (void* encoder);


// MTEncGetCaps
/**
 * \brief Retrieves the capability value for a specified encoder attribute.
 *
 * The function returns the capability value for a given encoder attribute.
 * The encoder attributes being queried are enumerated in ::MT_ENC_CAPS_PARAM fields.
 *
 * \param [in] encoder
 *   Pointer to the MTEncodeAPI interface.
 * \param [in] encodeID
 *   Encode ID, corresponding to which the capability attribute is to be retrieved.
 * \param [in] capsParam
 *   Used to specify attribute being queried. Refer ::MT_ENC_CAPS_PARAM for  more
 * details.
 * \param [out] capsVal
 *   The value corresponding to the capability attribute being queried.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_GENERIC \n
 */
MTENCSTATUS MTENCAPI MTEncGetCaps                     (void* encoder, MT_ENC_CODEC_ID encodeID, MT_ENC_CAPS_PARAM* capsParam, int* capsVal);

// MTEncGetSequenceParams
/**
 * \brief Get encoded sequence and picture header.
 *
 * This function can be used to retrieve the sequence and picture header out of
 * band. The client must call this function only after the encoder has been
 * initialized using ::MTEncInitEncoder() function. The client must
 * allocate the memory where the MtEncodeAPI interface can copy the bitstream
 * header and pass the pointer to the memory in MT_ENC_SEQUENCE_PARAM_PAYLOAD::spsppsBuffer.
 * The size of buffer is passed in the field  MT_ENC_SEQUENCE_PARAM_PAYLOAD::inBufferSize.
 * The MtEncodeAPI interface will copy the bitstream header payload and returns
 * the actual size of the bitstream header in the field
 * MT_ENC_SEQUENCE_PARAM_PAYLOAD::outSPSPPSPayloadSize.
 *
 * \param [in] encoder
 *   Pointer to the MtEncodeAPI interface.
 * \param [in,out] sequenceParamPayload
 *   Pointer to the ::_MT_ENC_SEQUENCE_PARAM_PAYLOAD structure.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 * ::MT_ENC_ERR_GENERIC \n
 *
 */
MTENCSTATUS MTENCAPI MTEncGetSequenceParams          (void* encoder, MT_ENC_SEQUENCE_PARAM_PAYLOAD* seqPayload);

// MTEncodeAPIGetMaxSupportedVersion
/**
 * \brief Gets the latest MTEncodeAPI version supported by the driver.
 *
 * This function can be used by clients to determine if the driver supports
 * the MTEncodeAPI header that the application was compiled with.
 *
 * \param [out] version
 *   A pointer to the requested value. The 4 least significant bits in the returned value
 *   indicate the minor version and the rest of the bits indicate the major
 *   version of the latest supported version.
 *
 * \return
 * ::MT_ENC_SUCCESS \n
 * ::MT_ENC_ERR_INVALID_PARAM \n
 */
MTENCSTATUS MTENCAPI MTEncodeAPIGetMaxSupportedVersion          (uint32_t* version);

/// \cond API PFN
/*
 *  Defines API function pointers.
 */
typedef MTENCSTATUS (MTENCAPI* PMTENCCREATEENCODER)         (MT_ENC_CREATE_ENCODER_PARAMS *createEncoderParams, void** encoder);
typedef MTENCSTATUS (MTENCAPI* PMTENCGETPRESETCONFIG)       (void* encoder, MT_ENC_CODEC_ID encodeID, MT_ENC_PRESET_ID presetID, MT_ENC_PRESET_CONFIG* presetConfig);
typedef MTENCSTATUS (MTENCAPI* PMTENCINITENCODER)           (void* encoder, MT_ENC_INIT_PARAMS* initParams);
typedef MTENCSTATUS (MTENCAPI* PMTENCRECONFIGUREENCODER)    (void* encoder, MT_ENC_RECONFIGURE_PARAMS* reInitParams);
typedef MTENCSTATUS (MTENCAPI* PMTENCCREATEOUTPUTBUFFER)    (void* encoder, MT_ENC_CREATE_OUTPUT_BUFFER* createOutputBufferParams);
typedef MTENCSTATUS (MTENCAPI* PMTENCRELEASEOUTPUTBUFFER)   (void* encoder, MT_ENC_OUTPUT_PTR outputBuffer);
typedef MTENCSTATUS (MTENCAPI* PMTENCENCODEFRAME)           (void* encoder, MT_ENC_PIC_PARAMS* encodePicParams);
typedef MTENCSTATUS (MTENCAPI* PMTENCLOCKOUTPUTBUFFER)      (void* encoder, MT_ENC_LOCK_BUFFER* lockOutputBufferParams);
typedef MTENCSTATUS (MTENCAPI* PMTENCUNLOCKOUTPUTBUFFER)    (void* encoder, MT_ENC_OUTPUT_PTR outputBuffer);
typedef MTENCSTATUS (MTENCAPI* PMTENCMAPRESOURCE)           (void* encoder, MT_ENC_MAP_RESOURCE* mapInputResParams);
typedef MTENCSTATUS (MTENCAPI* PMTENCUNMAPRESOURCE)         (void* encoder, MT_ENC_INPUT_PTR mappedInputBuffer);
typedef MTENCSTATUS (MTENCAPI* PMTENCRELEASEENCODER)        (void* encoder);
typedef MTENCSTATUS (MTENCAPI* PMTENCGETCAPS)               (void* encoder, MT_ENC_CODEC_ID encodeID, MT_ENC_CAPS_PARAM* capsParam, int* capsVal);
typedef MTENCSTATUS (MTENCAPI* PMTENCGETSEQUENCEPARAMS)     (void* encoder, MT_ENC_SEQUENCE_PARAM_PAYLOAD* seqPayload);

/// \endcond

/** @} */ /* END ENCODE_FUNC */

/**
 * \ingroup ENCODER_STRUCTURE
 * A list of pointers to the MTEncodeAPI functions.
 */
typedef struct _MT_ENCODE_API_FUNCTION_LIST
{
    uint32_t                            version;                            /**< [in]: The client should pass #MTENCAPI_VERSION.                           */
    uint32_t                            sdkVersion;                         /**< [out]: The SDK version. */
    PMTENCCREATEENCODER                 mtEncCreateEncoder;                 /**< [out]: The client should access ::MTEncCreateEncoder() by using this pointer.        */
    PMTENCGETPRESETCONFIG               mtEncGetPresetConfig;               /**< [out]: The client should access ::MTEncGetPresetConfig() by using this pointer.    */
    PMTENCINITENCODER                   mtEncInitEncoder;                   /**< [out]: The client should access ::MTEncInitEncoder() by using this pointer.        */
    PMTENCCREATEOUTPUTBUFFER            mtEncCreateOutputBuffer;            /**< [out]: The client should access ::MTEncCreateOutputBuffer() by using this pointer.    */
    PMTENCRELEASEOUTPUTBUFFER           mtEncReleaseOutputBuffer;           /**< [out]: The client should access ::MTEncReleaseOutputBuffer() by using this pointer.   */
    PMTENCENCODEFRAME                   mtEncEncodeFrame;                   /**< [out]: The client should access ::MTEncEncodeFrame() by using this pointer.            */
    PMTENCLOCKOUTPUTBUFFER              mtEncLockOutputBuffer;              /**< [out]: The client should access ::MTEncLockOutputBuffer() by using this pointer.            */
    PMTENCUNLOCKOUTPUTBUFFER            mtEncUnlockOutputBuffer;            /**< [out]: The client should access ::MTEncUnlockOutputBuffer() by using this pointer.          */
    PMTENCMAPRESOURCE                   mtEncMapResource;                   /**< [out]: The client should access ::MTEncMapResource() by using this pointer.         */
    PMTENCUNMAPRESOURCE                 mtEncUnmapResource;                 /**< [out]: The client should access ::MTEncUnmapResource() by using this pointer.       */
    PMTENCRELEASEENCODER                mtEncReleaseEncoder;                /**< [out]: The client should access ::MTEncReleaseEncoder() by using this pointer.           */
    PMTENCRECONFIGUREENCODER            mtEncReconfigureEncoder;            /**< [out]: The client should access ::MTEncReconfigureEncoder() through this pointer.       */
    PMTENCGETCAPS                       mtEncGetCaps;                       /**< [out]: The Client should access ::MTEncGetCaps() through this pointer.            */
    PMTENCGETSEQUENCEPARAMS             mtEncGetSequenceParams;             /**< [out]: The Client should access ::MTEncGetSequenceParams() through this pointer.            */
    void*                               reserved1[242];                     /**< [in]: Reserved and must be set to NULL. */
} MT_ENCODE_API_FUNCTION_LIST;

// MTEncodeAPICreateInstance
/**
 * \ingroup ENCODE_FUNC
 * \brief The entry point to the MTEncodeAPI interface.
 *
 * Creates an instance of the MTEncodeAPI interface, and populates the
 * \p functionList with function pointers to the API routines implemented by
 * MTEncodeAPI.
 *
 * \param [out] functionList
 *
 * \return
 * ::MT_ENC_SUCCESS
 * ::MT_ENC_ERR_INVALID_PARAM
 * ::MT_ENC_ERR_INVALID_VERSION
 */
MTENCSTATUS MTENCAPI MTEncodeAPICreateInstance(MT_ENCODE_API_FUNCTION_LIST *functionList);

#ifdef __cplusplus
}
#endif

#endif
