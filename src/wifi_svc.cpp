#include "wifi_svc.h"
#include "wifi_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <time.h>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char *k_ntp1 = "ntp.aliyun.com";
constexpr const char *k_ntp2 = "pool.ntp.org";
constexpr const char *k_ntp3 = "time.nist.gov";

WebServer *s_httpd = nullptr;
DNSServer *s_dns = nullptr;
bool s_portal = false;
bool s_ntp_started = false;
bool s_time_ok = false;
bool s_scan_pending = false;
int s_scan_n = 0;

volatile bool s_evt_got_ip = false;
volatile bool s_evt_disc = false;

char s_status[64] = "no wifi connect !";
char s_qr[80] = "WIFI:T:WPA;S:ESP32S3_1_54;P:12345678;;";
char s_scan_ssid[16][33] = {};
int s_scan_rssi[16] = {};
bool s_scan_open[16] = {};

bool parse_json_field(const String &body, const char *key, char *out, size_t n)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    int i = body.indexOf(pat);
    if (i < 0) {
        return false;
    }
    i = body.indexOf(':', i);
    if (i < 0) {
        return false;
    }
    i++;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) {
        i++;
    }
    if (i >= (int)body.length() || body[i] != '"') {
        return false;
    }
    i++;
    size_t w = 0;
    while (i < (int)body.length() && body[i] != '"' && w + 1 < n) {
        if (body[i] == '\\' && i + 1 < (int)body.length()) {
            i++;
        }
        out[w++] = body[i++];
    }
    out[w] = 0;
    return w > 0;
}

void start_ntp()
{
    configTzTime("CST-8", k_ntp1, k_ntp2, k_ntp3);
    s_ntp_started = true;
}

void on_wifi_event(arduino_event_t *e)
{
    switch (e->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        s_evt_got_ip = true;
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        s_evt_disc = true;
        break;
    default:
        break;
    }
}

void handle_root()
{
    if (!s_httpd) {
        return;
    }
    s_httpd->send(200, "text/html", WIFI_PORTAL_HTML);
}

void handle_wifi_post()
{
    if (!s_httpd) {
        return;
    }
    String body = s_httpd->hasArg("plain") ? s_httpd->arg("plain") : "";
    char ssid[33] = {};
    char pass[65] = {};
    if (!parse_json_field(body, "ssid", ssid, sizeof(ssid))) {
        s_httpd->send(400, "application/json", "{\"ok\":false,\"err\":\"ssid\"}");
        return;
    }
    parse_json_field(body, "password", pass, sizeof(pass));
    s_httpd->send(200, "application/json", "{\"ok\":true}");
    snprintf(s_status, sizeof(s_status), "WIFI Connect");
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);
}

void handle_not_found()
{
    if (!s_httpd) {
        return;
    }
    s_httpd->sendHeader("Location", WIFI_AP_URL, true);
    s_httpd->send(302, "text/plain", "Redirect to the captive portal");
}

} // namespace

void wifi_svc_begin()
{
    snprintf(s_qr, sizeof(s_qr), "WIFI:T:WPA;S:%s;P:%s;;", WIFI_AP_SSID, WIFI_AP_PASS);
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(on_wifi_event);
    snprintf(s_status, sizeof(s_status), "WIFI Connect");
    WiFi.begin();
}

void wifi_svc_start_portal()
{
    if (s_portal) {
        return;
    }
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    delay(100);

    if (!s_dns) {
        s_dns = new DNSServer();
    }
    s_dns->setErrorReplyCode(DNSReplyCode::NoError);
    s_dns->start(53, "*", WiFi.softAPIP());

    if (!s_httpd) {
        s_httpd = new WebServer(80);
    }
    s_httpd->on("/", HTTP_GET, handle_root);
    s_httpd->on("/wifi_config", HTTP_GET, handle_root);
    s_httpd->on("/wifi_config", HTTP_POST, handle_wifi_post);
    s_httpd->onNotFound(handle_not_found);
    s_httpd->begin();
    s_portal = true;
    snprintf(s_status, sizeof(s_status), "wifi config");
}

void wifi_svc_stop_portal()
{
    if (!s_portal) {
        return;
    }
    if (s_httpd) {
        s_httpd->stop();
    }
    if (s_dns) {
        s_dns->stop();
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_portal = false;
}

bool wifi_svc_portal_running()
{
    return s_portal;
}

const char *wifi_svc_qr_payload()
{
    return s_qr;
}

void wifi_svc_poll()
{
    if (s_portal) {
        if (s_dns) {
            s_dns->processNextRequest();
        }
        if (s_httpd) {
            s_httpd->handleClient();
        }
    }

    if (s_evt_got_ip) {
        s_evt_got_ip = false;
        snprintf(s_status, sizeof(s_status), "WIFI");
        start_ntp();
        if (s_portal) {
            wifi_svc_stop_portal();
        }
    }
    if (s_evt_disc) {
        s_evt_disc = false;
        if (!s_portal) {
            snprintf(s_status, sizeof(s_status), "no wifi connect !");
        }
        s_time_ok = false;
    }

    if (s_ntp_started && !s_time_ok) {
        struct tm t = {};
        if (getLocalTime(&t, 0) && (t.tm_year + 1900) >= 2024) {
            s_time_ok = true;
        }
    }

    if (s_scan_pending) {
        int n = WiFi.scanComplete();
        if (n != WIFI_SCAN_RUNNING) {
            s_scan_pending = false;
            s_scan_n = 0;
            if (n > 0) {
                if (n > 16) {
                    n = 16;
                }
                for (int i = 0; i < n; ++i) {
                    strncpy(s_scan_ssid[i], WiFi.SSID(i).c_str(), 32);
                    s_scan_ssid[i][32] = 0;
                    s_scan_rssi[i] = WiFi.RSSI(i);
                    s_scan_open[i] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                }
                s_scan_n = n;
            }
            WiFi.scanDelete();
        }
    }
}

bool wifi_svc_connected()
{
    return WiFi.status() == WL_CONNECTED;
}

int wifi_svc_rssi_level()
{
    if (!wifi_svc_connected()) {
        return 0;
    }
    int rssi = WiFi.RSSI();
    if (rssi >= -55) {
        return 3;
    }
    if (rssi >= -70) {
        return 2;
    }
    return 1;
}

const char *wifi_svc_ssid()
{
    static char ssid[33];
    if (!wifi_svc_connected()) {
        ssid[0] = 0;
        return ssid;
    }
    strncpy(ssid, WiFi.SSID().c_str(), 32);
    ssid[32] = 0;
    return ssid;
}

const char *wifi_svc_status_text()
{
    if (wifi_svc_connected()) {
        static char buf[48];
        snprintf(buf, sizeof(buf), "%s", wifi_svc_ssid());
        return buf;
    }
    return s_status;
}

bool wifi_svc_time_ready()
{
    return s_time_ok;
}

bool wifi_svc_clock_hms(int *hour, int *minute, int *second)
{
    if (!hour || !minute || !second || !s_time_ok) return false;
    struct tm t = {};
    if (!getLocalTime(&t, 0) || (t.tm_year + 1900) < 2024) return false;
    *hour = t.tm_hour;
    *minute = t.tm_min;
    *second = t.tm_sec;
    return true;
}

void wifi_svc_clock(int *hour, int *minute)
{
    struct tm t = {};
    if (s_time_ok && getLocalTime(&t, 0)) {
        *hour = t.tm_hour;
        *minute = t.tm_min;
        return;
    }
    *hour = -1;
    *minute = -1;
}

void wifi_svc_scan_start()
{
    if (s_scan_pending) {
        return;
    }
    WiFi.scanDelete();
    int r = WiFi.scanNetworks(true, true);
    if (r == WIFI_SCAN_RUNNING || r >= 0) {
        s_scan_pending = true;
        s_scan_n = 0;
    }
}

bool wifi_svc_scan_done()
{
    return !s_scan_pending;
}

int wifi_svc_scan_count()
{
    return s_scan_n;
}

const char *wifi_svc_scan_ssid(int i)
{
    if (i < 0 || i >= s_scan_n) {
        return "";
    }
    return s_scan_ssid[i];
}

int wifi_svc_scan_rssi(int i)
{
    if (i < 0 || i >= s_scan_n) {
        return 0;
    }
    return s_scan_rssi[i];
}

bool wifi_svc_scan_open(int i)
{
    if (i < 0 || i >= s_scan_n) {
        return false;
    }
    return s_scan_open[i];
}
