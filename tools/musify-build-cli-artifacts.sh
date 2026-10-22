#!/bin/sh
# musify: reproducible CLI artifact build for the MUSA/App migration path.
# Keeps the upstream CUDA source tree intact while ensuring application binaries
# are requested explicitly instead of relying on the previous library-only
# configure invocation.
set -eu

cd "$(dirname "$0")/.."

CONFIGURE_FLAGS="
  --disable-doc
  --disable-everything
  --enable-shared
  --enable-avutil
  --enable-avcodec
  --enable-avformat
  --enable-avfilter
  --enable-swscale
  --enable-swresample
  --enable-ffmpeg
  --enable-ffprobe
  --enable-ffplay
  --enable-filter=testsrc2
  --enable-filter=hwupload
  --enable-filter=hwupload_musa
  --enable-filter=hwdownload
  --enable-filter=hwmap
  --enable-filter=scale
  --enable-filter=scale_musa
  --enable-filter=overlay_musa
  --enable-filter=pad_musa
  --enable-filter=bilateral_musa
  --enable-filter=chromakey_musa
  --enable-filter=colorspace_musa
  --enable-filter=thumbnail_musa
  --enable-filter=yadif_musa
  --enable-filter=bwdif_musa
  --enable-filter=split
  --enable-filter=mandelbrot
  --enable-protocol=file
  --enable-indev=lavfi
  --enable-demuxer=rawvideo
  --enable-demuxer=h264
  --enable-demuxer=hevc
  --enable-demuxer=ivf
  --enable-demuxer=matroska
  --enable-demuxer=mov
  --enable-demuxer=mpegts
  --enable-bsf=h264_mp4toannexb
  --enable-bsf=hevc_mp4toannexb
  --enable-parser=h264
  --enable-parser=hevc
  --enable-parser=vp9
  --enable-parser=av1
  --enable-muxer=null
  --enable-muxer=h264
  --enable-muxer=hevc
  --enable-muxer=ivf
  --enable-muxer=rawvideo
  --enable-encoder=wrapped_avframe
  --enable-encoder=rawvideo
  --enable-decoder=wrapped_avframe
  --enable-decoder=rawvideo
  --enable-decoder=h264
  --enable-decoder=hevc
  --enable-ffmtcodec
  --enable-mtenc
  --enable-muvid
  --enable-mtdec
  --disable-libnpp
  --enable-nonfree
  --extra-cflags=-I/usr/local/musa/include
  --extra-cflags=-DUSE_MUSA=1
  --extra-ldflags=-L/usr/local/musa/lib
  --enable-encoder=h264_mtenc
  --enable-encoder=hevc_mtenc
  --enable-encoder=av1_mtenc
  --enable-decoder=h264_muvid
  --enable-decoder=hevc_muvid
  --enable-decoder=vp9_muvid
  --enable-decoder=av1_muvid
  --enable-hwaccel=h264_mtdec
  --enable-hwaccel=hevc_mtdec
  --enable-hwaccel=vp9_mtdec
  --enable-hwaccel=av1_mtdec
  --enable-muxer=mp4
  --enable-muxer=mov
  --disable-x86asm
"

# ffplay is a real FFmpeg program, not a stub: upstream requires SDL2.
# Expose the root cause before configure silently disables ffplay when SDL2 is
# absent, so verifier/director can distinguish dependency absence from artifact
# emission regressions.
if ! pkg-config --exists sdl2; then
    printf '%s
'         'musify: ffplay cannot be emitted: SDL2 development package is missing (pkg-config sdl2 not found).'         'musify: ffmpeg and ffprobe can still be built with this script; install SDL2 to include ffplay.' >&2
fi

# shellcheck disable=SC2086
./configure $CONFIGURE_FLAGS
make -j"${JOBS:-2}" ffmpeg ffprobe ${MUSIFY_REQUIRE_FFPLAY:+ffplay}

missing=0
for artifact in ffmpeg ffprobe; do
    if [ ! -x "./$artifact" ]; then
        printf 'musify: expected CLI artifact missing: %s
' "$artifact" >&2
        missing=1
    fi
done

if pkg-config --exists sdl2; then
    make -j"${JOBS:-2}" ffplay
    if [ ! -x ./ffplay ]; then
        printf '%s
' 'musify: expected CLI artifact missing: ffplay (SDL2 was detected)' >&2
        missing=1
    fi
else
    printf '%s
' 'musify: ffplay skipped because SDL2 is unavailable; this is a dependency root cause, not a stub.' >&2
fi

exit "$missing"
