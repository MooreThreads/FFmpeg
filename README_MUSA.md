# MUSA 平台 FFmpeg 支持与验证技术报告

## 1. 总体结论

当前 FFmpeg 已具备 MUSA / MT video 基础能力，覆盖以下功能范围：

- MUSA hardware device / hwaccel backend；
- MUSA hardware pixel format；
- MUVID 硬件解码器：H.264 / HEVC / VP9 / AV1；
- MTDEC hwaccel selectors：H.264 / HEVC / VP9 / AV1；
- MTENC 硬件编码器：H.264 / HEVC / AV1；
- MUSA 专用 filters：`hwupload_musa`、`scale_musa`、`overlay_musa`、`pad_musa`、`bilateral_musa`、`chromakey_musa`、`colorspace_musa`、`thumbnail_musa`、`yadif_musa`、`bwdif_musa`。

本地验证结果表明，当前分支的构建、基础能力查询、MTENC 编码、MUVID 解码、MTDEC 硬件加速解码以及 MUSA filter 主路径均已通过验证。

## 2. 构建方式与依赖

MUSA 功能依赖 MUSA SDK、`ffmtcodec` 以及 MUSA compiler `mcc`。其中，MUSA SDK 提供运行时、驱动接口、头文件与库；`ffmtcodec` 提供 MTENC / MUVID / MTDEC 所需的 codec wrapper 头文件；`mcc` 用于编译 MUSA filter kernel。

> **Driver 版本要求：MUSA driver 版本必须不低于 5.3。**


构建前需安装 `ffmtcodec` codec headers。参考如下：

```bash
git clone https://github.com/MooreThreads/mt-codec-headers
cd mt-codec-headers && make && sudo make install && cd -
```

完成 `ffmtcodec` 安装并确认 MUSA SDK 路径可用后，普通构建示例如下：

```bash
./configure \
  --disable-libnpp \
  --enable-nonfree \
  --extra-cflags=-I/usr/local/musa/include \
  --extra-ldflags=-L/usr/local/musa/lib

make -j$(nproc) ffmpeg ffprobe
```

普通完整构建中，MUSA 相关能力采用自动探测、按需禁用的配置策略；在 SDK 与依赖可被正确探测的情况下，无需手动列出全部 codec、filter 和封装组件。如需关闭对应的 MUSA codec/backend 能力，可使用以下 configure 选项：

```text
--disable-ffmtcodec
--disable-mtenc
--disable-muvid
--disable-mtdec
--disable-musa-mcc
```

建议使用仓库内构建脚本生成最小命令行验证产物：

```bash
./tools/musify-build-cli-artifacts.sh
```

该构建脚本采用最小化配置，仅生成验证所需的命令行程序和相关组件。

MUSA `.mu` kernel 由 `mcc` 编译。可通过 `--mcc` 和 `--mccflags` 指定编译器及其参数；默认 `MCCFLAGS` 不固定具体 `--offload-arch`。如需指定目标架构，应通过 `--mccflags` 显式传入对应 `--offload-arch`，例如：

```bash
--mccflags='-mtgpu --offload-arch=mp_22 --musa-device-only -O2 -I$(SRC_PATH) -I$(SRC_PATH)/libavfilter -I/usr/local/musa/include'
```

## 3. 当前支持能力

### 3.1 编码能力

| Codec | Encoder | 1080p NV12 | 10bit/P010 | 4K NV12 | 8K NV12 |
|---|---|---:|---:|---:|---:|
| H.264 | `h264_mtenc` | PASS | PASS | PASS | PASS |
| HEVC | `hevc_mtenc` | PASS | PASS | PASS | PASS |
| AV1 | `av1_mtenc` | PASS | PASS | PASS | PASS |

### 3.2 解码能力

| Codec | MUVID decoder | 8bit | 10bit | 当前状态 |
|---|---|---:|---:|---|
| H.264 | `h264_muvid` | PASS | PASS | PASS |
| HEVC | `hevc_muvid` | PASS | PASS | PASS |
| VP9 | `vp9_muvid` | PASS | PASS | PASS |
| AV1 | `av1_muvid` | PASS | PASS | PASS |

### 3.3 MTDEC hwaccel

| Codec | MTDEC selector | 结果 |
|---|---|---:|
| H.264 | `h264_mtdec` | PASS |
| HEVC | `hevc_mtdec` | PASS |
| VP9 | `vp9_mtdec` | PASS |
| AV1 | `av1_mtdec` | PASS |

当前 `-hwaccel musa` / `-hwaccel_output_format musa` 基础路径已覆盖 H.264、HEVC、VP9 和 AV1。

### 3.4 MUSA filters

| Filter | 结果 |
|---|---:|
| `hwupload_musa` | PASS |
| `scale_musa` | PASS |
| `overlay_musa` | PASS |
| `pad_musa` | PASS |
| `bilateral_musa` | PASS |
| `chromakey_musa` | PASS |
| `colorspace_musa` | PASS |
| `thumbnail_musa` | PASS |
| `yadif_musa` | PASS |
| `bwdif_musa` | PASS |

使用说明：`colorspace_musa` 需使用当前实现支持的参数，例如 `range=pc`；`chromakey_musa` 输出包含 alpha 分量，回下载格式应使用 `yuva420p`。

## 4. 功能建议命令

以下命令用于说明各类 MUSA 功能的推荐调用方式。命令中的输入文件、分辨率、码率和输出文件名可根据实际测试素材调整；验证当前分支能力时应使用当前源码树构建生成的本地 `./ffmpeg`。

### 4.1 能力查询

```bash
./ffmpeg -hide_banner -hwaccels
./ffmpeg -hide_banner -pix_fmts | grep -Ei '(^|[[:space:]])musa($|[[:space:]])'
./ffmpeg -hide_banner -encoders | grep -Ei 'mtenc'
./ffmpeg -hide_banner -decoders | grep -Ei 'muvid'
```

### 4.2 MTENC 编码

NV12 输入编码为 H.264：

```bash
./ffmpeg -y -nostdin \
  -f rawvideo -pix_fmt nv12 -s 1920x1080 -r 30 -i input_1080p_nv12.yuv \
  -c:v h264_mtenc -b:v 8M input.h264
```

P010 / 10-bit 输入编码为 HEVC：

```bash
./ffmpeg -y -nostdin \
  -f rawvideo -pix_fmt p010le -s 1920x1080 -r 30 -i input_1080p_p010.yuv \
  -c:v hevc_mtenc -b:v 8M input.hevc
```

NV12 输入编码为 AV1：

```bash
./ffmpeg -y -nostdin \
  -f rawvideo -pix_fmt nv12 -s 1920x1080 -r 30 -i input_1080p_nv12.yuv \
  -c:v av1_mtenc -b:v 8M input_av1.ivf
```

### 4.3 MUVID 解码

使用 MUVID decoder 解码压缩码流：

```bash
./ffmpeg -y -nostdin -c:v h264_muvid -i input.h264     -f null -
./ffmpeg -y -nostdin -c:v hevc_muvid -i input.hevc     -f null -
./ffmpeg -y -nostdin -c:v vp9_muvid  -i input_vp9.ivf -f null -
./ffmpeg -y -nostdin -c:v av1_muvid  -i input_av1.ivf -f null -
```

如需回下载为 CPU 侧 NV12 rawvideo，可使用：

```bash
./ffmpeg -y -nostdin -c:v h264_muvid -i input.h264 \
  -pix_fmt nv12 -f rawvideo output_nv12.yuv
```

### 4.4 MTDEC 硬件加速解码

使用 MUSA hwaccel 路径解码并保持硬件帧输出：

```bash
./ffmpeg -y -nostdin \
  -hwaccel musa -hwaccel_output_format musa \
  -i input.mp4 -f null -
```

如需将硬件帧回下载为 CPU 侧 NV12，可使用：

```bash
./ffmpeg -y -nostdin \
  -hwaccel musa -hwaccel_output_format musa \
  -i input.mp4 -vf hwdownload,format=nv12 -f rawvideo output_nv12.yuv
```

### 4.5 硬件转码

压缩码流经 MUSA hwaccel 解码后使用 MTENC 编码输出：

```bash
./ffmpeg -y -nostdin \
  -hwaccel musa -hwaccel_output_format musa \
  -i input.mp4 \
  -c:v h264_mtenc -b:v 8M -c:a copy output.mp4
```

### 4.6 MUSA filter

软件帧上传至 MUSA 后执行缩放：

```bash
./ffmpeg -y -nostdin \
  -f lavfi -i testsrc2=size=1920x1080:rate=30 \
  -vf format=nv12,hwupload_musa,scale_musa=1280:720,hwdownload,format=nv12 \
  -frames:v 60 -f null -
```

MUSA filter 与 MTENC 组合输出：

```bash
./ffmpeg -y -nostdin \
  -f lavfi -i testsrc2=size=1920x1080:rate=30 \
  -vf format=nv12,hwupload_musa,scale_musa=1280:720 \
  -frames:v 60 -c:v h264_mtenc -b:v 4M output.mp4
```

`colorspace_musa` 示例：

```bash
./ffmpeg -y -nostdin \
  -f lavfi -i testsrc2=size=1280x720:rate=30 \
  -vf format=nv12,hwupload_musa,colorspace_musa=range=pc,hwdownload,format=nv12 \
  -frames:v 60 -f null -
```

`chromakey_musa` 示例：

```bash
./ffmpeg -y -nostdin \
  -f lavfi -i testsrc2=size=1280x720:rate=30 \
  -vf format=nv12,hwupload_musa,chromakey_musa=0x00ff00:0.1:0.2,hwdownload,format=yuva420p \
  -frames:v 60 -f null -
```

## 5. Conformance 与专项验证

### 5.1 `ffmpeg-musa/decode`

`ffmpeg-musa/decode/` 使用当前源码树本地 `ffmpeg` 运行，结果如下：

```text
1263 successful, 9 skipped, 0 failed, 0 erroneous
```

结论：显式 MUSA hwaccel decode 主路径验证通过。

### 5.2 `ffmpeg-muvid/decode`

`ffmpeg-muvid/decode/` 使用当前源码树本地 `ffmpeg` 运行，结果如下：

```text
1259 successful, 9 skipped, 4 failed, 0 erroneous
```

4 个失败项当前按已确认范围允许跳过：

- 2 个 AV1 SVC/scalable-layer 子集；
- 2 个 AVC Soccer legacy baseline 差异项。

结论：剔除已确认跳过项后，MUVID decode 主路径验证通过。

### 5.3 MTENC vaapi-fits

AVC encode 可执行子集验证结果如下：

```text
47 collected, 30 successful, 0 failed, 0 erroneous, 1 skipped, 16 not run
```

结论：当前可执行 AVC MTENC encode 子集验证通过。

## 6. 已知限制

当前版本存在以下已知限制：

1. RGBA/BGRA direct MTENC input 当前不作为正向支持项；建议使用 NV12 或 P010/YUV420_10BIT。
2. MTENC 部分高级参数当前 runtime 不支持或尚未作为正向能力确认，包括 `rc-lookahead`、AQ、temporal filtering、forced split encode 等。
3. 当前暂不支持的 MUSA MTDEC 额外格式未纳入编译范围，包括 MJPEG、MPEG1/2、MPEG4、VC1、VP8 等。

## 7. 阶段性结论

当前已完成 MUSA / MT video 基础功能接入和本地验证：

```text
构建：PASS
MUSA backend/pixfmt 可见性：PASS
MTENC encode：PASS
MUVID decode：PASS
MTDEC hwaccel decode：PASS
MUSA filters：PASS
```

