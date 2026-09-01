#pragma once

#include <cstdint>

using ControlCenterWifiOpener = void (*)();

void control_center_init();
void control_center_tick();
bool control_center_is_visible();
void control_center_close();
void control_center_set_wifi_opener(ControlCenterWifiOpener opener);

void control_center_begin_pull(int32_t finger_y);
void control_center_update_pull(int32_t finger_y);
void control_center_finish_pull(int32_t velocity_y);
