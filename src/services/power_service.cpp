#include "power_service.h"

#include "board_config.h"
#include "hal.h"
#include "services/audio_service.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_sleep.h>

namespace {

bool s_screen_off = false;
bool s_shutdown_pending = false;
uint8_t s_saved_brightness = LCD_BRIGHTNESS_DEFAULT;

} // namespace

bool power_svc_screen_off()
{
    return s_screen_off;
}

void power_svc_set_screen_off(bool off)
{
    if (off == s_screen_off) return;
    if (off) {
        s_saved_brightness = hal_get_brightness();
        if (s_saved_brightness == 0) s_saved_brightness = LCD_BRIGHTNESS_DEFAULT;
        hal_screen_off();
        s_screen_off = true;
    } else {
        s_screen_off = false;
        hal_set_brightness(s_saved_brightness);
        hal_screen_wake();
    }
}

void power_svc_toggle_screen()
{
    power_svc_set_screen_off(!s_screen_off);
}

void power_svc_request_shutdown()
{
    s_shutdown_pending = true;
}

void power_svc_cancel_shutdown()
{
    s_shutdown_pending = false;
}

void power_svc_wake()
{
    if (s_screen_off) power_svc_set_screen_off(false);
}

void power_svc_confirm_shutdown()
{
    if (!s_shutdown_pending) return;
    s_shutdown_pending = false;
    audio_svc_stop();
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);
    hal_screen_off();
    SD_MMC.end();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_BOOT, 0);
    Serial.println("Entering deep sleep");
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();
}

void power_svc_poll()
{
    if (hal_take_touch_activity()) power_svc_wake();
}
