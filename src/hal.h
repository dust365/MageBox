#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "board_config.h"

bool hal_init();
void hal_set_brightness(uint8_t percent);
uint8_t hal_get_brightness();
float hal_battery_voltage();
int hal_battery_percent();
bool hal_is_charging();
bool hal_imu_ready();
bool hal_imu_read(float *ax, float *ay, float *az);
bool hal_imu_read_screen(float *ax, float *ay, float *az);

int hal_get_rotation();
void hal_set_rotation(int rot);
void hal_autorotate_enable(bool on);
bool hal_autorotate_enabled();
void hal_poll_autorotate();

extern volatile bool g_touch_irq;
