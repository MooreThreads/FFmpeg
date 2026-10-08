/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file Intel VAAPI-accelerated encoding API usage example
 * @example vaapi_encode.c
 *
 * Perform VAAPI-accelerated encoding. Read input from an NV12 raw
 * file, and write the H.264 encoded data to an output raw file.
 * Usage: vaapi_encode 1920 1080 input.yuv output.h264
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
#include <libavcodec/vaapi_encode.h>
#include <libavformat/avformat.h>

static int width, height;
static AVBufferRef *hw_device_ctx = NULL;
static enum AVPixelFormat frame_pix_format = AV_PIX_FMT_YUV420P;
static AVFormatContext *ofmt_ctx = NULL;
static int roi_enable = 0;

static int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx)
{
    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;

    if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx))) {
        fprintf(stderr, "Failed to create VAAPI frame context.\n");
        return -1;
    }
    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = AV_PIX_FMT_VAAPI;
    frames_ctx->sw_format = frame_pix_format;
    frames_ctx->width     = width;
    frames_ctx->height    = height;
    frames_ctx->initial_pool_size = 20;
    if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
        fprintf(stderr, "Failed to initialize VAAPI frame context."
                "Error code: %s\n",av_err2str(err));
        av_buffer_unref(&hw_frames_ref);
        return err;
    }
    ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if (!ctx->hw_frames_ctx)
        err = AVERROR(ENOMEM);

    av_buffer_unref(&hw_frames_ref);
    return err;
}

static int encode_write(AVCodecContext *avctx, AVFrame *frame, FILE *fout)
{
    int ret = 0;
    AVPacket *enc_pkt;
    AVFrameSideData *sd;
    unsigned char *data;
    int block_size = ff_vaapi_encoder_get_block_size(avctx);
    int w, h;
    int size;

    if (roi_enable && frame && block_size > 0) {
        w = frame->width / block_size;
        h = frame->height / block_size;
        if (frame->width % block_size)
            w += 1;
        if (frame->height % block_size)
            h += 1;
        size = w * h;
        //size = 0;
        if (size > 0) {
            sd = av_frame_new_side_data(frame, AV_FRAME_DATA_QP_DELTA_MAP, size);
            if (!sd) {
                return -1;
            }
            data = sd->data;
            if (data != NULL) {
                memset(data, 0, size);
                memset(data, 10, size/4);
                memset(data+(size*3)/4, 5, size/4);
            }
        }
    }

    if (!(enc_pkt = av_packet_alloc()))
        return AVERROR(ENOMEM);

    if ((ret = avcodec_send_frame(avctx, frame)) < 0) {
        fprintf(stderr, "Error code: %s\n", av_err2str(ret));
        goto end;
    }
    while (1) {
        ret = avcodec_receive_packet(avctx, enc_pkt);
        if (ret)
            break;

        enc_pkt->stream_index = 0;
        //ret = fwrite(enc_pkt->data, enc_pkt->size, 1, fout);
        ret = av_interleaved_write_frame(ofmt_ctx, enc_pkt);
        if (ret < 0) {
            fprintf(stderr, "Error during writing data to output file. "
                    "Error code: %s\n", av_err2str(ret));
            return -1;
        }
        av_packet_unref(enc_pkt);
        if (!ret) {
            ret = AVERROR(errno);
            break;
        }
    }

end:
    av_packet_free(&enc_pkt);
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

int main(int argc, char *argv[])
{
    int size, err, size_c;
    FILE *fin = NULL, *fout = NULL;
    AVFrame *sw_frame = NULL, *hw_frame = NULL;
    AVCodecContext *avctx = NULL;
    const AVCodec *codec = NULL;
    const char *enc_name = "h264_vaapi";
    int ret = 0;
    AVStream *ost;
    int total_enc_frame = -1, enc_frame = 0;

    if (argc < 5) {
        fprintf(stderr, "Usage: %s <width> <height> <input file> <output file> [h264/hevc/av1_vaapi] \
            [pix_fmt, 0:nv12, 1:yuv420, 2:argb, 3:rgba] [log level] [enc frame num] [roi enable:0/1]\n", argv[0]);
        return -1;
    }

    width  = atoi(argv[1]);
    height = atoi(argv[2]);
    size   = width * height;

    if (!(fin = fopen(argv[3], "r"))) {
        fprintf(stderr, "Fail to open input file : %s\n", strerror(errno));
        return -1;
    }

   if ((ret = (avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, argv[4]))) < 0) {
        fprintf(stderr, "Failed to deduce output format from file extension. Error code: "
                "%s\n", av_err2str(ret));
        goto close;
    }
    ret = avio_open(&ofmt_ctx->pb, argv[4], AVIO_FLAG_WRITE);
    if (ret < 0) {
        fprintf(stderr, "Cannot open output file. "
                "Error code: %s\n", av_err2str(ret));
        goto close;
    }
    if (argc > 5) {
        if (strcmp(argv[5], "h264_vaapi") == 0 || strcmp(argv[5], "hevc_vaapi") == 0 || strcmp(argv[5], "av1_vaapi") == 0) {
            enc_name = argv[5];
        }
    }

    if (argc > 6) {
        int sw_format_setting = atoi(argv[6]);
        switch (sw_format_setting)
        {
        case 0:
            frame_pix_format = AV_PIX_FMT_NV12;
            break;
        case 1:
            frame_pix_format = AV_PIX_FMT_YUV420P;
            break;
        case 2:
            frame_pix_format = AV_PIX_FMT_ARGB;
            break;
        case 3:
            frame_pix_format = AV_PIX_FMT_RGBA;
            break;
        default:
            break;
        }
    }
    if (frame_pix_format == AV_PIX_FMT_ARGB || frame_pix_format == AV_PIX_FMT_RGBA)
        size = 4 * size;
    if (frame_pix_format == AV_PIX_FMT_YUV420P)
            size_c = size / 4;
        else //NV12
            size_c = size / 2;

    int log_level = 0;
    if (argc > 7)
        log_level = atoi(argv[7]);
    if (argc > 8)
        total_enc_frame = atoi(argv[8]);
    if (argc > 9)
        roi_enable = atoi(argv[9]);
    av_log_set_level(log_level);
    err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                 NULL, NULL, 0);
    if (err < 0) {
        fprintf(stderr, "Failed to create a VAAPI device. Error code: %s\n", av_err2str(err));
        goto close;
    }

    if (!(codec = avcodec_find_encoder_by_name(enc_name))) {
        fprintf(stderr, "Could not find encoder.\n");
        err = -1;
        goto close;
    }

    if (!(avctx = avcodec_alloc_context3(codec))) {
        err = AVERROR(ENOMEM);
        goto close;
    }

    avctx->width     = width;
    avctx->height    = height;
    avctx->time_base = (AVRational){1, 25};
    avctx->framerate = (AVRational){25, 1};
    avctx->sample_aspect_ratio = (AVRational){1, 1};
    avctx->pix_fmt   = AV_PIX_FMT_VAAPI;

    /* set hw_frames_ctx for encoder's AVCodecContext */
    if ((err = set_hwframe_ctx(avctx, hw_device_ctx)) < 0) {
        fprintf(stderr, "Failed to set hwframe context.\n");
        goto close;
    }

    if ((err = avcodec_open2(avctx, codec, NULL)) < 0) {
        fprintf(stderr, "Cannot open video encoder codec. Error code: %s\n", av_err2str(err));
        goto close;
    }
    if (!(ost = avformat_new_stream(ofmt_ctx, codec))) {
        fprintf(stderr, "Failed to allocate stream for output format.\n");
        ret = AVERROR(ENOMEM);
        goto close;
    }
    ost->time_base = avctx->time_base;
    ret = avcodec_parameters_from_context(ost->codecpar, avctx);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy the stream parameters. "
                "Error code: %s\n", av_err2str(ret));
        goto close;
    }
    /* write the stream header */
    if ((ret = avformat_write_header(ofmt_ctx, NULL)) < 0) {
        fprintf(stderr, "Error while writing stream header. "
                "Error code: %s\n", av_err2str(ret));
        goto close;
    }
    while (1) {
        if (!(sw_frame = av_frame_alloc())) {
            err = AVERROR(ENOMEM);
            goto close;
        }
        /* read data into software frame, and transfer them into hw frame */
        sw_frame->width  = width;
        sw_frame->height = height;
        sw_frame->format = frame_pix_format;
        if (total_enc_frame > 0 && total_enc_frame <= enc_frame)
            break;
        if ((err = av_frame_get_buffer(sw_frame, 0)) < 0)
            goto close;
        if ((err = fread((uint8_t*)(sw_frame->data[0]), size, 1, fin)) <= 0)
            break;

        if (sw_frame->format != AV_PIX_FMT_ARGB && sw_frame->format != AV_PIX_FMT_RGBA) {
            if ((err = fread((uint8_t*)(sw_frame->data[1]), size_c, 1, fin)) <= 0)
                break;
        }
        if (sw_frame->format == AV_PIX_FMT_YUV420P) {
            if ((err = fread((uint8_t*)(sw_frame->data[2]), size_c, 1, fin)) <= 0)
                break;
        }

        if (!(hw_frame = av_frame_alloc())) {
            err = AVERROR(ENOMEM);
            goto close;
        }
        if ((err = av_hwframe_get_buffer(avctx->hw_frames_ctx, hw_frame, 0)) < 0) {
            fprintf(stderr, "Error code: %s.\n", av_err2str(err));
            goto close;
        }
        if (!hw_frame->hw_frames_ctx) {
            err = AVERROR(ENOMEM);
            goto close;
        }
        if ((err = av_hwframe_transfer_data(hw_frame, sw_frame, 0)) < 0) {
            fprintf(stderr, "Error while transferring frame data to surface."
                    "Error code: %s.\n", av_err2str(err));
            goto close;
        }

        if ((err = (encode_write(avctx, hw_frame, fout))) < 0) {
            fprintf(stderr, "Failed to encode.\n");
            goto close;
        }
        av_frame_free(&hw_frame);
        av_frame_free(&sw_frame);
        enc_frame++;
    }

    /* flush encoder */
    err = encode_write(avctx, NULL, fout);
    if (err == AVERROR_EOF)
        err = 0;
    /* write the trailer for output stream */
    av_write_trailer(ofmt_ctx);
close:
    if (fin)
        fclose(fin);
    if (fout)
        fclose(fout);
    av_frame_free(&sw_frame);
    av_frame_free(&hw_frame);
    avcodec_free_context(&avctx);
    av_buffer_unref(&hw_device_ctx);

    return err;
}
