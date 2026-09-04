#include <Arduino.h>
#include <OneButton.h>

#include "hal.h"
#include "ui.h"
#include "ui/volume_overlay.h"
#include "services/audio_service.h"
#include "services/sd_service.h"
#include "services/volume_service.h"
#include "services/power_service.h"
#include "wifi_svc.h"
#include "board_config.h"

static OneButton s_btn_boot(PIN_BTN_BOOT, true);
static OneButton s_btn_power(PIN_BTN_PWR, true);
static OneButton s_btn_plus(PIN_BTN_PLUS, true);

static void volume_down()
{
    const bool waking = power_svc_screen_off();
    power_svc_wake();
    if (waking) return;
    volume_overlay_show(volume_service_adjust(-10), false);
}

static void volume_up()
{
    const bool waking = power_svc_screen_off();
    power_svc_wake();
    if (waking) return;
    volume_overlay_show(volume_service_adjust(10), true);
}

static void request_shutdown()
{
    power_svc_wake();
    ui_show_shutdown_confirm();
}

static void boot_click()
{
    if (power_svc_screen_off()) {
        power_svc_wake();
        return;
    }
    ui_go_home();
}

static void boot_double_click()
{
    if (power_svc_screen_off()) {
        power_svc_wake();
        return;
    }
    ui_show_recents();
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\nESP32-S3-Touch-LCD-1.54  Brookesia-style UI");

    if (!hal_init()) {
        Serial.println("HAL init failed");
        while (true) {
            delay(1000);
        }
    }

    wifi_svc_begin();
    volume_service_begin();
    sd_svc_begin();
    audio_svc_begin();
    ui_init();

    s_btn_boot.attachClick(boot_click);
    s_btn_boot.attachDoubleClick(boot_double_click);
    s_btn_boot.attachLongPressStart(request_shutdown);
    s_btn_power.attachClick(volume_down);
    s_btn_plus.attachClick(volume_up);
    s_btn_power.setLongPressIntervalMs(180);
    s_btn_plus.setLongPressIntervalMs(180);
    s_btn_power.attachDuringLongPress(volume_down);
    s_btn_plus.attachDuringLongPress(volume_up);
}

void loop()
{
    lv_timer_handler();
    wifi_svc_poll();
    volume_service_poll();
    power_svc_poll();
    audio_svc_poll();
    ui_tick();
    hal_poll_autorotate();
    s_btn_boot.tick();
    s_btn_power.tick();
    s_btn_plus.tick();
    delay(5);
}
