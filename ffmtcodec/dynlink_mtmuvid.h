/***************************************************************************************************
 * Copyright @2025 Moore Threads Technology Co., Ltd ("Moore Threads"). All
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

#if !defined(__MTMUVID_H__)
#define __MTMUVID_H__

#include "dynlink_muviddec.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * \mainpage MTDEC for Linux
 *
 * \section decoder MTDEC
 * MTDEC is a video decoding library for Moore Threads GPU devices.
 * MTDEC contains parser and decoder functions that can be used to deal video at together or be used alone.
 * MTDEC can be used for decoding bitstreams of various formats including H264, HEVC, VP9 and AV1.
 * For parser, it can provide the input bitstream as one complete frame of data, a partial frame (less than one complete frame), or as multiple frames.
 * For decoder, it decode the compressed video streams and copies the resulting YUV frames to system memory, or do post-processing.
 *
 * MTDEC api also provides implementiong of commonly used post-processing operations such as scaling, cropping, aspect ration conversion,
 * and color space conversion to mang popular output video formats.
 * Decoded video frames can be presented to the display with graphics interoperablitiy for video playback, eg: OpenGL, vulkan,
 * passed directly to a dedicated hardware encoder [MTENC] for high-performance video transcoding.
 *
 * The MTDEC functions, structures, and other parameters are exposed in mtmuvid.h and muviddec.h.
 * Broadly, the using flow consists of the following steps:
 * 1. Creates a parser instance.
 * 2. Send bitstream data to parser.
 * 3. Create a decoder instance.
 * 4. Decode one video frame.
 * 5. Get the decoded video frame, including scaling, cropping, color space conversion, and aspect ratio conversion.
 * 6. Releases the parser and decoder.
 *
 * \code
 * // pseudocode
 * // open video module
 * hModule = dlopen(TEXT("libvideo_musa.so"));
 *
 * MUVIDFUNCTIONS mtvid = {};
 * mtvid.muvid* = dlsym(hModule, "muvid*");
 * // create parser
 * MUvideoparser hParser = NULL;
 * MUvideodecoder hDecoder = NULL;
 *
 * mtvid.muvidCreateVideoParser(&hParser, parameters);
 *
 * MUVIDSOURCEDATAPACKET packet = { 0 };
 * while(1)
 * {
 *      readBitstream(&packet);
 *      mtvid.muvidParseVideoData(hParser, &packet);
 *
 *      if (sps/pps)
 *      {
 *          // call pfnSequenceCallback and create decoder
 *          mtvid.muvidCreateDecoder(&hDecoder, parameters);
 *       }
 *      if (one complete frame)
 *      {
 *          // if get one complete frame of data will call pfnDecodePicture
 *          mtvid.muvidDecodePicture(hDecoder, parameters);
 *      }
 *
 *      if (need display)
 *      {
 *          // call pfnDisplayCallback
 *          mtvid.muvidMapVideoFrame(hDecoder, parameters);
 *          // when call muvidMapVideoFrame will do post-processing include scaling, cropping, color conversion and so on
 *          // do some other things
 *          mtvid.muvidUnmapVideoFrame(hDecoder, parameters)
 *       }
 * }
 * mtvid.muvidDestroyVideoParser(hParser);
 * mtvid.muvidDestroyDecoder(hDecoder);
 * dlclose(hModule)
 *
 * \endcode
 */

/**
 * \addtogroup STRUCTS MTMUVID Data Structures
 * @{
 */

#define MAX_CLOCK_TS 3

typedef void *MUvideoparser;
typedef long long MUvideotimestamp;

/************************************************************************/
//! \ingroup STRUCTS
//! \struct HEVCTIMECODESET
//! Used to store Time code extracted from Time code SEI in HEVC codec
/************************************************************************/
typedef struct _HEVCTIMECODESET
{
    unsigned int time_offset_value;
    unsigned short n_frames;
    unsigned char clock_timestamp_flag;
    unsigned char units_field_based_flag;
    unsigned char counting_type;
    unsigned char full_timestamp_flag;
    unsigned char discontinuity_flag;
    unsigned char cnt_dropped_flag;
    unsigned char seconds_value;
    unsigned char minutes_value;
    unsigned char hours_value;
    unsigned char seconds_flag;
    unsigned char minutes_flag;
    unsigned char hours_flag;
    unsigned char time_offset_length;
    unsigned char reserved;
} HEVCTIMECODESET;

/************************************************************************/
//! \ingroup STRUCTS
//! \struct HEVCSEITIMECODE
//! Used to extract Time code SEI in HEVC codec
/************************************************************************/
typedef struct _HEVCSEITIMECODE
{
    HEVCTIMECODESET time_code_set[MAX_CLOCK_TS];
    unsigned char num_clock_ts;
} HEVCSEITIMECODE;

/**********************************************************************************/
//! \ingroup STRUCTS
//! \struct MUSEIMESSAGE;
//! Used in MUVIDSEIMESSAGEINFO structure
/**********************************************************************************/
typedef struct _MUSEIMESSAGE
{
    unsigned char sei_message_type; /**< OUT: SEI Message Type      */
    unsigned char reserved[3];
    unsigned int sei_message_size;  /**< OUT: SEI Message Size      */
} MUSEIMESSAGE;

/************************************************************************************************/
//! \ingroup STRUCTS
//! \struct MUVIDEOFORMAT
//! Video format
/************************************************************************************************/
typedef struct
{
    musaVideoCodec codec;                   /**< OUT: Compression format          */
   /**
    * OUT: frame rate = numerator / denominator (for example: 30000/1001)
    */
    struct {
        /**< OUT: frame rate numerator   (0 = unspecified or variable frame rate) */
        unsigned int numerator;
        /**< OUT: frame rate denominator (0 = unspecified or variable frame rate) */
        unsigned int denominator;
    } frame_rate;
    unsigned char progressive_sequence;     /**< OUT: 0=interlaced, 1=progressive                                      */
    unsigned char bit_depth_luma_minus8;    /**< OUT: high bit depth luma. E.g, 2 for 10-bitdepth, 4 for 12-bitdepth   */
    unsigned char bit_depth_chroma_minus8;  /**< OUT: high bit depth chroma. E.g, 2 for 10-bitdepth, 4 for 12-bitdepth */
    unsigned char min_num_decode_surfaces;  /**< OUT: Minimum number of decode surfaces to be allocated for correct
                                                      decoding. The client can send this value in ulNumDecodeSurfaces
                                                      (in MUVIDDECODECREATEINFO structure).
                                                      This guarantees correct functionality and optimal video memory
                                                      usage but not necessarily the best performance, which depends on
                                                      the design of the overall application. The optimal number of
                                                      decode surfaces (in terms of performance and memory utilization)
                                                      should be decided by experimentation for each application, but it
                                                      cannot go below min_num_decode_surfaces.
                                                      If this value is used for ulNumDecodeSurfaces then it must be
                                                      returned to parser during sequence callback.                     */
    unsigned int coded_width;               /**< OUT: coded frame width in pixels                                      */
    unsigned int coded_height;              /**< OUT: coded frame height in pixels                                     */
   /**
    * area of the frame that should be displayed
    * typical example:
    * coded_width = 1920, coded_height = 1088
    * display_area = { 0,0,1920,1080 }
    */
    struct {
        int left;                           /**< OUT: left position of display rect    */
        int top;                            /**< OUT: top position of display rect     */
        int right;                          /**< OUT: right position of display rect   */
        int bottom;                         /**< OUT: bottom position of display rect  */
    } display_area;
    musaVideoChromaFormat chroma_format;    /**< OUT:  Chroma format                   */
    unsigned int bitrate;                   /**< OUT: video bitrate (bps, 0=unknown)   */
   /**
    * OUT: Display Aspect Ratio = x:y (4:3, 16:9, etc)
    */
    struct {
        int x;
        int y;
    } display_aspect_ratio;
    /**
    * Video Signal Description
    * Refer section E.2.1 (VUI parameters semantics) of H264 spec file
    */
    struct {
        unsigned char video_format          : 3; /**< OUT: 0-Component, 1-PAL, 2-NTSC, 3-SECAM, 4-MAC, 5-Unspecified     */
        unsigned char video_full_range_flag : 1; /**< OUT: indicates the black level and luma and chroma range           */
        unsigned char reserved_zero_bits    : 4; /**< Reserved bits                                                      */
        unsigned char color_primaries;           /**< OUT: chromaticity coordinates of source primaries                  */
        unsigned char transfer_characteristics;  /**< OUT: opto-electronic transfer characteristic of the source picture */
        unsigned char matrix_coefficients;       /**< OUT: used in deriving luma and chroma signals from RGB primaries   */
    } video_signal_description;
    unsigned int seqhdr_data_length;             /**< OUT: Additional bytes following (MUVIDEOFORMATEX)                  */
    unsigned int reserved1[8];
} MUVIDEOFORMAT;

/****************************************************************/
//! \ingroup STRUCTS
//! \struct MUVIDOPERATINGPOINTINFO
//! Operating point information of scalable bitstream
/****************************************************************/
typedef struct
{
    musaVideoCodec codec;
    union
    {
        struct
        {
            unsigned char  operating_points_cnt;
            unsigned char  reserved24_bits[3];
            unsigned short operating_points_idc[32];
        } av1;
        unsigned char CodecReserved[1024];
    };
    unsigned int reserved1[8];
} MUVIDOPERATINGPOINTINFO;

/**********************************************************************************/
//! \ingroup STRUCTS
//! \struct MUVIDSEIMESSAGEINFO
//! Used in muvidParseVideoData API with PFNVIDSEIMSGCALLBACK pfnGetSEIMsg
/**********************************************************************************/
typedef struct _MUVIDSEIMESSAGEINFO
{
    void *pSEIData;                 /**< OUT: SEI Message Data      */
    MUSEIMESSAGE *pSEIMessage;      /**< OUT: SEI Message Info      */
    unsigned int sei_message_count; /**< OUT: SEI Message Count     */
    unsigned int picIdx;            /**< OUT: SEI Message Pic Index */
    unsigned int reserved[8];
} MUVIDSEIMESSAGEINFO;

/****************************************************************/
//! \ingroup STRUCTS
//! \struct MUVIDAV1SEQHDR
//! AV1 specific sequence header information
/****************************************************************/
typedef struct {
    unsigned int max_width;
    unsigned int max_height;
    unsigned char reserved[1016];
} MUVIDAV1SEQHDR;

/****************************************************************/
//! \ingroup STRUCTS
//! \struct MUVIDEOFORMATEX
//! Video format including raw sequence header information
/****************************************************************/
typedef struct
{
    MUVIDEOFORMAT format;                 /**< OUT: MUVIDEOFORMAT structure */
    union {
        MUVIDAV1SEQHDR av1;
        unsigned char raw_seqhdr_data[1024];  /**< OUT: Sequence header data    */
    };
} MUVIDEOFORMATEX;

/***************************************************************/
//! \enum MUvideopacketflags
//! Data packet flags
//! Used in MUVIDSOURCEDATAPACKET structure
/***************************************************************/
typedef enum {
    MUVID_PKT_ENDOFSTREAM   = 0x01,   /**< Set when this is the last packet for this stream                              */
    MUVID_PKT_TIMESTAMP     = 0x02,   /**< Timestamp is valid                                                            */
    MUVID_PKT_DISCONTINUITY = 0x04,   /**< Set when a discontinuity has to be signalled                                  */
    MUVID_PKT_ENDOFPICTURE  = 0x08,   /**< Set when the packet contains exactly one frame or one field                   */
    MUVID_PKT_NOTIFY_EOS    = 0x10,   /**< If this flag is set along with MUVID_PKT_ENDOFSTREAM, an additional (dummy)
                                           display callback will be invoked with null value of MUVIDPARSERDISPINFO which
                                           should be interpreted as end of the stream.                                   */
} MUvideopacketflags;

/*****************************************************************************/
//! \ingroup STRUCTS
//! \struct MUVIDSOURCEDATAPACKET
//! Data Packet
//! Used in muvidParseVideoData API
//! IN for muvidParseVideoData
/*****************************************************************************/
typedef struct _MUVIDSOURCEDATAPACKET
{
    unsigned long flags;            /**< IN: Combination of MUVID_PKT_XXX flags                              */
    unsigned long payload_size;     /**< IN: number of bytes in the payload (may be zero if EOS flag is set) */
    const unsigned char *payload;   /**< IN: Pointer to packet payload data (may be NULL if EOS flag is set) */
    MUvideotimestamp timestamp;     /**< IN: Presentation time stamp (10MHz clock), only valid if
                                             MUVID_PKT_TIMESTAMP flag is set                                 */
} MUVIDSOURCEDATAPACKET;

/**********************************************************************************/
//! \ingroup STRUCTS
//! \struct MUVIDPARSERDISPINFO
//! Used in muvidParseVideoData API with PFNVIDDISPLAYCALLBACK pfnDisplayPicture
/**********************************************************************************/
typedef struct _MUVIDPARSERDISPINFO
{
    int picture_index;          /**< OUT: Index of the current picture                                                         */
    int progressive_frame;      /**< OUT: 1 if progressive frame; 0 otherwise                                                  */
    int top_field_first;        /**< OUT: 1 if top field is displayed first; 0 otherwise                                       */
    int repeat_first_field;     /**< OUT: Number of additional fields (1=ivtc, 2=frame doubling, 4=frame tripling,
                                     -1=unpaired field)                                                                        */
    MUvideotimestamp timestamp; /**< OUT: Presentation time stamp                                                              */
    int reserved[9];
} MUVIDPARSERDISPINFO;

/// \cond CALLBACK PFN
/***********************************************************************************************************************/
//! Parser callbacks
//! The parser will call these synchronously from within muvidParseVideoData(), whenever there is sequence change or a picture
//! is ready to be decoded and/or displayed.
//! Return values from these callbacks are interpreted as below. If the callbacks return failure, it will be propagated by
//! muvidParseVideoData() to the application.
//! Parser picks default operating point as 0 and outputAllLayers flag as 0 if PFNVIDOPPOINTCALLBACK is not set or return value is
//! -1 or invalid operating point.
//! PFNVIDSEQUENCECALLBACK : 0: fail, 1: succeeded, > 1: override dpb size of parser (set by MUVIDPARSERPARAMS::ulMaxNumDecodeSurfaces
//! while creating parser)
//! PFNVIDDECODECALLBACK   : 0: fail, >=1: succeeded
//! PFNVIDDISPLAYCALLBACK  : 0: fail, >=1: succeeded
//! PFNVIDOPPOINTCALLBACK  : <0: fail, >=0: succeeded (bit 0-9: OperatingPoint, bit 10-10: outputAllLayers, bit 11-30: reserved)
//! PFNVIDSEIMSGCALLBACK   : 0: fail, >=1: succeeded
/***********************************************************************************************************************/
typedef int (MUSAAPI *PFNVIDSEQUENCECALLBACK)(void *, MUVIDEOFORMAT *);
typedef int (MUSAAPI *PFNVIDDECODECALLBACK)(void *, MUVIDPICPARAMS *);
typedef int (MUSAAPI *PFNVIDDISPLAYCALLBACK)(void *, MUVIDPARSERDISPINFO *);
typedef int (MUSAAPI *PFNVIDOPPOINTCALLBACK)(void *, MUVIDOPERATINGPOINTINFO*);
typedef int (MUSAAPI *PFNVIDSEIMSGCALLBACK) (void *, MUVIDSEIMESSAGEINFO *);
/// \endcond


/**************************************/
//! \ingroup STRUCTS
//! \struct MUVIDPARSERPARAMS
//! Used in muvidCreateVideoParser API
/**************************************/
typedef struct _MUVIDPARSERPARAMS
{
    musaVideoCodec CodecType;                   /**< IN: musaVideoCodec_XXX                                                  */
    unsigned int ulMaxNumDecodeSurfaces;        /**< IN: Max # of decode surfaces (parser will cycle through these)          */
    unsigned int ulClockRate;                   /**< IN: Timestamp units in Hz (0=default=10000000Hz)                        */
    unsigned int ulErrorThreshold;              /**< IN: % Error threshold (0-100) for calling pfnDecodePicture (100=always
                                                     IN: call pfnDecodePicture even if picture bitstream is fully corrupted)
                                                     IN: don't support this for now                                         */
    unsigned int ulMaxDisplayDelay;             /**< IN: Max display queue delay (improves pipelining of decode with display)
                                                         0=no delay (recommended values: 2..4)                               */
    unsigned int bAnnexb : 1;                   /**< IN: AV1 annexB stream                                                   */
    unsigned int uReserved : 31;                /**< Reserved for future use - set to zero                                   */
    unsigned int uReserved1[4];                 /**< IN: Reserved for future use - set to 0                                  */
    void *pUserData;                            /**< IN: User data for callbacks                                             */
    PFNVIDSEQUENCECALLBACK pfnSequenceCallback; /**< IN: Called before decoding frames and/or whenever there is a fmt change */
    PFNVIDDECODECALLBACK pfnDecodePicture;      /**< IN: Called when a picture is ready to be decoded (decode order)         */
    PFNVIDDISPLAYCALLBACK pfnDisplayPicture;    /**< IN: Called whenever a picture is ready to be displayed (display order)  */
    PFNVIDOPPOINTCALLBACK pfnGetOperatingPoint; /**< IN: Called from AV1 sequence header to get operating point of a AV1
                                                         scalable bitstream                                                  */
    PFNVIDSEIMSGCALLBACK pfnGetSEIMsg;          /**< IN: Called when all SEI messages are parsed for particular frame        */
    const char *deviceName;                     /**< IN: The device name to Decode  (for example: /dev/dri/renderD128).
                                                         Used only on Linux.                                                 */
    void *pvReserved2[4];                       /**< Reserved for future use - set to NULL                                   */
    MUVIDEOFORMATEX *pExtVideoInfo;             /**< IN: [Optional] sequence header data from system layer, current no used  */
} MUVIDPARSERPARAMS;


/// \cond API PFN
/*
 *  Defines API function pointers.
 */
typedef MUresult (MUSAAPI *pfnMuvidCreateVideoParser)(MUvideoparser *pObj, MUVIDPARSERPARAMS *pParams);
typedef MUresult (MUSAAPI *pfnMuvidParseVideoData)(MUvideoparser obj, MUVIDSOURCEDATAPACKET *pPacket);
typedef MUresult (MUSAAPI *pfnMuvidDestroyVideoParser)(MUvideoparser obj);
typedef MUresult (MUSAAPI *pfnMuvidGetDecoderCaps)(MUVIDDECODECAPS *pdc);
typedef MUresult (MUSAAPI *pfnMuvidCreateDecoder)(MUvideodecoder *phDecoder, MUVIDDECODECREATEINFO *pdci);
typedef MUresult (MUSAAPI *pfnMuvidDestroyDecoder)(MUvideodecoder hDecoder);
typedef MUresult (MUSAAPI *pfnMuvidDecodePicture)(MUvideodecoder hDecoder, MUVIDPICPARAMS *pPicParams);
typedef MUresult (MUSAAPI *pfnMuvidGetDecodeStatus)(MUvideodecoder hDecoder, int nPicIdx, MUVIDGETDECODESTATUS* pDecodeStatus);
typedef MUresult (MUSAAPI *pfnMuvidReconfigureDecoder)(MUvideodecoder hDecoder, MUVIDRECONFIGUREDECODERINFO *pDecReconfigParams);
typedef MUresult (MUSAAPI *pfnMuvidMapVideoFrame)(MUvideodecoder hDecoder, int nPicIdx, void **pDevPtr,
                                             unsigned int *pPitch, MUVIDPROCPARAMS *pVPP);
typedef MUresult (MUSAAPI *pfnMuvidUnmapVideoFrame)(MUvideodecoder hDecoder, void *DevPtr);

/// \endcond

/** @} */ /* END STRUCTS */

/**
 * \addtogroup MTDEC_FUNC MTMUVID Functions
 * @{
 */
/************************************************************************************************/
//! \ingroup MTDEC_FUNC
//! \fn MUresult MUSAAPI muvidCreateVideoParser(MUvideoparser *pObj, MUVIDPARSERPARAMS *pParams)
//! Create video parser object and initialize
/************************************************************************************************/
typedef MUresult MUSAAPI tmuvidCreateVideoParser(MUvideoparser *pObj, MUVIDPARSERPARAMS *pParams);

/************************************************************************************************/
//! \ingroup MTDEC_FUNC
//! \fn MUresult MUSAAPI muvidParseVideoData(MUvideoparser obj, MUVIDSOURCEDATAPACKET *pPacket)
//! Parse the video data from source data packet in pPacket
//! Extracts parameter sets like SPS, PPS, bitstream etc. from pPacket and
//! calls back pfnDecodePicture with MUVIDPICPARAMS data for kicking of HW decoding
//! calls back pfnSequenceCallback with MUVIDEOFORMAT data for initial sequence header or when
//! the decoder encounters a video format change
//! calls back pfnDisplayPicture with MUVIDPARSERDISPINFO data to display a video frame
/************************************************************************************************/
typedef MUresult MUSAAPI tmuvidParseVideoData(MUvideoparser obj, MUVIDSOURCEDATAPACKET *pPacket);

/************************************************************************************************/
//! \ingroup MTDEC_FUNC
//! \fn MUresult MUSAAPI muvidDestroyVideoParser(MUvideoparser obj)
//! Destroy the video parser
/************************************************************************************************/
typedef MUresult MUSAAPI tmuvidDestroyVideoParser(MUvideoparser obj);

/**********************************************************************************************/

/** @} */ /* END MTDEC_FUNC */

/**
 * \ingroup STRUCTS
 * A list of pointers to the MTDecodeAPI functions.
 */
typedef struct _MUVIDFUNCTIONS
{
    pfnMuvidCreateVideoParser        muvidCreateVideoParser;
    pfnMuvidParseVideoData           muvidParseVideoData;
    pfnMuvidDestroyVideoParser       muvidDestroyVideoParser;
    pfnMuvidGetDecoderCaps           muvidGetDecoderCaps;
    pfnMuvidCreateDecoder            muvidCreateDecoder;
    pfnMuvidDestroyDecoder           muvidDestroyDecoder;
    pfnMuvidDecodePicture            muvidDecodePicture;
    pfnMuvidGetDecodeStatus          muvidGetDecodeStatus;
    pfnMuvidReconfigureDecoder       muvidReconfigureDecoder;
    pfnMuvidMapVideoFrame            muvidMapVideoFrame;
    pfnMuvidUnmapVideoFrame          muvidUnmapVideoFrame;
} MUVIDFUNCTIONS;

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif // __MTMUVID_H__
