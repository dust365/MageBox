#pragma once

#include <cstdint>

bool power_svc_screen_off();
void power_svc_set_screen_off(bool off);
void power_svc_toggle_screen();
void power_svc_request_shutdown();
void power_svc_confirm_shutdown();
void power_svc_cancel_shutdown();
void power_svc_wake();
void power_svc_poll();
