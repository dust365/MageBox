#include "settings.h"

#include "board_config.h"
#include "hal.h"
#include "ui/debug_overlay.h"
#include "ui/ui_components.h"
#include "wifi_portal.h"
#include "wifi_svc.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {

AppContext s_context = {};
lv_obj_t *s_wifi_status = nullptr;
lv_obj_t *s_wifi_list = nullptr;
lv_obj_t *s_wifi_scan_label = nullptr;
lv_obj_t *s_qr_status = nullptr;
lv_obj_t *s_brightness_bar = nullptr;
lv_obj_t *s_brightness_label = nullptr;
lv_obj_t *s_battery_bar = nullptr;
lv_obj_t *s_battery_label = nullptr;
int s_last_scan_count = -99;
uint32_t s_last_status_update = 0;

struct BrightnessDrag {
    bool active = false;
    bool horizontal = false;
    int32_t x0 = 0;
    int32_t y0 = 0;
} s_drag;

int clamp_percent(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

void set_brightness(int value)
{
    value = clamp_percent(value, 8, 100);
    if (s_brightness_bar) lv_bar_set_value(s_brightness_bar, value, LV_ANIM_OFF);
    if (s_brightness_label) lv_label_set_text_fmt(s_brightness_label, "%d%%", value);
    hal_set_brightness((uint8_t)value);
}

int bar_value_at(lv_obj_t *bar, int x)
{
    lv_area_t area;
    lv_obj_get_coords(bar, &area);
    int width = area.x2 - area.x1;
    if (width < 1) width = 1;
    return (x - area.x1) * 100 / width;
}

void on_brightness_drag(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point = {};
    if (indev) lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        s_drag = {true, false, point.x, point.y};
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_drag.active && !s_drag.horizontal && s_brightness_bar) {
            int dx = abs(point.x - s_drag.x0);
            int dy = abs(point.y - s_drag.y0);
            if (dx < 8 && dy < 8) set_brightness(bar_value_at(s_brightness_bar, point.x));
        }
        s_drag.active = false;
        s_drag.horizontal = false;
        return;
    }
    if (code != LV_EVENT_PRESSING || !s_drag.active || !s_brightness_bar) return;

    int dx = point.x - s_drag.x0;
    int dy = point.y - s_drag.y0;
    if (!s_drag.horizontal) {
        if (abs(dx) < 8 && abs(dy) < 8) return;
        if (abs(dy) >= abs(dx)) {
            s_drag.active = false;
            return;
        }
        s_drag.horizontal = true;
    }
    set_brightness(bar_value_at(s_brightness_bar, point.x));
}

uint32_t battery_color()
{
    if (hal_is_charging()) return 0x0A84FF;
    return hal_battery_percent() <= 20 ? 0xFF453A : 0x30D158;
}

void refresh_battery()
{
    if (!s_battery_bar || !s_battery_label) return;
    int percent = hal_battery_percent();
    lv_bar_set_value(s_battery_bar, percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_battery_bar, lv_color_hex(battery_color()), LV_PART_INDICATOR);
    if (hal_is_charging()) {
        lv_label_set_text_fmt(s_battery_label, "Charging  %d%%", percent);
    } else {
        lv_label_set_text_fmt(s_battery_label, "%d%%  %.2f V", percent, hal_battery_voltage());
    }
}

void refresh_wifi_status()
{
    if (!s_wifi_status) return;
    if (wifi_svc_connected()) {
        lv_label_set_text_fmt(s_wifi_status, "On  %s", wifi_svc_ssid());
    } else {
        lv_label_set_text(s_wifi_status, "Not connected");
    }
}

void fill_wifi_list()
{
    if (!s_wifi_list) return;
    if (s_wifi_scan_label) {
        lv_label_set_text(s_wifi_scan_label, wifi_svc_scan_done() ? "Scan" : "...");
    }
    lv_obj_clean(s_wifi_list);
    if (!wifi_svc_scan_done()) {
        lv_obj_t *label = lv_label_create(s_wifi_list);
        lv_label_set_text(label, "Scanning...");
        lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_muted), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_all(label, 8, 0);
        return;
    }
    int count = wifi_svc_scan_count();
    if (count <= 0) {
        lv_obj_t *label = lv_label_create(s_wifi_list);
        lv_label_set_text(label, "No networks");
        lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_muted), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_all(label, 8, 0);
        return;
    }

    const char *current = wifi_svc_connected() ? wifi_svc_ssid() : "";
    for (int i = 0; i < count; ++i) {
        const char *ssid = wifi_svc_scan_ssid(i);
        char name[40];
        snprintf(name, sizeof(name), "%.16s", ssid);
        lv_obj_t *button = lv_list_add_button(s_wifi_list, nullptr, name);
        lv_obj_set_style_text_font(button, &lv_font_montserrat_12, 0);
        bool connected = current[0] && strcmp(ssid, current) == 0;
        lv_obj_set_style_text_color(button,
                                    lv_color_hex(connected ? 0x0A84FF : ui_components::color_text), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_ver(button, 6, 0);
        lv_obj_set_style_pad_hor(button, 8, 0);

        lv_obj_t *rssi = lv_label_create(button);
        lv_label_set_text_fmt(rssi, "%d", wifi_svc_scan_rssi(i));
        lv_obj_set_style_text_color(rssi, lv_color_hex(ui_components::color_muted), 0);
        lv_obj_set_style_text_font(rssi, &lv_font_montserrat_12, 0);
        lv_obj_clear_flag(rssi, LV_OBJ_FLAG_CLICKABLE);
    }
}

void open_qr_page()
{
    lv_obj_t *overlay = ui_components::make_overlay(s_context.root);
    ui_components::add_page_title(overlay, "Setup", 0x0A84FF);
    wifi_svc_start_portal();

    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, 108, 108);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 34);

    const char *payload = wifi_svc_qr_payload();
    lv_obj_t *qr = lv_qrcode_create(card);
    lv_qrcode_set_size(qr, 96);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_update(qr, payload, (uint32_t)strlen(payload));
    lv_obj_center(qr);

    lv_obj_t *info = lv_label_create(overlay);
    lv_label_set_text_fmt(info, "SSID  %s\nPIN   %s\nOpen  192.168.4.1", WIFI_AP_SSID, WIFI_AP_PASS);
    lv_obj_set_style_text_color(info, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 148);

    s_qr_status = lv_label_create(overlay);
    lv_label_set_text(s_qr_status, "Scan QR on phone");
    lv_obj_set_style_text_color(s_qr_status, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_qr_status, &lv_font_montserrat_12, 0);
    lv_obj_align(s_qr_status, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(overlay, [](lv_event_t *) {
        wifi_svc_stop_portal();
        s_qr_status = nullptr;
    }, LV_EVENT_DELETE, nullptr);
    s_context.push_overlay(overlay);
}

void open_wifi_page()
{
    lv_obj_t *overlay = ui_components::make_overlay(s_context.root);
    ui_components::add_page_title(overlay, "Wi-Fi", 0x0A84FF);

    s_wifi_status = lv_label_create(overlay);
    lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_wifi_status, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_wifi_status, 212);
    lv_label_set_long_mode(s_wifi_status, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_wifi_status, LV_ALIGN_TOP_LEFT, 14, 32);
    refresh_wifi_status();

    lv_obj_t *setup = ui_components::make_row(overlay, "Setup hotspot", 50);
    lv_obj_set_height(setup, 32);
    lv_obj_add_event_cb(setup, [](lv_event_t *) { open_qr_page(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *section = lv_label_create(overlay);
    lv_label_set_text(section, "Networks");
    lv_obj_set_style_text_color(section, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(section, &lv_font_montserrat_12, 0);
    lv_obj_align(section, LV_ALIGN_TOP_LEFT, 14, 90);

    lv_obj_t *scan = lv_obj_create(overlay);
    lv_obj_remove_style_all(scan);
    lv_obj_set_size(scan, 52, 24);
    lv_obj_align(scan, LV_ALIGN_TOP_RIGHT, -8, 86);
    lv_obj_add_flag(scan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scan, [](lv_event_t *) {
        wifi_svc_scan_start();
        fill_wifi_list();
    }, LV_EVENT_CLICKED, nullptr);
    s_wifi_scan_label = lv_label_create(scan);
    lv_label_set_text(s_wifi_scan_label, "Scan");
    lv_obj_set_style_text_color(s_wifi_scan_label, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_text_font(s_wifi_scan_label, &lv_font_montserrat_12, 0);
    lv_obj_center(s_wifi_scan_label);

    constexpr int list_y = 110;
    s_wifi_list = lv_list_create(overlay);
    lv_obj_set_size(s_wifi_list, 220, BOARD_LCD_V_RES - list_y - 16);
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_MID, 0, list_y);
    lv_obj_set_style_bg_color(s_wifi_list, lv_color_hex(ui_components::color_card), 0);
    lv_obj_set_style_bg_opa(s_wifi_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_wifi_list, 12, 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 4, 0);
    lv_obj_set_style_pad_row(s_wifi_list, 0, 0);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_add_event_cb(overlay, [](lv_event_t *) {
        s_wifi_status = nullptr;
        s_wifi_list = nullptr;
        s_wifi_scan_label = nullptr;
        s_last_scan_count = -99;
    }, LV_EVENT_DELETE, nullptr);
    wifi_svc_scan_start();
    fill_wifi_list();
    s_context.push_overlay(overlay);
}

lv_obj_t *make_setting_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
    return label;
}

} // namespace

void settings_app::create(const AppContext &context)
{
    s_context = context;
    ui_components::add_page_title(context.root, "Settings", 0x8E8E93);

    lv_obj_t *content = lv_obj_create(context.root);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, BOARD_LCD_H_RES, 198);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(content, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x0A84FF), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, 2, LV_PART_SCROLLBAR);

    lv_obj_t *rotation = ui_components::make_card(content, 2, 36);
    make_setting_label(rotation, "Auto-rotate");
    lv_obj_t *rotation_switch = ui_components::make_switch(rotation, hal_autorotate_enabled());
    lv_obj_align(rotation_switch, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(rotation_switch, [](lv_event_t *event) {
        lv_obj_t *control = (lv_obj_t *)lv_event_get_target(event);
        hal_autorotate_enable(lv_obj_has_state(control, LV_STATE_CHECKED));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *debug = ui_components::make_card(content, 42, 36);
    make_setting_label(debug, "Debug overlay");
    lv_obj_t *debug_switch = ui_components::make_switch(debug, debug_overlay_enabled());
    lv_obj_align(debug_switch, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(debug_switch, [](lv_event_t *event) {
        lv_obj_t *control = (lv_obj_t *)lv_event_get_target(event);
        debug_overlay_set_enabled(lv_obj_has_state(control, LV_STATE_CHECKED));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *brightness = ui_components::make_card(content, 82, 52);
    lv_obj_t *brightness_title = make_setting_label(brightness, "Brightness");
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 12, 6);
    s_brightness_label = lv_label_create(brightness);
    lv_obj_set_style_text_color(s_brightness_label, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_brightness_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_brightness_label, LV_ALIGN_TOP_RIGHT, -12, 8);
    s_brightness_bar = ui_components::make_track(brightness, 0x0A84FF);
    set_brightness(hal_get_brightness());
    lv_obj_t *brightness_hit = lv_obj_create(brightness);
    lv_obj_remove_style_all(brightness_hit);
    lv_obj_set_size(brightness_hit, 200, 28);
    lv_obj_align(brightness_hit, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(brightness_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(brightness_hit, on_brightness_drag, LV_EVENT_ALL, nullptr);

    lv_obj_t *wifi = ui_components::make_card(content, 138, 48);
    lv_obj_add_flag(wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi, [](lv_event_t *) { open_wifi_page(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *wifi_title = make_setting_label(wifi, "Wi-Fi");
    lv_obj_align(wifi_title, LV_ALIGN_TOP_LEFT, 12, 6);
    lv_obj_t *wifi_subtitle = lv_label_create(wifi);
    lv_obj_set_width(wifi_subtitle, 160);
    lv_label_set_long_mode(wifi_subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(wifi_subtitle, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(wifi_subtitle, &lv_font_montserrat_12, 0);
    lv_label_set_text(wifi_subtitle, wifi_svc_connected() ? wifi_svc_ssid() : "Not connected");
    lv_obj_align(wifi_subtitle, LV_ALIGN_BOTTOM_LEFT, 12, -6);
    lv_obj_t *chevron = lv_label_create(wifi);
    lv_label_set_text(chevron, ">");
    lv_obj_set_style_text_color(chevron, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(chevron, &lv_font_montserrat_14, 0);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *battery = ui_components::make_card(content, 190, 48);
    lv_obj_t *battery_title = make_setting_label(battery, "Battery");
    lv_obj_align(battery_title, LV_ALIGN_TOP_LEFT, 12, 6);
    s_battery_label = lv_label_create(battery);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -12, 8);
    s_battery_bar = ui_components::make_track(battery, battery_color());
    refresh_battery();

    lv_obj_t *scroll_end = lv_obj_create(content);
    lv_obj_remove_style_all(scroll_end);
    lv_obj_set_size(scroll_end, 1, 1);
    lv_obj_set_pos(scroll_end, 0, 243);
}

void settings_app::update()
{
    uint32_t now = lv_tick_get();
    if (now - s_last_status_update >= 500) {
        s_last_status_update = now;
        refresh_battery();
        refresh_wifi_status();
    }
    if (s_wifi_list) {
        int count = wifi_svc_scan_done() ? wifi_svc_scan_count() : -1;
        if (count != s_last_scan_count) {
            s_last_scan_count = count;
            fill_wifi_list();
        }
    }
    if (s_qr_status && wifi_svc_connected()) {
        lv_label_set_text(s_qr_status,
                          wifi_svc_time_ready() ? "Connected  time synced"
                                                : "Connected  syncing time");
    }
}

void settings_app::destroy()
{
    wifi_svc_stop_portal();
    s_context = {};
    s_wifi_status = nullptr;
    s_wifi_list = nullptr;
    s_wifi_scan_label = nullptr;
    s_qr_status = nullptr;
    s_brightness_bar = nullptr;
    s_brightness_label = nullptr;
    s_battery_bar = nullptr;
    s_battery_label = nullptr;
    s_last_scan_count = -99;
    s_last_status_update = 0;
    s_drag = {};
}
