#pragma once

#include <cstdint>

using VolumeApplyCallback = void (*)(uint8_t percent);

constexpr int volume_event_down = 8;
constexpr int volume_event_up = 10;

void volume_service_begin();
void volume_service_poll();
uint8_t volume_service_get();
uint8_t volume_service_set(int percent);
uint8_t volume_service_adjust(int delta);
void volume_service_set_output_callback(VolumeApplyCallback callback);
