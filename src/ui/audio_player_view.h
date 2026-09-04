#pragma once

#include <lvgl.h>
#include <cstdint>

enum class AudioUiState : uint8_t {
    idle,
    loading,
    playing,
    paused,
    error,
};

struct AudioPlayerViewState {
    const char *title = "";
    const char *subtitle = "";
    AudioUiState state = AudioUiState::idle;
    uint32_t position_sec = 0;
    uint32_t duration_sec = 0;
    bool seekable = false;
    bool has_prev = false;
    bool has_next = false;
    uint8_t vu_left = 0;
    uint8_t vu_right = 0;
};

struct AudioPlayerViewCallbacks {
    void (*toggle)() = nullptr;
    void (*prev)() = nullptr;
    void (*next)() = nullptr;
    void (*seek)(uint32_t sec) = nullptr;
    void (*back)() = nullptr;
};

lv_obj_t *audio_player_view_create(lv_obj_t *parent, const AudioPlayerViewCallbacks &callbacks);
void audio_player_view_apply(const AudioPlayerViewState &state);
void audio_player_view_reset();
