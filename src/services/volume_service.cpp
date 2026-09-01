#include "volume_service.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

constexpr uint8_t default_volume = 60;
constexpr uint32_t save_delay_ms = 1500;

Preferences s_preferences;
uint8_t s_volume = default_volume;
uint32_t s_changed_at = 0;
bool s_save_pending = false;
VolumeApplyCallback s_output_callback = nullptr;

uint8_t clamp_volume(int percent)
{
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return (uint8_t)percent;
}

} // namespace

void volume_service_begin()
{
    s_preferences.begin("system", false);
    s_volume = clamp_volume(s_preferences.getUChar("volume", default_volume));
}

void volume_service_poll()
{
    if (!s_save_pending || millis() - s_changed_at < save_delay_ms) return;
    s_preferences.putUChar("volume", s_volume);
    s_save_pending = false;
}

uint8_t volume_service_get()
{
    return s_volume;
}

uint8_t volume_service_set(int percent)
{
    uint8_t next = clamp_volume(percent);
    if (next == s_volume) return s_volume;
    s_volume = next;
    s_changed_at = millis();
    s_save_pending = true;
    if (s_output_callback) s_output_callback(s_volume);
    return s_volume;
}

uint8_t volume_service_adjust(int delta)
{
    return volume_service_set((int)s_volume + delta);
}

void volume_service_set_output_callback(VolumeApplyCallback callback)
{
    s_output_callback = callback;
    if (s_output_callback) s_output_callback(s_volume);
}
