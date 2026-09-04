#pragma once

#include <cstdint>

enum class AudioPlayState : uint8_t {
    idle,
    loading,
    playing,
    paused,
    error,
};

struct AudioStatus {
    AudioPlayState state = AudioPlayState::idle;
    uint32_t position_sec = 0;
    uint32_t duration_sec = 0;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    uint8_t vu_left = 0;
    uint8_t vu_right = 0;
    char title[64] = {};
    char artist[64] = {};
    const char *error = nullptr;
};

bool audio_svc_begin();
void audio_svc_poll();
void audio_svc_stop();
bool audio_svc_play(const char *path);
void audio_svc_toggle();
bool audio_svc_seek(uint32_t sec);
AudioStatus audio_svc_status();
bool audio_svc_take_eof();
