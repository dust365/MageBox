#pragma once

#include <cstdint>

enum class VideoPlayState : uint8_t {
    idle,
    loading,
    playing,
    paused,
    ended,
    error,
};

struct VideoStatus {
    VideoPlayState state = VideoPlayState::idle;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t fps = 0;
    uint32_t frame_index = 0;
    uint32_t frame_count = 0;
    uint32_t position_ms = 0;
    uint32_t duration_ms = 0;
    const char *error = nullptr;
};

struct VideoFileInfo {
    bool supported = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t fps = 0;
    uint32_t duration_ms = 0;
    bool has_audio = false;
    bool audio_supported = false;
    uint32_t audio_sample_rate = 0;
    uint8_t audio_channels = 0;
    uint8_t audio_bits = 0;
    char audio_codec[8] = {};
};

bool video_svc_probe(const char *path, VideoFileInfo *info);
bool video_svc_play(const char *path);
void video_svc_poll();
void video_svc_toggle();
void video_svc_stop();
VideoStatus video_svc_status();
const uint16_t *video_svc_framebuffer();
bool video_svc_take_frame();
