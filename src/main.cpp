#include <Arduino.h>
#include <OneButton.h>

#include "hal.h"
#include "ui.h"
#include "ui/volume_overlay.h"
#include "services/volume_service.h"
#include "wifi_svc.h"
#include "board_config.h"

static OneButton s_btn_boot(PIN_BTN_BOOT, true);
static OneButton s_btn_power(PIN_BTN_PWR, true);
static OneButton s_btn_plus(PIN_BTN_PLUS, true);

static void volume_down()
{
    volume_overlay_show(volume_service_adjust(-10), false);
}

static void volume_up()
{
    volume_overlay_show(volume_service_adjust(10), true);
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
    ui_init();

    s_btn_boot.attachClick(ui_go_home);
    s_btn_boot.attachDoubleClick(ui_show_recents);
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
    ui_tick();
    hal_poll_autorotate();
    s_btn_boot.tick();
    s_btn_power.tick();
    s_btn_plus.tick();
    delay(5);
}
