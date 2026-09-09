# MageBox 视频播放与转码说明

本文只记录 MageBox 当前的视频播放链路、支持范围和转码方法。播放器运行在 ESP32-S3 + 240×240 LCD 上，目标是从 TF 卡读取本地视频并逐帧显示。

## 1. 支持范围

| 项目 | 当前要求 |
| --- | --- |
| 文件目录 | TF 卡 `/video` |
| 文件扩展名 | `.avi`，大小写不敏感 |
| 容器 | RIFF AVI |
| 视频编码 | MJPEG（AVI 视频流标记为 `MJPG`） |
| 分辨率 | 必须是 `240×240` |
| 帧率 | 播放器接受约 `1–100 fps`；推荐 `30 fps` |
| 音频 | 可无音频；若带音频，只支持 16-bit PCM、1/2 声道、8–96 kHz |
| 单帧 JPEG | 不超过 4 MiB |

视频流必须由 AVI 的 `##dc` 或 `##db` chunk 组成。H.264/H.265、MP4/MKV/WebM、非 240×240 视频，以及 AAC/MP3 音频不会被当前播放器接受。

音频是可选的，不影响“只播放视频”的文件制作。若希望最小文件和最简单的兼容性，使用无音频命令并加上 `-an`。

## 2. 转码前准备

电脑需要安装 FFmpeg，并确认两个命令可用：

```bash
ffmpeg -version
ffprobe -version
```

先查看源文件：

```bash
ffprobe -v error \
  -show_entries stream=index,codec_type,codec_name,width,height,r_frame_rate,sample_rate,channels,bits_per_sample \
  -show_entries format=duration,size \
  -of default=noprint_wrappers=1 input.mp4
```

## 3. 推荐转码命令

### 3.1 只保留视频（推荐）

下面的命令会把任意常见输入缩放并补黑边到 240×240，转换为 30 fps 的 MJPEG AVI，并移除音频：

```bash
ffmpeg -i input.mp4 \
  -vf "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black,setsar=1" \
  -r 30 -c:v mjpeg -q:v 5 -pix_fmt yuvj420p \
  -an -f avi output.avi
```

参数说明：

- `scale` + `pad`：保持原始比例，输出尺寸固定为 240×240。
- `-r 30`：固定为 30 fps，兼顾流畅度、SD 卡读取压力和文件大小。
- `-c:v mjpeg`：生成播放器可以逐帧交给 JPEGDEC 解码的 MJPEG 视频。
- `-q:v 5`：画质/体积折中；数字越小画质越高、文件越大，可按需调整到 3–8。
- `-an`：不写入音频流，得到纯视频文件。

只转换源文件的一段时，加上例如 `-ss 00:01:00 -t 10`：

```bash
ffmpeg -ss 00:01:00 -i input.mp4 -t 10 \
  -vf "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black,setsar=1" \
  -r 30 -c:v mjpeg -q:v 5 -pix_fmt yuvj420p \
  -an -f avi output_10s.avi
```

### 3.2 保留兼容的 PCM 音频（可选）

如果需要声音，必须将音频转成 16-bit little-endian PCM。下面使用 44.1 kHz、双声道：

```bash
ffmpeg -i input.mp4 \
  -vf "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black,setsar=1" \
  -r 30 -c:v mjpeg -q:v 5 -pix_fmt yuvj420p \
  -c:a pcm_s16le -ar 44100 -ac 2 -sample_fmt s16 \
  -f avi output_pcm.avi
```

也可以使用单声道（`-ac 1`）或其他 8–96 kHz 采样率。不要直接保留 AAC、MP3、Opus 等压缩音频。

## 4. 转码后的检查

转码完成后，先在电脑上检查容器、视频和音频参数：

```bash
ffprobe -v error \
  -show_entries stream=index,codec_type,codec_name,codec_tag_string,width,height,r_frame_rate,sample_rate,channels,bits_per_sample \
  -show_entries format=duration,size \
  -of default=noprint_wrappers=1 output.avi
```

纯视频文件至少应看到：

```text
codec_type=video
codec_name=mjpeg
codec_tag_string=MJPG
width=240
height=240
r_frame_rate=30/1
```

带声音的文件还应看到：

```text
codec_type=audio
codec_name=pcm_s16le
sample_rate=44100
channels=2
bits_per_sample=16
```

建议先用桌面播放器播放输出文件，确认画面没有旋转、拉伸或明显损坏，再复制到 TF 卡。

## 5. TF 卡到设备的播放流程

1. 在 TF 卡根目录创建 `/video`。
2. 将 `output.avi` 复制到 `/video`。
3. 将 TF 卡插入设备并启动固件。
4. 打开桌面上的 **Video** 应用；应用会扫描 `/video` 下的 AVI 文件并显示探测到的分辨率、帧率和音频信息。
5. 点击文件开始播放。播放页支持播放/暂停、返回列表和退出应用；带 PCM 音频时，画面会跟随音频时钟同步。

设备端的实际调用顺序是：扫描文件 → 解析 AVI header → 检查 MJPEG/240×240/帧率/音频约束 → 从 `movi` 中读取视频 chunk → JPEG 解码到 PSRAM 帧缓冲 → 刷新 LCD。屏幕熄灭时会暂停视频和音频，恢复后继续播放。

## 6. 常见问题

| 现象 | 原因与处理 |
| --- | --- |
| 列表显示 `Unsupported AVI` | 容器不是 AVI，或视频不是 MJPEG；重新使用 `-c:v mjpeg -f avi`。 |
| 播放提示 `Video must be 240x240` | 输出尺寸不对；检查 FFmpeg 的 `scale` 和 `pad`，并用 `ffprobe` 确认宽高。 |
| 播放提示 `Invalid frame rate` | 时间基/帧率异常或低于 1 fps、高于 100 fps；重新固定为 `-r 30`。 |
| 播放提示 `Unsupported audio format` | 音频不是 16-bit PCM，或声道/采样率超范围；使用 `-c:a pcm_s16le -ar 44100 -ac 2`，或者直接加 `-an`。 |
| 有画面但没有声音 | 确认文件是 PCM 音频，并检查设备音量、功放和扬声器连接。 |
| 播放卡顿 | 降低 `-q:v` 的画质需求（例如改为 7），缩短视频，或使用速度更快的 TF 卡。 |
| 文件能在电脑播放、设备不能播放 | 电脑播放器支持的格式更广；以本文件“支持范围”和 `ffprobe` 输出为准重新转码。 |

## 7. 项目内测试素材

仓库中的 [`test_media/video`](../test_media/video/) 已包含符合上述规则的样例，包括无音频 MJPEG AVI 和 PCM 音频测试文件。复制这些文件到 TF 卡 `/video` 后即可验证列表、播放、暂停、结束和音频链路。

