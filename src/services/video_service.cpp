#include "video_service.h"

#include "board_config.h"
#include "services/audio_service.h"
#include "services/jpeg_service.h"
#include "services/power_service.h"
#include "services/sd_service.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr uint32_t frame_bytes = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t);
constexpr uint32_t max_chunk_size = 4U * 1024U * 1024U;
constexpr uint32_t audio_prebuffer_ms = 250;
constexpr uint32_t audio_refill_ms = 300;
constexpr uint16_t background = 0x0000;

constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) |
           ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

struct AviMetadata {
    uint32_t frame_period_us = 0;
    uint32_t frame_count = 0;
    uint32_t movi_start = 0;
    uint32_t movi_end = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t video_stream = 0;
    uint8_t audio_stream = 0;
    bool has_video = false;
    bool has_audio = false;
    uint16_t audio_format = 0;
    uint16_t audio_channels = 0;
    uint32_t audio_sample_rate = 0;
    uint16_t audio_block_align = 0;
    uint16_t audio_bits = 0;
    uint32_t audio_avg_bytes = 0;
};

struct FrameReader {
    File *file = nullptr;
    uint32_t start = 0;
    uint32_t size = 0;
    uint32_t position = 0;
};

File s_file;
File s_audio_file;
AviMetadata s_meta;
FrameReader s_reader;
uint16_t *s_frame = nullptr;
VideoPlayState s_state = VideoPlayState::idle;
uint32_t s_video_scan_pos = 0;
uint32_t s_audio_scan_pos = 0;
uint32_t s_frame_index = 0;
uint32_t s_next_due_us = 0;
bool s_frame_ready = false;
bool s_screen_suspended = false;
uint32_t s_decoded_frames = 0;
uint32_t s_dropped_frames = 0;
uint64_t s_decode_time_us = 0;
uint32_t s_play_started_ms = 0;
bool s_stats_logged = false;
bool s_audio_active = false;
bool s_audio_eof = false;
uint32_t s_last_buffer_warning_ms = 0;
char s_path[sd_svc_name_max] = {};
char s_error[64] = {};

void set_error(const char *message)
{
    strncpy(s_error, message ? message : "Video error", sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = 0;
    s_state = VideoPlayState::error;
    Serial.printf("Video: %s\n", s_error);
}

bool seek_to(uint32_t position)
{
    return s_file && position <= s_file.size() && s_file.seek(position);
}

bool read_bytes(void *output, size_t size)
{
    return s_file && s_file.read((uint8_t *)output, size) == size;
}

bool read_u32(uint32_t &value)
{
    uint8_t bytes[4];
    if (!read_bytes(bytes, sizeof(bytes))) return false;
    value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
            ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

bool read_audio_u32(uint32_t &value)
{
    uint8_t bytes[4];
    if (!s_audio_file || s_audio_file.read(bytes, sizeof(bytes)) != sizeof(bytes)) return false;
    value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
            ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

uint32_t aligned_size(uint32_t size)
{
    return size + (size & 1U);
}

bool valid_range(uint32_t start, uint32_t size, uint32_t limit)
{
    return start <= limit && size <= limit - start;
}

bool parse_stream_list(uint32_t start, uint32_t end, uint8_t stream_index, AviMetadata &meta)
{
    uint32_t type = 0;
    uint32_t handler = 0;
    uint32_t scale = 0;
    uint32_t rate = 0;
    uint32_t length = 0;
    uint32_t compression = 0;
    int32_t width = 0;
    int32_t height = 0;

    for (uint32_t pos = start; pos + 8 <= end;) {
        if (!seek_to(pos)) return false;
        uint32_t id = 0;
        uint32_t size = 0;
        if (!read_u32(id) || !read_u32(size)) return false;
        const uint32_t data = pos + 8;
        if (!valid_range(data, size, end)) return false;

        if (id == fourcc('s', 't', 'r', 'h') && size >= 36) {
            uint8_t header[36];
            if (!read_bytes(header, sizeof(header))) return false;
            auto le32 = [](const uint8_t *p) {
                return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            };
            type = le32(header);
            handler = le32(header + 4);
            scale = le32(header + 20);
            rate = le32(header + 24);
            length = le32(header + 32);
        } else if (id == fourcc('s', 't', 'r', 'f') && size >= 16) {
            uint8_t format[20];
            const size_t format_size = size >= sizeof(format) ? sizeof(format) : 16;
            if (!read_bytes(format, format_size)) return false;
            auto le32 = [](const uint8_t *p) {
                return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            };
            if (type == fourcc('v', 'i', 'd', 's')) {
                width = (int32_t)le32(format + 4);
                height = (int32_t)le32(format + 8);
                if (size >= 20) compression = le32(format + 16);
            } else if (type == fourcc('a', 'u', 'd', 's')) {
                meta.audio_format = (uint16_t)(format[0] | ((uint16_t)format[1] << 8));
                meta.audio_channels = (uint16_t)(format[2] | ((uint16_t)format[3] << 8));
                meta.audio_sample_rate = le32(format + 4);
                meta.audio_avg_bytes = le32(format + 8);
                meta.audio_block_align = (uint16_t)(format[12] | ((uint16_t)format[13] << 8));
                meta.audio_bits = (uint16_t)(format[14] | ((uint16_t)format[15] << 8));
            }
        }
        pos = data + aligned_size(size);
    }

    if (type == fourcc('a', 'u', 'd', 's')) {
        meta.has_audio = true;
        meta.audio_stream = stream_index;
        return true;
    }
    if (type != fourcc('v', 'i', 'd', 's') || meta.has_video) return true;
    if (handler != fourcc('M', 'J', 'P', 'G') && compression != fourcc('M', 'J', 'P', 'G')) return true;
    if (width <= 0 || height == 0) return true;

    meta.has_video = true;
    meta.video_stream = stream_index;
    meta.width = (uint16_t)width;
    meta.height = (uint16_t)(height < 0 ? -height : height);
    if (scale && rate) meta.frame_period_us = (uint32_t)((uint64_t)scale * 1000000ULL / rate);
    if (length) meta.frame_count = length;
    return true;
}

bool parse_header(AviMetadata &meta)
{
    const uint32_t file_size = s_file.size();
    if (file_size < 12 || !seek_to(0)) return false;
    uint32_t riff = 0;
    uint32_t riff_size = 0;
    uint32_t avi = 0;
    if (!read_u32(riff) || !read_u32(riff_size) || !read_u32(avi) ||
        riff != fourcc('R', 'I', 'F', 'F') || avi != fourcc('A', 'V', 'I', ' ')) return false;

    const uint32_t riff_end = (riff_size <= file_size - 8) ? riff_size + 8 : file_size;
    uint32_t avih_period = 0;
    uint32_t avih_frames = 0;
    uint32_t avih_width = 0;
    uint32_t avih_height = 0;
    uint8_t stream_index = 0;

    for (uint32_t pos = 12; pos + 8 <= riff_end;) {
        if (!seek_to(pos)) return false;
        uint32_t id = 0;
        uint32_t size = 0;
        if (!read_u32(id) || !read_u32(size)) return false;
        const uint32_t data = pos + 8;
        if (!valid_range(data, size, riff_end)) return false;

        if (id == fourcc('L', 'I', 'S', 'T') && size >= 4) {
            uint32_t list_type = 0;
            if (!read_u32(list_type)) return false;
            if (list_type == fourcc('m', 'o', 'v', 'i')) {
                meta.movi_start = data + 4;
                meta.movi_end = data + size;
            } else if (list_type == fourcc('h', 'd', 'r', 'l')) {
                const uint32_t hdrl_end = data + size;
                for (uint32_t child = data + 4; child + 8 <= hdrl_end;) {
                    if (!seek_to(child)) return false;
                    uint32_t child_id = 0;
                    uint32_t child_size = 0;
                    if (!read_u32(child_id) || !read_u32(child_size)) return false;
                    const uint32_t child_data = child + 8;
                    if (!valid_range(child_data, child_size, hdrl_end)) return false;
                    if (child_id == fourcc('a', 'v', 'i', 'h') && child_size >= 40) {
                        uint8_t header[40];
                        if (!read_bytes(header, sizeof(header))) return false;
                        auto le32 = [](const uint8_t *p) {
                            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                        };
                        avih_period = le32(header);
                        avih_frames = le32(header + 16);
                        avih_width = le32(header + 32);
                        avih_height = le32(header + 36);
                    } else if (child_id == fourcc('L', 'I', 'S', 'T') && child_size >= 4) {
                        uint32_t child_type = 0;
                        if (!read_u32(child_type)) return false;
                        if (child_type == fourcc('s', 't', 'r', 'l')) {
                            if (!parse_stream_list(child_data + 4, child_data + child_size,
                                                   stream_index, meta)) return false;
                            stream_index++;
                        }
                    }
                    child = child_data + aligned_size(child_size);
                }
            }
        }
        pos = data + aligned_size(size);
    }

    if (!meta.frame_period_us) meta.frame_period_us = avih_period;
    if (!meta.frame_count) meta.frame_count = avih_frames;
    if (!meta.width) meta.width = (uint16_t)avih_width;
    if (!meta.height) meta.height = (uint16_t)avih_height;
    return meta.has_video && meta.movi_start && meta.movi_end > meta.movi_start;
}

bool is_video_chunk(uint32_t id)
{
    const uint8_t a = id & 0xFF;
    const uint8_t b = (id >> 8) & 0xFF;
    const uint8_t c = (id >> 16) & 0xFF;
    const uint8_t d = (id >> 24) & 0xFF;
    if (a < '0' || a > '9' || b < '0' || b > '9') return false;
    const uint8_t stream = (uint8_t)((a - '0') * 10 + (b - '0'));
    return stream == s_meta.video_stream && c == 'd' && (d == 'c' || d == 'b');
}

bool is_audio_chunk(uint32_t id)
{
    const uint8_t a = id & 0xFF;
    const uint8_t b = (id >> 8) & 0xFF;
    const uint8_t c = (id >> 16) & 0xFF;
    const uint8_t d = (id >> 24) & 0xFF;
    if (a < '0' || a > '9' || b < '0' || b > '9') return false;
    const uint8_t stream = (uint8_t)((a - '0') * 10 + (b - '0'));
    return stream == s_meta.audio_stream && c == 'w' && d == 'b';
}

enum class FrameResult : uint8_t { ready, wait, end, error };

FrameResult next_frame(uint32_t &start, uint32_t &size)
{
    while (s_video_scan_pos + 8 <= s_meta.movi_end) {
        if (!seek_to(s_video_scan_pos)) {
            Serial.printf("Video AVI seek failed at %lu\n", (unsigned long)s_video_scan_pos);
            return FrameResult::error;
        }
        uint32_t id = 0;
        uint32_t chunk_size = 0;
        if (!read_u32(id) || !read_u32(chunk_size)) {
            Serial.printf("Video AVI chunk header failed at %lu\n", (unsigned long)s_video_scan_pos);
            return FrameResult::error;
        }
        const uint32_t data = s_video_scan_pos + 8;
        if (!valid_range(data, chunk_size, s_meta.movi_end)) {
            Serial.printf("Video AVI chunk out of range at %lu size %lu end %lu\n",
                          (unsigned long)s_video_scan_pos, (unsigned long)chunk_size,
                          (unsigned long)s_meta.movi_end);
            return FrameResult::error;
        }
        if (id == fourcc('L', 'I', 'S', 'T') && chunk_size >= 4) {
            s_video_scan_pos = data + 4;
            continue;
        }
        if (is_video_chunk(id) && chunk_size > 0 && chunk_size <= max_chunk_size) {
            s_video_scan_pos = data + aligned_size(chunk_size);
            start = data;
            size = chunk_size;
            return FrameResult::ready;
        }
        s_video_scan_pos = data + aligned_size(chunk_size);
    }
    return FrameResult::end;
}

FrameResult refill_audio(uint32_t target_ms)
{
    if (!s_audio_active) return FrameResult::end;
    uint8_t buffer[1024];
    while (audio_svc_pcm_buffered_ms() < target_ms &&
           s_audio_scan_pos + 8 <= s_meta.movi_end) {
        if (!s_audio_file.seek(s_audio_scan_pos)) {
            Serial.printf("Video AVI audio seek failed at %lu\n", (unsigned long)s_audio_scan_pos);
            return FrameResult::error;
        }
        uint32_t id = 0;
        uint32_t chunk_size = 0;
        if (!read_audio_u32(id) || !read_audio_u32(chunk_size)) {
            Serial.printf("Video AVI audio header failed at %lu\n", (unsigned long)s_audio_scan_pos);
            return FrameResult::error;
        }
        const uint32_t data = s_audio_scan_pos + 8;
        if (!valid_range(data, chunk_size, s_meta.movi_end)) {
            Serial.printf("Video AVI audio chunk out of range at %lu\n",
                          (unsigned long)s_audio_scan_pos);
            return FrameResult::error;
        }
        if (id == fourcc('L', 'I', 'S', 'T') && chunk_size >= 4) {
            s_audio_scan_pos = data + 4;
            continue;
        }
        if (is_audio_chunk(id) && chunk_size > 0) {
            if (!audio_svc_pcm_can_accept(chunk_size)) return FrameResult::wait;
            uint32_t copied = 0;
            while (copied < chunk_size) {
                const uint32_t request = (chunk_size - copied) < sizeof(buffer) ?
                    chunk_size - copied : sizeof(buffer);
                if (!s_audio_file.seek(data + copied) ||
                    s_audio_file.read(buffer, request) != request ||
                    !audio_svc_pcm_enqueue(buffer, request)) {
                    Serial.printf("Video AVI audio read failed at %lu size %lu\n",
                                  (unsigned long)(data + copied), (unsigned long)request);
                    return FrameResult::error;
                }
                copied += request;
            }
        }
        s_audio_scan_pos = data + aligned_size(chunk_size);
    }
    return s_audio_scan_pos + 8 <= s_meta.movi_end ? FrameResult::ready : FrameResult::end;
}

void jpeg_close(void *) {}

int32_t jpeg_read(JPEGFILE *file, uint8_t *buffer, int32_t length)
{
    FrameReader *reader = file ? (FrameReader *)file->fHandle : nullptr;
    if (!reader || !reader->file || length <= 0) return 0;
    uint32_t remaining = reader->size - reader->position;
    uint32_t wanted = (uint32_t)length < remaining ? (uint32_t)length : remaining;
    if (!wanted || !reader->file->seek(reader->start + reader->position)) return 0;
    size_t read = reader->file->read(buffer, wanted);
    reader->position += (uint32_t)read;
    return (int32_t)read;
}

int32_t jpeg_seek(JPEGFILE *file, int32_t position)
{
    FrameReader *reader = file ? (FrameReader *)file->fHandle : nullptr;
    if (!reader || !reader->file || position < 0 || (uint32_t)position > reader->size) return 0;
    reader->position = (uint32_t)position;
    return reader->file->seek(reader->start + reader->position) ? position : 0;
}

int jpeg_draw(JPEGDRAW *draw)
{
    if (!s_frame || !draw || !draw->pPixels) return 0;
    const int stride = draw->iWidth > 0 ? draw->iWidth : draw->iWidthUsed;
    const int width = draw->iWidthUsed > 0 ? draw->iWidthUsed : draw->iWidth;
    if (stride <= 0 || width <= 0) return 0;
    for (int row = 0; row < draw->iHeight; ++row) {
        const int y = draw->y + row;
        if (y < 0 || y >= BOARD_LCD_V_RES || draw->x < 0 || draw->x + width > BOARD_LCD_H_RES) continue;
        memcpy(s_frame + y * BOARD_LCD_H_RES + draw->x,
               draw->pPixels + row * stride, (size_t)width * sizeof(uint16_t));
    }
    return 1;
}

bool decode_frame(uint32_t start, uint32_t size)
{
    JPEGDEC *decoder = jpeg_svc_acquire();
    if (!decoder) return false;
    s_reader = {&s_file, start, size, 0};
    bool ok = decoder->open(&s_reader, (int)size, jpeg_close, jpeg_read, jpeg_seek, jpeg_draw) != 0;
    if (ok) {
        ok = decoder->getWidth() == BOARD_LCD_H_RES && decoder->getHeight() == BOARD_LCD_V_RES;
        if (ok) {
            decoder->setPixelType(RGB565_LITTLE_ENDIAN);
            ok = decoder->decode(0, 0, 0) != 0;
        }
        decoder->close();
    }
    jpeg_svc_release(decoder);
    return ok;
}

void log_memory(const char *phase)
{
    Serial.printf("Video memory %s: internal=%u largest=%u psram=%u\n", phase,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void log_performance()
{
    if (s_stats_logged || !s_decoded_frames) return;
    s_stats_logged = true;
    const uint32_t elapsed = millis() - s_play_started_ms;
    const uint32_t average_decode = (uint32_t)(s_decode_time_us / s_decoded_frames);
    const float fps = elapsed ? (float)s_decoded_frames * 1000.0f / elapsed : 0.0f;
    Serial.printf("Video stats: decoded=%u dropped=%u fps=%.1f decode_avg=%u us\n",
                  (unsigned)s_decoded_frames, (unsigned)s_dropped_frames, fps,
                  (unsigned)average_decode);
}

void finish_playback(VideoPlayState state)
{
    if (s_file) s_file.close();
    if (s_audio_file) s_audio_file.close();
    if (s_audio_active) audio_svc_pcm_end();
    s_audio_active = false;
    s_state = state;
    log_performance();
}

void release_resources(bool keep_status)
{
    if (s_audio_active) audio_svc_pcm_end();
    s_audio_active = false;
    if (s_file) s_file.close();
    if (s_audio_file) s_audio_file.close();
    if (s_frame) {
        heap_caps_free(s_frame);
        s_frame = nullptr;
    }
    s_reader = {};
    s_frame_ready = false;
    s_screen_suspended = false;
    s_audio_eof = false;
    s_last_buffer_warning_ms = 0;
    if (!keep_status) {
        s_meta = {};
        s_frame_index = 0;
        s_decoded_frames = 0;
        s_dropped_frames = 0;
        s_decode_time_us = 0;
        s_play_started_ms = 0;
        s_stats_logged = false;
        s_state = VideoPlayState::idle;
        s_path[0] = 0;
        s_error[0] = 0;
    }
}

} // namespace

bool video_svc_probe(const char *path, VideoFileInfo *info)
{
    if (!path || !path[0] || !info || s_file || s_frame) return false;
    *info = {};
    s_file = SD_MMC.open(path, FILE_READ);
    if (!s_file) return false;
    AviMetadata metadata;
    const bool parsed = parse_header(metadata);
    s_file.close();
    if (!parsed) return false;

    info->width = metadata.width;
    info->height = metadata.height;
    info->fps = metadata.frame_period_us ?
        (uint16_t)((1000000U + metadata.frame_period_us / 2) / metadata.frame_period_us) : 0;
    info->duration_ms = metadata.frame_period_us ?
        (uint32_t)((uint64_t)metadata.frame_count * metadata.frame_period_us / 1000ULL) : 0;
    info->has_audio = metadata.has_audio;
    info->audio_supported = !metadata.has_audio ||
        (metadata.audio_format == 1 && metadata.audio_bits == 16 &&
         (metadata.audio_channels == 1 || metadata.audio_channels == 2) &&
         metadata.audio_sample_rate >= 8000 && metadata.audio_sample_rate <= 96000 &&
         metadata.audio_block_align != 0);
    info->audio_sample_rate = metadata.audio_sample_rate;
    info->audio_channels = (uint8_t)metadata.audio_channels;
    info->audio_bits = (uint8_t)metadata.audio_bits;
    strncpy(info->audio_codec, metadata.has_audio ?
            (info->audio_supported ? "PCM" : "Audio") : "None",
            sizeof(info->audio_codec) - 1);
    info->supported = info->audio_supported && metadata.width == BOARD_LCD_H_RES &&
                      metadata.height == BOARD_LCD_V_RES &&
                      metadata.frame_period_us >= 10000 && metadata.frame_period_us <= 1000000;
    return true;
}

bool video_svc_play(const char *path)
{
    if (!path || !path[0]) return false;
    audio_svc_stop();
    release_resources(false);
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = 0;
    s_state = VideoPlayState::loading;
    s_file = SD_MMC.open(path, FILE_READ);
    if (!s_file) {
        set_error("Can't open video");
        return false;
    }
    if (!parse_header(s_meta)) {
        release_resources(true);
        set_error("Unsupported AVI");
        return false;
    }
    if (s_meta.has_audio && (s_meta.audio_format != 1 || s_meta.audio_bits != 16 ||
        (s_meta.audio_channels != 1 && s_meta.audio_channels != 2) ||
        s_meta.audio_sample_rate < 8000 || s_meta.audio_sample_rate > 96000 ||
        !s_meta.audio_block_align)) {
        release_resources(true);
        set_error("Unsupported audio format");
        return false;
    }
    if (s_meta.width != BOARD_LCD_H_RES || s_meta.height != BOARD_LCD_V_RES) {
        release_resources(true);
        set_error("Video must be 240x240");
        return false;
    }
    if (s_meta.frame_period_us < 10000 || s_meta.frame_period_us > 1000000) {
        release_resources(true);
        set_error("Invalid frame rate");
        return false;
    }

    s_audio_active = s_meta.has_audio && audio_svc_pcm_begin(
        s_meta.audio_sample_rate, (uint8_t)s_meta.audio_bits,
        (uint8_t)s_meta.audio_channels);
    if (s_meta.has_audio && !s_audio_active) {
        release_resources(true);
        set_error("Audio output failed");
        return false;
    }
    if (s_audio_active) {
        s_audio_file = SD_MMC.open(path, FILE_READ);
        s_audio_scan_pos = s_meta.movi_start;
        const FrameResult prebuffer = s_audio_file ? refill_audio(audio_prebuffer_ms) :
                                                     FrameResult::error;
        s_audio_eof = prebuffer == FrameResult::end;
        if (prebuffer == FrameResult::error || !audio_svc_pcm_start()) {
            release_resources(true);
            set_error("Audio prebuffer failed");
            return false;
        }
        Serial.printf("PCM prebuffer: %u ms\n", (unsigned)audio_svc_pcm_buffered_ms());
    }

    log_memory("before");
    s_frame = (uint16_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frame) {
        release_resources(true);
        set_error("Not enough memory");
        return false;
    }
    for (uint32_t i = 0; i < BOARD_LCD_H_RES * BOARD_LCD_V_RES; ++i) s_frame[i] = background;
    s_video_scan_pos = s_meta.movi_start;
    s_frame_index = 0;
    s_next_due_us = micros();
    s_frame_ready = false;
    s_state = VideoPlayState::loading;
    s_play_started_ms = millis();
    s_stats_logged = false;
    log_memory("playing");
    Serial.printf("Video play: %s %ux%u %u frames %.2f fps\n", s_path,
                  s_meta.width, s_meta.height, s_meta.frame_count,
                  1000000.0 / s_meta.frame_period_us);
    return true;
}

void video_svc_poll()
{
    if (s_state != VideoPlayState::loading && s_state != VideoPlayState::playing) return;
    if (power_svc_screen_off()) {
        if (!s_screen_suspended && s_audio_active) audio_svc_pcm_pause(true);
        s_screen_suspended = true;
        return;
    }
    if (s_screen_suspended) {
        s_screen_suspended = false;
        s_next_due_us = micros();
        if (s_audio_active) audio_svc_pcm_pause(false);
    }

    if (s_audio_active && !s_audio_eof) {
        const FrameResult audio_result = refill_audio(audio_refill_ms);
        if (audio_result == FrameResult::error) {
            finish_playback(VideoPlayState::error);
            set_error("AVI audio read failed");
            return;
        }
        s_audio_eof = audio_result == FrameResult::end;
    }

    uint32_t start = 0;
    uint32_t size = 0;
    if (s_audio_active) {
        const uint32_t audio_position_ms = audio_svc_pcm_position_ms();
        const uint32_t buffered_ms = audio_svc_pcm_buffered_ms();
        const uint32_t duration_ms = (uint32_t)((uint64_t)s_meta.frame_count *
                                                s_meta.frame_period_us / 1000ULL);
        if (!s_audio_eof && buffered_ms < 40 && millis() - s_last_buffer_warning_ms >= 1000) {
            s_last_buffer_warning_ms = millis();
            Serial.printf("PCM buffer low: %u ms at %u ms\n",
                          (unsigned)buffered_ms, (unsigned)audio_position_ms);
        }
        if (s_audio_eof && buffered_ms == 0) {
            if (s_frame_index >= s_meta.frame_count || audio_position_ms >= duration_ms) {
                finish_playback(VideoPlayState::ended);
                return;
            }
            audio_svc_pcm_end();
            s_audio_active = false;
            s_next_due_us = micros();
        }
        if (!s_audio_active) return;

        uint32_t wanted_frame = (uint32_t)((uint64_t)audio_position_ms * 1000ULL /
                                           s_meta.frame_period_us);
        if (wanted_frame >= s_meta.frame_count && s_meta.frame_count) {
            wanted_frame = s_meta.frame_count - 1;
        }
        if (s_frame_index >= s_meta.frame_count || s_frame_index > wanted_frame) return;
        while (s_frame_index < wanted_frame) {
            const FrameResult skipped = next_frame(start, size);
            if (skipped == FrameResult::end) {
                finish_playback(VideoPlayState::ended);
                return;
            }
            if (skipped == FrameResult::error) {
                finish_playback(VideoPlayState::error);
                set_error("AVI stream read failed");
                return;
            }
            s_frame_index++;
            s_dropped_frames++;
        }
    }

    uint32_t now = micros();
    if (!s_audio_active && (int32_t)(now - s_next_due_us) < 0) return;

    while (!s_audio_active && (int32_t)(now - s_next_due_us) > (int32_t)s_meta.frame_period_us) {
        const FrameResult skipped = next_frame(start, size);
        if (skipped == FrameResult::wait) return;
        if (skipped == FrameResult::end) {
            finish_playback(VideoPlayState::ended);
            return;
        }
        if (skipped == FrameResult::error) {
            finish_playback(VideoPlayState::error);
            set_error("AVI stream read failed");
            return;
        }
        s_frame_index++;
        s_dropped_frames++;
        s_next_due_us += s_meta.frame_period_us;
    }
    const FrameResult frame = next_frame(start, size);
    if (frame == FrameResult::wait) return;
    if (frame == FrameResult::end) {
        finish_playback(VideoPlayState::ended);
        return;
    }
    if (frame == FrameResult::error) {
        finish_playback(VideoPlayState::error);
        set_error("AVI stream read failed");
        return;
    }
    const uint32_t decode_started = micros();
    if (!decode_frame(start, size)) {
        finish_playback(VideoPlayState::error);
        set_error("JPEG decode failed");
        return;
    }
    s_decode_time_us += (uint32_t)(micros() - decode_started);
    s_decoded_frames++;
    s_frame_index++;
    s_frame_ready = true;
    s_state = VideoPlayState::playing;
    if (!s_audio_active) s_next_due_us += s_meta.frame_period_us;
}

void video_svc_toggle()
{
    if (s_state == VideoPlayState::playing || s_state == VideoPlayState::loading) {
        s_state = VideoPlayState::paused;
        if (s_audio_active) audio_svc_pcm_pause(true);
    } else if (s_state == VideoPlayState::paused) {
        s_state = VideoPlayState::playing;
        s_next_due_us = micros();
        if (s_audio_active) audio_svc_pcm_pause(false);
    } else if (s_state == VideoPlayState::ended) {
        char path[sizeof(s_path)];
        strncpy(path, s_path, sizeof(path));
        path[sizeof(path) - 1] = 0;
        video_svc_play(path);
    }
}

void video_svc_stop()
{
    if (s_file || s_frame) {
        log_performance();
        log_memory("stopping");
    }
    release_resources(false);
    log_memory("stopped");
}

VideoStatus video_svc_status()
{
    VideoStatus status;
    status.state = s_state;
    status.width = s_meta.width;
    status.height = s_meta.height;
    status.fps = s_meta.frame_period_us ? (uint16_t)((1000000U + s_meta.frame_period_us / 2) /
                                                     s_meta.frame_period_us) : 0;
    status.frame_index = s_frame_index;
    status.frame_count = s_meta.frame_count;
    status.position_ms = s_audio_active ? audio_svc_pcm_position_ms() :
        (s_meta.frame_period_us ?
         (uint32_t)((uint64_t)s_frame_index * s_meta.frame_period_us / 1000ULL) : 0);
    status.duration_ms = s_meta.frame_period_us ?
        (uint32_t)((uint64_t)s_meta.frame_count * s_meta.frame_period_us / 1000ULL) : 0;
    if (status.duration_ms && status.position_ms > status.duration_ms) status.position_ms = status.duration_ms;
    status.error = s_error[0] ? s_error : nullptr;
    return status;
}

const uint16_t *video_svc_framebuffer()
{
    return s_frame;
}

bool video_svc_take_frame()
{
    const bool ready = s_frame_ready;
    s_frame_ready = false;
    return ready;
}
