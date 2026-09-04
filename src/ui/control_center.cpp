#include "control_center.h"

#include <lvgl.h>

#include "assets/assets.h"
#include "board_config.h"
#include "hal.h"
#include "services/volume_service.h"
#include "services/power_service.h"
#include "ui/ui_components.h"
#include "wifi_svc.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {

constexpr int panel_height = 220;
constexpr int snap_distance = panel_height / 2;
constexpr int snap_velocity = 480;
constexpr uint32_t anim_ms = 180;
constexpr uint32_t status_period_ms = 500;
constexpr uint32_t color_panel = 0x1C1C1E;
constexpr uint32_t color_tile = 0x2C2C2E;
constexpr uint32_t color_tile_on = 0x0A84FF;
constexpr uint32_t color_track = 0x3A3A3C;

const lv_image_dsc_t *const wifi_icons[] = {
    &esp_brookesia_image_small_status_bar_wifi_close_20_20,
    &esp_brookesia_image_small_status_bar_wifi_level1_20_20,
    &esp_brookesia_image_small_status_bar_wifi_level2_20_20,
    &esp_brookesia_image_small_status_bar_wifi_level3_20_20,
};

enum class Phase : uint8_t {
    hidden,
    pulling,
    open,
    animating,
};

lv_obj_t *s_root = nullptr;
lv_obj_t *s_scrim = nullptr;
lv_obj_t *s_panel = nullptr;
lv_obj_t *s_wifi_tile = nullptr;
lv_obj_t *s_wifi_tile_icon = nullptr;
lv_obj_t *s_lock_tile = nullptr;
lv_obj_t *s_screen_tile = nullptr;
lv_obj_t *s_brightness = nullptr;
lv_obj_t *s_volume = nullptr;
lv_obj_t *s_volume_icon = nullptr;

ControlCenterWifiOpener s_wifi_opener = nullptr;
Phase s_phase = Phase::hidden;
int s_reveal = 0;
int s_pull_origin_y = 0;
int s_pull_origin_reveal = 0;
int s_handle_last_y = 0;
uint32_t s_handle_last_ms = 0;
int s_handle_velocity = 0;
int s_anim_target = 0;
uint32_t s_last_status_ms = 0;
bool s_brightness_dragging = false;
bool s_volume_dragging = false;

int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

void apply_reveal(int reveal);

void anim_exec(void *, int32_t value)
{
    apply_reveal((int)value);
}

void set_interactive(bool on)
{
    if (!s_scrim || !s_panel) return;
    if (on) {
        lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
    }
}

void hide_now()
{
    lv_anim_delete(s_panel, anim_exec);
    apply_reveal(0);
    s_phase = Phase::hidden;
    set_interactive(false);
    if (s_root) lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void on_anim_done(lv_anim_t *)
{
    if (s_anim_target >= panel_height) {
        apply_reveal(panel_height);
        s_phase = Phase::open;
        set_interactive(true);
        return;
    }
    hide_now();
}

void animate_reveal(int target)
{
    target = clamp_int(target, 0, panel_height);
    lv_anim_delete(s_panel, anim_exec);
    s_anim_target = target;
    if (abs(s_reveal - target) <= 2) {
        apply_reveal(target);
        on_anim_done(nullptr);
        return;
    }

    s_phase = Phase::animating;
    set_interactive(false);
    if (s_root) lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_panel);
    lv_anim_set_values(&anim, s_reveal, target);
    lv_anim_set_duration(&anim, anim_ms);
    lv_anim_set_exec_cb(&anim, anim_exec);
    lv_anim_set_completed_cb(&anim, on_anim_done);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

void apply_reveal(int reveal)
{
    s_reveal = clamp_int(reveal, 0, panel_height);
    if (s_panel) lv_obj_set_y(s_panel, s_reveal - panel_height);
    if (s_scrim) {
        lv_obj_set_style_bg_opa(s_scrim, (lv_opa_t)(s_reveal * 150 / panel_height), 0);
    }
}

void refresh_lock_tile()
{
    if (!s_lock_tile) return;
    bool locked = !hal_autorotate_enabled();
    lv_obj_set_style_bg_color(s_lock_tile, lv_color_hex(locked ? color_tile_on : color_tile), 0);
}

void refresh_screen_tile()
{
    if (!s_screen_tile) return;
    lv_obj_set_style_bg_color(s_screen_tile,
        lv_color_hex(power_svc_screen_off() ? color_tile_on : color_tile), 0);
}

void refresh_volume_icon(int percent)
{
    if (!s_volume_icon) return;
    const char *symbol = LV_SYMBOL_MUTE;
    if (percent > 60) symbol = LV_SYMBOL_VOLUME_MAX;
    else if (percent > 0) symbol = LV_SYMBOL_VOLUME_MID;
    lv_label_set_text(s_volume_icon, symbol);
}

void refresh_status()
{
    int wifi_level = wifi_svc_rssi_level();
    if (s_wifi_tile_icon) {
        lv_image_set_src(s_wifi_tile_icon, wifi_icons[wifi_level]);
        lv_obj_set_style_image_recolor(s_wifi_tile_icon,
            lv_color_hex(wifi_level == 0 ? ui_components::color_muted : ui_components::color_text), 0);
    }

    refresh_lock_tile();
    refresh_screen_tile();
    if (s_brightness && !s_brightness_dragging) {
        lv_slider_set_value(s_brightness, clamp_int(hal_get_brightness(), 8, 100), LV_ANIM_OFF);
    }
    if (s_volume && !s_volume_dragging) {
        int volume = volume_service_get();
        lv_slider_set_value(s_volume, volume, LV_ANIM_OFF);
        refresh_volume_icon(volume);
    }
}

void on_wifi_clicked(lv_event_t *)
{
    hide_now();
    if (s_wifi_opener) s_wifi_opener();
}

void on_lock_clicked(lv_event_t *)
{
    hal_autorotate_enable(!hal_autorotate_enabled());
    refresh_lock_tile();
}

void on_screen_clicked(lv_event_t *)
{
    power_svc_toggle_screen();
    refresh_screen_tile();
    // Close the panel after switching off the backlight so the hidden panel
    // cannot consume the first touch used to wake the screen.
    if (power_svc_screen_off()) control_center_close();
}

void on_brightness_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) lv_event_stop_bubbling(event);
    if (code == LV_EVENT_PRESSED) s_brightness_dragging = true;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) s_brightness_dragging = false;
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_PRESSED) return;
    int value = clamp_int((int)lv_slider_get_value(s_brightness), 8, 100);
    hal_set_brightness((uint8_t)value);
}

void on_volume_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) lv_event_stop_bubbling(event);
    if (code == LV_EVENT_PRESSED) s_volume_dragging = true;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) s_volume_dragging = false;
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_PRESSED) return;
    int value = clamp_int((int)lv_slider_get_value(s_volume), 0, 100);
    volume_service_set(value);
    refresh_volume_icon(value);
}

void on_handle_event(lv_event_t *event)
{
    if (s_phase != Phase::open && s_phase != Phase::pulling) return;

    lv_indev_t *indev = lv_indev_active();
    lv_point_t point = {};
    if (indev) lv_indev_get_point(indev, &point);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        lv_anim_delete(s_panel, anim_exec);
        s_phase = Phase::pulling;
        set_interactive(false);
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
        s_pull_origin_y = point.y;
        s_pull_origin_reveal = s_reveal;
        s_handle_last_y = point.y;
        s_handle_last_ms = millis();
        s_handle_velocity = 0;
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        apply_reveal(s_pull_origin_reveal + (int)(point.y - s_pull_origin_y));
        uint32_t now = millis();
        uint32_t dt = now - s_handle_last_ms;
        if (dt >= 16) {
            s_handle_velocity = ((int)point.y - s_handle_last_y) * 1000 / (int)dt;
            s_handle_last_y = point.y;
            s_handle_last_ms = now;
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        control_center_finish_pull(s_handle_velocity);
    }
}

lv_obj_t *make_icon(lv_obj_t *parent, const lv_image_dsc_t *source)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_size(image, 20, 20);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_image_recolor(image, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

lv_obj_t *make_sun_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 18, 18);
    lv_obj_set_pos(icon, x, y);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    const int rays[][4] = {
        {8, 0, 2, 4}, {8, 14, 2, 4}, {0, 8, 4, 2}, {14, 8, 4, 2},
        {3, 3, 2, 2}, {13, 3, 2, 2}, {3, 13, 2, 2}, {13, 13, 2, 2},
    };
    for (int i = 0; i < 8; ++i) {
        lv_obj_t *mark = lv_obj_create(icon);
        lv_obj_remove_style_all(mark);
        lv_obj_set_size(mark, rays[i][2], rays[i][3]);
        lv_obj_set_pos(mark, rays[i][0], rays[i][1]);
        lv_obj_set_style_bg_color(mark, lv_color_hex(ui_components::color_muted), 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(mark, 1, 0);
        lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *core = lv_obj_create(icon);
    lv_obj_remove_style_all(core);
    lv_obj_set_size(core, 8, 8);
    lv_obj_center(core);
    lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(core, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
    lv_obj_clear_flag(core, LV_OBJ_FLAG_CLICKABLE);
    return icon;
}

lv_obj_t *make_tile(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 68, 70);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_radius(tile, 16, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(color_tile), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    return tile;
}

lv_obj_t *make_slider(lv_obj_t *parent, int y, int minimum)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 176, 22);
    lv_obj_set_pos(slider, 44, y);
    lv_slider_set_range(slider, minimum, 100);
    lv_obj_set_ext_click_area(slider, 10);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(slider, 11, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 11, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(color_track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(color_tile_on), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 3, LV_PART_KNOB);
    return slider;
}

void create_widgets()
{
    lv_display_t *display = lv_display_get_default();
    if (!display) return;

    s_root = lv_obj_create(lv_display_get_layer_top(display));
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_scrim = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_scrim, [](lv_event_t *) { control_center_close(); }, LV_EVENT_CLICKED, nullptr);

    s_panel = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, BOARD_LCD_H_RES, panel_height);
    lv_obj_set_pos(s_panel, 0, -panel_height);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(color_panel), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLL_CHAIN);

    s_wifi_tile = make_tile(s_panel, 8, 16);
    lv_obj_add_event_cb(s_wifi_tile, on_wifi_clicked, LV_EVENT_CLICKED, nullptr);
    s_wifi_tile_icon = make_icon(s_wifi_tile, wifi_icons[0]);
    lv_obj_align(s_wifi_tile_icon, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *wifi_label = lv_label_create(s_wifi_tile);
    lv_label_set_text(wifi_label, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_align(wifi_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_clear_flag(wifi_label, LV_OBJ_FLAG_CLICKABLE);

    s_lock_tile = make_tile(s_panel, 86, 16);
    lv_obj_add_event_cb(s_lock_tile, on_lock_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lock_icon = lv_label_create(s_lock_tile);
    lv_label_set_text(lock_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(lock_icon, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(lock_icon, &lv_font_montserrat_16, 0);
    lv_obj_align(lock_icon, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_clear_flag(lock_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *lock_label = lv_label_create(s_lock_tile);
    lv_label_set_text(lock_label, "Lock");
    lv_obj_set_style_text_color(lock_label, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(lock_label, &lv_font_montserrat_12, 0);
    lv_obj_align(lock_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_clear_flag(lock_label, LV_OBJ_FLAG_CLICKABLE);

    s_screen_tile = make_tile(s_panel, 164, 16);
    lv_obj_add_event_cb(s_screen_tile, on_screen_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *screen_icon = lv_label_create(s_screen_tile);
    lv_label_set_text(screen_icon, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(screen_icon, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(screen_icon, &lv_font_montserrat_16, 0);
    lv_obj_align(screen_icon, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *screen_label = lv_label_create(s_screen_tile);
    lv_label_set_text(screen_label, "Screen");
    lv_obj_set_style_text_color(screen_label, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(screen_label, &lv_font_montserrat_10, 0);
    lv_obj_align(screen_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_clear_flag(screen_label, LV_OBJ_FLAG_CLICKABLE);

    make_sun_icon(s_panel, 16, 106);
    s_brightness = make_slider(s_panel, 104, 8);
    lv_obj_add_event_cb(s_brightness, on_brightness_event, LV_EVENT_ALL, nullptr);

    s_volume_icon = lv_label_create(s_panel);
    lv_label_set_text(s_volume_icon, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_color(s_volume_icon, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_volume_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_volume_icon, 16, 150);
    s_volume = make_slider(s_panel, 148, 0);
    lv_obj_add_event_cb(s_volume, on_volume_event, LV_EVENT_ALL, nullptr);

    lv_obj_t *handle = lv_obj_create(s_panel);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, BOARD_LCD_H_RES, 22);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(handle, on_handle_event, LV_EVENT_ALL, nullptr);
    lv_obj_t *bar = lv_obj_create(handle);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 36, 4);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
}

} // namespace

void control_center_init()
{
    create_widgets();
    apply_reveal(0);
    refresh_status();
}

void control_center_tick()
{
    if (s_phase == Phase::hidden) return;
    uint32_t now = millis();
    if (now - s_last_status_ms < status_period_ms) return;
    s_last_status_ms = now;
    refresh_status();
}

bool control_center_is_visible()
{
    return s_phase != Phase::hidden;
}

void control_center_close()
{
    if (s_phase == Phase::hidden) return;
    if (s_phase == Phase::animating && s_anim_target == 0) return;
    animate_reveal(0);
}

void control_center_set_wifi_opener(ControlCenterWifiOpener opener)
{
    s_wifi_opener = opener;
}

void control_center_begin_pull(int32_t finger_y)
{
    lv_anim_delete(s_panel, anim_exec);
    if (s_root) lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_phase = Phase::pulling;
    set_interactive(false);
    s_pull_origin_y = finger_y;
    s_pull_origin_reveal = s_reveal;
}

void control_center_update_pull(int32_t finger_y)
{
    if (s_phase != Phase::pulling) return;
    apply_reveal(s_pull_origin_reveal + (int)(finger_y - s_pull_origin_y));
}

void control_center_finish_pull(int32_t velocity_y)
{
    if (s_phase != Phase::pulling) return;
    bool open = s_reveal >= snap_distance;
    if (velocity_y > snap_velocity) open = true;
    if (velocity_y < -snap_velocity) open = false;
    animate_reveal(open ? panel_height : 0);
}
