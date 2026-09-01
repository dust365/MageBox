#pragma once

#include <cstddef>

constexpr const char *WIFI_AP_SSID = "ESP32S3_1_54";
constexpr const char *WIFI_AP_PASS = "12345678";
constexpr const char *WIFI_AP_URL = "http://192.168.4.1/wifi_config";

void wifi_svc_begin();
void wifi_svc_poll();

bool wifi_svc_connected();
int wifi_svc_rssi_level(); /* 0=off, 1..3 bars */
const char *wifi_svc_ssid();
const char *wifi_svc_status_text();

bool wifi_svc_time_ready();
void wifi_svc_clock(int *hour, int *minute);

void wifi_svc_start_portal();
void wifi_svc_stop_portal();
bool wifi_svc_portal_running();
const char *wifi_svc_qr_payload();

void wifi_svc_scan_start();
bool wifi_svc_scan_done();
int wifi_svc_scan_count();
const char *wifi_svc_scan_ssid(int i);
int wifi_svc_scan_rssi(int i);
bool wifi_svc_scan_open(int i);
