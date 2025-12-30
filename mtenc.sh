#!/bin/bash

make

export LD_LIBRARY_PATH=.

mtgpu_file="/dev/mtgpu.0"
if [ -e ${mtgpu_file} ]; then
    echo "mtgpu env exist"
    export LIBVA_DRIVER_NAME=mtgpu
    export LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
    #export LIBVA_DRIVERS_PATH=/home/mt/workbench/mt-media-driver/mt_video/mt_video_dist/x86_64/all/
fi

if [ $# -eq 1 ];then
   yuv_path=${1}
else
    yuv_path=~/data/videos/yuv
fi
echo "yuv path: ${yuv_path}"

rm *.h264 *.h265 *.yuv

export LIBVA_TRACE=vaapi_mtenc_h264_log
export FFREPORT=file=ffmpeg_mtenc_h264.log:level=56

./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 10 -rc_mode CQP -constqp 14 -r 25 -vframes 300 -y test_ffmpeg.h264 > log_test_ffmpeg.log

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/1920x1080_nv12.yuv -s 1920x1080 -src_fmt nv12 -bf 0 -codec h264 -profile baseline -rc cbr -bitrate 2000k -fps 30 -gop 30 -frames 300 -o test_cbr_baseline.h264 > log_baseline1.txt
# FIXME
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1920x1080 -i ${yuv_path}/1920x1080_nv12.yuv -c:v h264_mtenc -profile:v baseline -rc_mode CBR -b:v 2M -g 30 -bf 0 -r 25 -vframes 300 -y test_cbr_baseline_ffmpeg.h264 > log_test_cbr_baseline_ffmpeg.log

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/720p_NV12.yuv -s 1280x720 -src_fmt nv12 -bf 0 -codec h264 -profile baseline -rc cqp -constqp 26 -fps 25 -gop 10 -frames 300 -o test_baseline.h264 > log_baseline.txt
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -profile:v baseline -g 10 -rc_mode CQP -constqp 26 -r 25 -vframes 300 -y test_baseline_ffmpeg.h264 > log_test_baseline_ffmpeg.log

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/720p_NV12.yuv -codec h264 -src_fmt nv12 -s 1280x720 -frames 300 -gop 10 -rc cbr -bitrate 2000K -fps 25 -o test_2M.h264 > log_2m.txt
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 10 -rc_mode CBR -b:v 2M -r 25 -vframes 300 -y test_2M_ffmpeg.h264 > log_test_2M_ffmpeg.log
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 10 -rc_mode CBR -b:v 4M -r 25 -vframes 300 -y test_4M_ffmpeg.h264 > log_test_4M_ffmpeg.log
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 10 -rc_mode CBR -b:v 8M -r 25 -vframes 300 -y test_8M_ffmpeg.h264 > log_test_8M_ffmpeg.log
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 10 -rc_mode VBR -b:v 4M -maxrate 20M -r 25 -vframes 300 -y test_V4M_ffmpeg.h264 > log_test_V2M_ffmpeg.log
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 10 -rc_mode VBR -b:v 8M -maxrate 20M -r 25 -vframes 300 -y test_V8M_ffmpeg.h264 > log_test_V4M_ffmpeg.log
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 10 -rc_mode VBR -b:v 10M -maxrate 20M -r 25 -vframes 300 -y test_V10M_ffmpeg.h264 > log_test_V8M_ffmpeg.log
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -profile:v high -g 10 -rc_mode CQP -constqp 14 -r 25 -vframes 300 -y test_high_ffmpeg.h264 > log_test_high_ffmpeg.log

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/720p_NV12.yuv -codec h264 -src_fmt nv12 -s 1280x720 -frames 150 -o test_150.h264 > log.txt
# FIXME
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v h264_mtenc -g 250 -rc_mode CQP -constqp 26 -r 25 -vframes 150 -y test_150_ffmpeg.h264 > log_test_150_ffmpeg.log

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/in_720p_rgba.yuv -codec h264 -src_fmt rgba -r 1280x720 -s 960x720 -frames 150 -o test_vpp.h264  > log_vpp_venc.txt
# FIXME: -vf ?
#./ffmpeg -v trace -f rawvideo -pix_fmt rgba -s:v 1280x720 -i ${yuv_path}/in_720p_rgba.yuv -c:v h264_mtenc -rc_mode CQP -constqp 20 -g 10 -bf 0 -vframes 1 -y out.h264

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/1920x1080_nv12.yuv -codec h264 -src_fmt nv12 -s 1920x1080 -frames 300 -gop 10 -rc cqp -constqp 14 -fps 25 -o test_1080p.h264 > log_1080p.txt
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1920x1080 -i ${yuv_path}/1920x1080_nv12.yuv -c:v h264_mtenc -rc_mode CQP -constqp 14 -g 10 -r 25 -bf 0 -vframes 300 -y test_1080p_ffmpeg.h264 > log_1080p_ffmpeg.txt

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/1920x1080_bgra.yuv -codec h264 -src_fmt bgra -s 1920x1080 -frames 300 -gop 10 -rc cqp -constqp 14 -fps 25 -o test_1080p_bgra.h264 > log_1080p_bgra.txt
./ffmpeg -v trace -f rawvideo -pix_fmt bgra -s:v 1920x1080 -i ${yuv_path}/1920x1080_bgra.yuv -c:v h264_mtenc -rc_mode CQP -constqp 14 -g 10 -r 25 -vframes 300 -y test_1080p_bgra_ffmpeg.h264 > log_1080p_bgra_ffmpeg.txt

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/bbb_2160p_10s_nv12.yuv -codec h264 -src_fmt nv12 -s 3840x2160 -frames 300 -gop 10 -rc cqp -constqp 14 -fps 25 -o test_4k.h264 > log_4k.txt
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 3840x2160 -i ${yuv_path}/bbb_2160p_10s_nv12.yuv -c:v h264_mtenc -rc_mode CQP -constqp 14 -g 10 -r 25 -bf 0 -vframes 300 -y test_4k_ffmpeg.h264 > log_4k_ffmpeg.txt

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/bbb_2160p_10s_bgra.yuv -codec h264 -src_fmt bgra -s 3840x2160 -frames 300 -gop 10 -rc cqp -constqp 14 -fps 25 -o test_4k_bgra.h264 > log_4k_bgra.txt
./ffmpeg -v trace -f rawvideo -pix_fmt bgra -s:v 3840x2160 -i ${yuv_path}/bbb_2160p_10s_bgra.yuv -c:v h264_mtenc -rc_mode CQP -constqp 14 -g 10 -r 25 -vframes 300 -y test_4k_bgra_ffmpeg.h264 > log_4k_bgra_ffmpeg.txt

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/bbb_2160p_10s_rgba.yuv -codec h264 -src_fmt rgba -s 3840x2160 -frames 300 -gop 10 -rc cqp -constqp 14 -fps 25 -o test_4k_rgba.h264 > log_4k_rgba.txt
./ffmpeg -v trace -f rawvideo -pix_fmt rgba -s:v 3840x2160 -i ${yuv_path}/bbb_2160p_10s_rgba.yuv -c:v h264_mtenc -rc_mode CQP -constqp 14 -g 10 -r 25 -vframes 300 -y test_4k_rgba_ffmpeg.h264 > log_4k_rgba_ffmpeg.txt


export LIBVA_TRACE=vaapi_mtenc_h265_log
export FFREPORT=file=ffmpeg_mtenc_h265.log:level=56
###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/720p_NV12.yuv -codec hevc -src_fmt nv12 -s 1280x720 -frames 300 -gop 10 -rc cqp -constqp 14 -o test.h265 > log_hevc.txt
./ffmpeg -v trace -f rawvideo -pix_fmt nv12 -s:v 1280x720 -i ${yuv_path}/720p_NV12.yuv -c:v hevc_mtenc -rc_mode CQP -constqp 14 -g 10 -r 25 -vframes 300 -y test_ffmpeg.h265 > log_test_ffmpeg_h265.log

###MTENCODE_LEVEL=1 ./AppEncGL -i ${yuv_path}/1920x1080_p010le.yuv -s 1920x1080 -codec hevc -src_fmt p010le -bf 0 -rc cqp -constqp 20 -fps 30 -gop 30 -frames 900 -o test_hevc10bit.h265 > log_p010.txt
# FIXME
./ffmpeg -v trace -f rawvideo -pix_fmt p010le -s:v 1920x1080 -i ${yuv_path}/1920x1080_p010le.yuv -c:v hevc_mtenc -bf 0 -rc_mode CQP -constqp 20 -g 30 -r 25 -vframes 900 -y test_hevc10bit_ffmpeg.h265 > log_p010_ffmpeg.txt



rm test264.yuv
ffmpeg -hide_banner -i test_ffmpeg.h264 -pix_fmt nv12 test264.yuv -y
ffmpeg -hide_banner -s 1280*720 -pix_fmt nv12 -i ${yuv_path}/720p_NV12.yuv  -s 1280*720 -pix_fmt nv12 -i test264.yuv  -lavfi psnr=shortest=1 -f null -
rm test264.yuv

rm test265.yuv
ffmpeg -hide_banner -i test_ffmpeg.h265 -pix_fmt nv12 test265.yuv -y
ffmpeg -hide_banner -s 1280*720 -pix_fmt nv12 -i ${yuv_path}/720p_NV12.yuv  -s 1280*720 -pix_fmt nv12 -i test265.yuv  -lavfi psnr=shortest=1 -f null -
rm test265.yuv
