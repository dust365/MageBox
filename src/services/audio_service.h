#pragma once

#include <cstdint>
#include <cstddef>

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

bool audio_svc_pcm_begin(uint32_t sample_rate, uint8_t bits, uint8_t channels);
bool audio_svc_pcm_can_accept(size_t input_bytes);
bool audio_svc_pcm_enqueue(const uint8_t *data, size_t input_bytes);
bool audio_svc_pcm_start();
void audio_svc_pcm_pause(bool paused);
void audio_svc_pcm_end();
uint32_t audio_svc_pcm_position_ms();
uint32_t audio_svc_pcm_buffered_ms();
