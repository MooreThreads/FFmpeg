/*
 * Video Acceleration API (video encoding) encode sample
 *
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
 * @file
 * Intel VAAPI-accelerated multithreading encoding example.
 *
 * @example vaapi_encode.c
 * This example shows how to do VAAPI-accelerated encoding. now only support NV12
 * raw file, usage like: vaapi_encode_thread 1920 1080 2 input.yuv output.h264
 * usage: vaapi_encode_thread <width> <height> <thread> <input file> <output file>
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/frame.h"
#include "libavutil/threadmessage.h"

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

struct message
{
    long type;
    AVFrame *frame;
};

typedef struct
{
    AVCodecContext *avctx;
    AVFrame *sw_frame;
    AVFrame *hw_frame;
    FILE *fin;
    char *filename;
    int thread_id;
    int msgid;
} ThreadData;


static int width, height;
static AVBufferRef *hw_device_ctx = NULL;
static FILE *fout = NULL;
static FILE *fin = NULL;
static int thread_id = 0;
static sem_t sem_hw;
static int num_threads;

static int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx)
{
    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;

    if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx)))
    {
        fprintf(stderr, "Failed to create VAAPI frame context.\n");
        return -1;
    }
    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format = AV_PIX_FMT_VAAPI;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = width;
    frames_ctx->height = height;
    frames_ctx->initial_pool_size = 20;
    if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0)
    {
        fprintf(stderr, "Failed to initialize VAAPI frame context."
                        "Error code: %s\n",
                av_err2str(err));
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

    if (!(enc_pkt = av_packet_alloc()))
        return AVERROR(ENOMEM);

    if ((ret = avcodec_send_frame(avctx, frame)) < 0)
    {
        fprintf(stderr, "Error code: %s\n", av_err2str(ret));
        goto end;
    }
    while (1)
    {
        ret = avcodec_receive_packet(avctx, enc_pkt);
        if (ret)
        {
            break;
        }
        enc_pkt->stream_index = 0;
        ret = fwrite(enc_pkt->data, enc_pkt->size, 1, fout);
        av_packet_unref(enc_pkt);
    }

end:
    av_packet_free(&enc_pkt);
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

void *read_func(void *arg)
{
    ThreadData *data = (ThreadData *)arg;
    FILE *fin_thread = NULL;
    fin_thread = fopen(data->filename, "r");
    int size = data->avctx->width * data->avctx->height;
    int err;
    int flag = 0;
    AVFrame *sw_frame;
    AVFrame *hw_frame;
    struct msqid_ds msqstat;

    while (1)
    {
        if (!(sw_frame = av_frame_alloc()))
        {
            err = AVERROR(ENOMEM);
            goto close;
        }

        sw_frame->width = data->avctx->width;
        sw_frame->height = data->avctx->height;
        sw_frame->format = AV_PIX_FMT_NV12;

        if ((err = av_frame_get_buffer(sw_frame, 0)) < 0)
            goto close;

        if (!flag)
        {
            flag = !flag;
            fseek(fin_thread, (data->thread_id - 1) * size * 3 / 2, SEEK_SET);
        }
        else
        {
            unsigned long shift_pos = (num_threads - 1) * size * 3 / 2;
            fseek(fin_thread, shift_pos, SEEK_CUR);
        }

        if ((err = fread((uint8_t *)(sw_frame->data[0]), size, 1, fin_thread)) <= 0)
            break;
        if ((err = fread((uint8_t *)(sw_frame->data[1]), size / 2, 1, fin_thread)) <= 0)
            break;

        sem_wait(&sem_hw);

        if (!(hw_frame = av_frame_alloc()))
        {
            err = AVERROR(ENOMEM);
            goto close;
        }
 
        if ((err = av_hwframe_get_buffer(data->avctx->hw_frames_ctx, hw_frame, 0)) < 0)
        {
            fprintf(stderr, "Error code: %s.\n", av_err2str(err));
            goto close;
        }

        if (!hw_frame->hw_frames_ctx)
        {
            err = AVERROR(ENOMEM);
            goto close;
        }

        if ((err = av_hwframe_transfer_data(hw_frame, sw_frame, 0)) < 0)
        {
            fprintf(stderr, "Error while transferring frame data to surface."
                            "Error code: %s.\n",
                    av_err2str(err));
            goto close;
        }

        struct message msg = {
            .frame = hw_frame,
            .type = data->thread_id,
        };

        if (msgsnd(data->msgid, &msg, sizeof(msg.frame), 0) < 0)
        {
            printf("message send posted\n");
            goto close;
        }
        av_frame_free(&sw_frame);
    }

close:
    while (1)
    {
        msgctl(data->msgid, IPC_STAT, &msqstat);
        if (msqstat.msg_qnum == 0)
        {
            if (msgctl(data->msgid, IPC_RMID, NULL) == -1)
            {
                perror("msgctl");
            }
            break;
        }
    }
    av_frame_free(&sw_frame);
    printf("Free Thread[%d]\n", data->thread_id);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    int err;
    AVFrame *sw_frame = NULL, *hw_frame = NULL;
    AVCodecContext *avctx = NULL;
    const AVCodec *codec = NULL;
    const char *enc_name = "h264_vaapi";
    struct message recv_msg;

    if (argc < 6)
    {
        fprintf(stderr, "Usage: %s <width> <height> <thread> <input file> <output file>\n", argv[0]);
        return -1;
    }

    num_threads=atoi(argv[3]);
    pthread_t read_threads[num_threads];
    ThreadData datas[num_threads];
    sem_init(&sem_hw, 0, num_threads);

    width = atoi(argv[1]);
    height = atoi(argv[2]);

    if (!(fin = fopen(argv[4], "r")))
    {
        fprintf(stderr, "Fail to open input file : %s\n", strerror(errno));
        return -1;
    }
    if (!(fout = fopen(argv[5], "w+b")))
    {
        fprintf(stderr, "Fail to open output file : %s\n", strerror(errno));
        err = -1;
        goto close;
    }

    err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                 NULL, NULL, 0);
    if (err < 0)
    {
        fprintf(stderr, "Failed to create a VAAPI device. Error code: %s\n", av_err2str(err));
        goto close;
    }

    if (!(codec = avcodec_find_encoder_by_name(enc_name)))
    {
        fprintf(stderr, "Could not find encoder.\n");
        err = -1;
        goto close;
    }

    if (!(avctx = avcodec_alloc_context3(codec)))
    {
        err = AVERROR(ENOMEM);
        goto close;
    }

    avctx->width = width;
    avctx->height = height;
    avctx->time_base = (AVRational){1, 25};
    avctx->framerate = (AVRational){25, 1};
    avctx->sample_aspect_ratio = (AVRational){1, 1};
    avctx->pix_fmt = AV_PIX_FMT_VAAPI;

    /* set hw_frames_ctx for encoder's AVCodecContext */
    if ((err = set_hwframe_ctx(avctx, hw_device_ctx)) < 0)
    {
        fprintf(stderr, "Failed to set hwframe context.\n");
        goto close;
    }

    if ((err = avcodec_open2(avctx, codec, NULL)) < 0)
    {
        fprintf(stderr, "Cannot open video encoder codec. Error code: %s\n", av_err2str(err));
        goto close;
    }

    for (int i = 0; i < num_threads; i++)
    {
        datas[i].avctx = avctx;
        datas[i].sw_frame = sw_frame;
        datas[i].hw_frame = hw_frame;
        datas[i].fin = fin;
        datas[i].filename = argv[4];
        datas[i].thread_id = i + 1;
        if ((datas[i].msgid = msgget(i + 2000, IPC_CREAT | 0666)) < 0)
        {
            perror("msgget err!\n");
        }
    }


    for (int i = 0; i < num_threads; i++)
    {
        if (pthread_create(&read_threads[i], NULL, read_func, &datas[i]))
        {
            fprintf(stderr, "Error creating thread.\n");
            goto close;
        }
        fprintf(stderr, "Create Thread[%d] Success!\n", i + 1);
        usleep(1);
    }

    while (1)
    {
        thread_id = thread_id < num_threads ? thread_id + 1 : 1;
        if (msgrcv(datas[thread_id - 1].msgid, &recv_msg, 1024, thread_id, 0) == -1)
        {
            printf("message recv fail\n");
            break;
        }
        else
        {
            if ((err = (encode_write(avctx, recv_msg.frame, fout))) < 0)
            {
                fprintf(stderr, "Failed to encode.\n");
                break;
            }
            av_frame_free(&recv_msg.frame);
        }
        sem_post(&sem_hw);
    }

    for (int i = 0; i < num_threads; i++)
    {
        err = pthread_join(read_threads[i], NULL);
        if (err)
        {
            fprintf(stderr, "Error joining thread");
            goto close;
        }
    }

    /* flush encoder */
    err = encode_write(avctx, NULL, fout);
    if (err == AVERROR_EOF)
        err = 0;

close:
    if (fin)
        fclose(fin);
    if (fout)
        fclose(fout);
    av_frame_free(&sw_frame);
    av_frame_free(&hw_frame);
    avcodec_free_context(&avctx);
    av_buffer_unref(&hw_device_ctx);
    sem_destroy(&sem_hw);
    return err;
}
