#include "ui.h"

#include "apps/app_registry.h"
#include "apps/settings/settings.h"
#include "assets/assets.h"
#include "board_config.h"
#include "hal.h"
#include "ui/control_center.h"
#include "ui/debug_overlay.h"
#include "ui/ui_components.h"
#include "ui/volume_overlay.h"
#include "services/power_service.h"
#include "wifi_svc.h"

#include <Arduino.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int status_height = 36;
constexpr int table_width = 192;
constexpr int table_height = 168;
constexpr int icon_cell_size = 90;
constexpr int icon_size = 60;
constexpr int icon_gap = 12;
constexpr int dot_inactive_size = 12;
constexpr int dot_active_width = 40;
constexpr int dot_gap = 30;
constexpr int dot_bottom = 30;
constexpr int page_count = 5;
constexpr int bottom_edge = 10;
constexpr int top_edge = 12;
constexpr int direction_lock = 8;
constexpr int swipe_minimum = 24;
constexpr int recents_hold_ms = 380;
constexpr int nav_capacity = 8;
constexpr uint32_t color_dot_off = 0xC6C6C6;

const lv_image_dsc_t *const wifi_icons[] = {
    &esp_brookesia_image_small_status_bar_wifi_close_20_20,
    &esp_brookesia_image_small_status_bar_wifi_level1_20_20,
    &esp_brookesia_image_small_status_bar_wifi_level2_20_20,
    &esp_brookesia_image_small_status_bar_wifi_level3_20_20,
};

lv_obj_t *s_home = nullptr;
lv_obj_t *s_app_screen = nullptr;
lv_obj_t *s_recents = nullptr;
lv_obj_t *s_status_clock = nullptr;
lv_obj_t *s_status_battery_body = nullptr;
lv_obj_t *s_status_battery_fill = nullptr;
lv_obj_t *s_status_battery_cap = nullptr;
lv_obj_t *s_status_battery_charge = nullptr;
lv_obj_t *s_status_wifi = nullptr;
lv_obj_t *s_tileview = nullptr;
lv_obj_t *s_page_dots[page_count] = {};
bool s_home_paging = true;
lv_obj_t *s_nav_stack[nav_capacity] = {};
int s_page_index = 0;
int s_nav_depth = 0;
AppId s_foreground = AppId::none;
AppId s_running = AppId::none;
AppId s_recent = AppId::none;
bool s_app_alive = false;
lv_obj_t *s_shutdown_overlay = nullptr;

enum class GestureKind : uint8_t {
    idle,
    pending,
    nav,
    control,
    ignored,
};

struct GestureState {
    GestureKind kind = GestureKind::idle;
    bool from_bottom = false;
    bool from_top = false;
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t last_y = 0;
    uint32_t started_at = 0;
    uint32_t last_at = 0;
    int32_t velocity_y = 0;
} s_gesture;

void launch_app(AppId id);
void set_home_paging_enabled(bool enabled);

lv_obj_t *make_screen(uint32_t background = ui_components::color_bg)
{
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(screen, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

lv_obj_t *make_status_icon(lv_obj_t *parent, const lv_image_dsc_t *source)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_size(image, 20, 20);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_image_recolor(image, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    return image;
}

lv_obj_t *make_status_battery(lv_obj_t *parent)
{
    lv_obj_t *wrapper = lv_obj_create(parent);
    lv_obj_remove_style_all(wrapper);
    lv_obj_set_size(wrapper, 20, 20);

    s_status_battery_body = lv_obj_create(wrapper);
    lv_obj_remove_style_all(s_status_battery_body);
    lv_obj_set_size(s_status_battery_body, 16, 10);
    lv_obj_align(s_status_battery_body, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_width(s_status_battery_body, 1, 0);
    lv_obj_set_style_border_color(s_status_battery_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(s_status_battery_body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_status_battery_body, 2, 0);
    lv_obj_clear_flag(s_status_battery_body, LV_OBJ_FLAG_SCROLLABLE);

    s_status_battery_fill = lv_obj_create(s_status_battery_body);
    lv_obj_remove_style_all(s_status_battery_fill);
    lv_obj_set_height(s_status_battery_fill, 6);
    lv_obj_set_style_bg_color(s_status_battery_fill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_status_battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_status_battery_fill, 1, 0);
    lv_obj_align(s_status_battery_fill, LV_ALIGN_LEFT_MID, 2, 0);

    s_status_battery_cap = lv_obj_create(wrapper);
    lv_obj_remove_style_all(s_status_battery_cap);
    lv_obj_set_size(s_status_battery_cap, 2, 5);
    lv_obj_align(s_status_battery_cap, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_status_battery_cap, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_status_battery_cap, LV_OPA_COVER, 0);

    s_status_battery_charge = make_status_icon(
        wrapper, &esp_brookesia_image_small_status_bar_battery_charge_20_20);
    lv_obj_center(s_status_battery_charge);
    lv_obj_add_flag(s_status_battery_charge, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_status_battery_charge, LV_OBJ_FLAG_HIDDEN);
    return wrapper;
}

void push_overlay(lv_obj_t *overlay)
{
    if (!overlay || s_nav_depth >= nav_capacity) return;
    s_nav_stack[s_nav_depth++] = overlay;
}

void pop_overlay()
{
    if (s_nav_depth <= 0) return;
    lv_obj_t *overlay = s_nav_stack[--s_nav_depth];
    s_nav_stack[s_nav_depth] = nullptr;
    if (overlay) lv_obj_delete(overlay);
}

void close_app_screen()
{
    const AppDefinition *running = app_find(s_running);
    if (running && running->deactivate) running->deactivate();
    if (running && running->destroy) running->destroy();
    s_running = AppId::none;
    s_nav_depth = 0;
    memset(s_nav_stack, 0, sizeof(s_nav_stack));
    if (s_app_screen) {
        lv_obj_delete(s_app_screen);
        s_app_screen = nullptr;
    }
}

void deactivate_running_app()
{
    const AppDefinition *app = app_find(s_running);
    if (app && app->deactivate) app->deactivate();
}

void show_home_screen()
{
    deactivate_running_app();
    s_foreground = AppId::none;
    lv_screen_load(s_home);
}

void open_wifi_settings()
{
    if (s_foreground != AppId::settings) launch_app(AppId::settings);
    settings_app::open_wifi();
}

void sample_velocity(const lv_point_t &point)
{
    uint32_t now = millis();
    uint32_t dt = now - s_gesture.last_at;
    if (dt < 16) return;
    s_gesture.velocity_y = ((int32_t)point.y - s_gesture.last_y) * 1000 / (int32_t)dt;
    s_gesture.last_y = point.y;
    s_gesture.last_at = now;
}

void set_home_paging_enabled(bool enabled)
{
    if (!s_tileview) return;
    if (s_home_paging == enabled) return;
    s_home_paging = enabled;
    if (enabled) {
        lv_obj_set_scroll_dir(s_tileview, LV_DIR_HOR);
        return;
    }
    lv_obj_scroll_to_x(s_tileview, s_page_index * BOARD_LCD_H_RES, LV_ANIM_OFF);
    lv_obj_set_scroll_dir(s_tileview, LV_DIR_NONE);
}

void go_back()
{
    if (s_nav_depth > 0) pop_overlay();
    else if (s_foreground != AppId::none) show_home_screen();
}

static void close_shutdown_confirm()
{
    power_svc_cancel_shutdown();
    if (s_shutdown_overlay) {
        lv_obj_delete(s_shutdown_overlay);
        s_shutdown_overlay = nullptr;
    }
}

static void confirm_shutdown(lv_event_t *)
{
    power_svc_confirm_shutdown();
}

void show_shutdown_confirm_impl()
{
    if (s_shutdown_overlay) return;
    power_svc_request_shutdown();
    lv_display_t *display = lv_display_get_default();
    if (!display) return;
    s_shutdown_overlay = lv_obj_create(lv_display_get_layer_top(display));
    lv_obj_remove_style_all(s_shutdown_overlay);
    lv_obj_set_size(s_shutdown_overlay, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_shutdown_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_shutdown_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(s_shutdown_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_shutdown_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_shutdown_overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 216, 136);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(ui_components::color_card), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_shadow_width(panel, 18, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_40, 0);
    lv_obj_center(panel);

    lv_obj_t *icon = lv_label_create(panel);
    lv_label_set_text(icon, LV_SYMBOL_POWER);
    lv_obj_set_size(icon, 30, 30);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(icon, 6, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Turn off device?");
    lv_obj_set_style_text_color(title, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 47);

    lv_obj_t *subtitle = lv_label_create(panel);
    lv_label_set_text(subtitle, "Music will stop and the screen will turn off.");
    lv_obj_set_width(subtitle, 190);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 68);

    lv_obj_t *cancel = lv_button_create(panel);
    lv_obj_set_size(cancel, 88, 30);
    lv_obj_set_style_radius(cancel, 10, 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x48484A), 0);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 14, -12);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { close_shutdown_confirm(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_12, 0);
    lv_obj_center(cancel_label);

    lv_obj_t *confirm = lv_button_create(panel);
    lv_obj_set_size(confirm, 88, 30);
    lv_obj_set_style_radius(confirm, 10, 0);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(0xFF453A), 0);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -14, -12);
    lv_obj_add_event_cb(confirm, confirm_shutdown, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, "Turn off");
    lv_obj_set_style_text_font(confirm_label, &lv_font_montserrat_12, 0);
    lv_obj_center(confirm_label);
}

void launch_app(AppId id)
{
    const AppDefinition *app = app_find(id);
    if (!app) return;
    close_app_screen();
    s_app_screen = make_screen();
    ui_components::make_back_hint(s_app_screen);
    app->create({s_app_screen, push_overlay});
    s_foreground = id;
    s_running = id;
    s_recent = id;
    s_app_alive = true;
    lv_screen_load_anim(s_app_screen, LV_SCR_LOAD_ANIM_FADE_IN, 120, 0, false);
}

void on_icon_clicked(lv_event_t *event)
{
    launch_app((AppId)(uintptr_t)lv_event_get_user_data(event));
}

void create_icon(lv_obj_t *page, const AppDefinition &app, int slot, bool centered = false)
{
    int y = (table_height - icon_cell_size) / 2;
    int x = centered ? (BOARD_LCD_H_RES - icon_cell_size) / 2 :
        (BOARD_LCD_H_RES - table_width) / 2 + slot * (icon_cell_size + icon_gap);
    lv_obj_t *wrapper = lv_obj_create(page);
    lv_obj_remove_style_all(wrapper);
    lv_obj_set_size(wrapper, icon_cell_size, icon_cell_size);
    lv_obj_set_pos(wrapper, x, y);
    lv_obj_add_flag(wrapper, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wrapper, on_icon_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)app.id);

    lv_obj_t *image = lv_image_create(wrapper);
    lv_image_set_src(image, app.icon);
    lv_obj_set_size(image, icon_size, icon_size);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *name = lv_label_create(wrapper);
    lv_label_set_text(name, app.name);
    lv_obj_set_style_text_color(name, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE);
}

void refresh_page_dots()
{
    for (int i = 0; i < page_count; ++i) {
        if (!s_page_dots[i]) continue;
        bool active = i == s_page_index;
        lv_obj_set_size(s_page_dots[i], active ? dot_active_width : dot_inactive_size,
                        dot_inactive_size);
        lv_obj_set_style_radius(s_page_dots[i], dot_inactive_size / 2, 0);
        lv_obj_set_style_bg_color(s_page_dots[i],
            lv_color_hex(active ? ui_components::color_text : color_dot_off), 0);
        lv_obj_set_style_bg_opa(s_page_dots[i], LV_OPA_COVER, 0);
    }
}

void on_home_scroll(lv_event_t *event)
{
    lv_obj_t *tileview = (lv_obj_t *)lv_event_get_target(event);
    int page = (lv_obj_get_scroll_x(tileview) + BOARD_LCD_H_RES / 2) / BOARD_LCD_H_RES;
    if (page < 0) page = 0;
    if (page >= page_count) page = page_count - 1;
    if (page != s_page_index) {
        s_page_index = page;
        refresh_page_dots();
    }
}

void create_home()
{
    s_home = make_screen(0x000000);
    lv_obj_t *background = lv_image_create(s_home);
    lv_image_set_src(background, &img_wallpaper);
    lv_obj_center(background);
    lv_obj_clear_flag(background, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(background, LV_OBJ_FLAG_IGNORE_LAYOUT);

    lv_obj_t *status = lv_obj_create(s_home);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, BOARD_LCD_H_RES, status_height);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 3, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    s_status_wifi = make_status_icon(status, wifi_icons[0]);
    s_status_clock = lv_label_create(status);
    lv_label_set_text(s_status_clock, "--:--");
    lv_obj_set_style_text_color(s_status_clock, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(s_status_clock, &lv_font_montserrat_16, 0);
    make_status_battery(status);

    s_tileview = lv_tileview_create(s_home);
    lv_obj_set_size(s_tileview, BOARD_LCD_H_RES, table_height);
    lv_obj_align(s_tileview, LV_ALIGN_TOP_MID, 0, status_height);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_all(s_tileview, 0, 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, on_home_scroll, LV_EVENT_SCROLL_END, nullptr);

    for (int page_index = 0; page_index < page_count; ++page_index) {
        lv_dir_t direction = page_index == 0 ? LV_DIR_RIGHT
                             : page_index == page_count - 1 ? LV_DIR_LEFT : LV_DIR_HOR;
        lv_obj_t *page = lv_tileview_add_tile(s_tileview, page_index, 0, direction);
        lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        const AppDefinition *first = app_at(page_index * 2);
        const AppDefinition *second = app_at(page_index * 2 + 1);
        if (first) create_icon(page, *first, 0, first && !second);
        if (second) create_icon(page, *second, 1);
    }

    lv_obj_t *dot_box = lv_obj_create(s_home);
    lv_obj_remove_style_all(dot_box);
    lv_obj_set_height(dot_box, 20);
    lv_obj_set_width(dot_box, LV_SIZE_CONTENT);
    lv_obj_align(dot_box, LV_ALIGN_BOTTOM_MID, 0, -dot_bottom);
    lv_obj_set_flex_flow(dot_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dot_box, dot_gap, 0);
    for (int i = 0; i < page_count; ++i) {
        s_page_dots[i] = lv_obj_create(dot_box);
        lv_obj_remove_style_all(s_page_dots[i]);
        lv_obj_clear_flag(s_page_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
    refresh_page_dots();
}

void create_recents()
{
    s_recents = make_screen();
    lv_obj_t *title = lv_label_create(s_recents);
    lv_label_set_text(title, "Recents");
    lv_obj_set_style_text_color(title, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *card = lv_obj_create(s_recents);
    lv_obj_set_size(card, 108, 108);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(ui_components::color_card), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, "No app");
    lv_obj_set_style_text_color(name, lv_color_hex(ui_components::color_text), 0);
    lv_obj_center(name);
    lv_obj_set_user_data(s_recents, name);

    lv_obj_t *trash = lv_image_create(s_recents);
    lv_image_set_src(trash, &esp_brookesia_image_small_recents_screen_trash_38_38);
    lv_obj_set_size(trash, 38, 38);
    lv_obj_align(trash, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(trash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(trash, [](lv_event_t *) {
        s_app_alive = false;
        s_recent = AppId::none;
        close_app_screen();
        show_home_screen();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(card, [](lv_event_t *) {
        if (s_app_alive && s_app_screen && s_recent != AppId::none) {
            s_foreground = s_recent;
            lv_screen_load(s_app_screen);
        } else {
            show_home_screen();
        }
    }, LV_EVENT_CLICKED, nullptr);
}

void refresh_recents_card()
{
    lv_obj_t *name = s_recents ? (lv_obj_t *)lv_obj_get_user_data(s_recents) : nullptr;
    const AppDefinition *app = app_find(s_recent);
    if (name) lv_label_set_text(name, app && s_app_alive ? app->name : "No app");
}

void finish_nav_gesture()
{
    int dy = s_gesture.y0 - s_gesture.y;
    int dx = s_gesture.x - s_gesture.x0;
    uint32_t duration = millis() - s_gesture.started_at;
    bool bottom_up = s_gesture.from_bottom && dy >= swipe_minimum && abs(dx) < dy;
    if (!bottom_up) return;

    if (lv_screen_active() == s_recents) {
        if (dy > 50) {
            s_app_alive = false;
            s_recent = AppId::none;
            close_app_screen();
        }
        show_home_screen();
        return;
    }
    bool pause_mid = s_gesture.y > 70 && s_gesture.y < 170 && duration >= recents_hold_ms;
    if (pause_mid && s_foreground != AppId::none) ui_show_recents();
    else if (s_foreground != AppId::none) go_back();
    else if (s_app_alive) ui_show_recents();
}

void handle_gesture()
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) indev = lv_indev_get_next(nullptr);
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    lv_indev_state_t state = lv_indev_get_state(indev);

    if (state == LV_INDEV_STATE_PRESSED) {
        if (s_gesture.kind == GestureKind::idle) {
            if (control_center_is_visible()) {
                s_gesture.kind = GestureKind::ignored;
                return;
            }
            s_gesture.kind = GestureKind::pending;
            s_gesture.from_bottom = point.y >= BOARD_LCD_V_RES - bottom_edge;
            s_gesture.from_top = point.y <= top_edge;
            s_gesture.x0 = point.x;
            s_gesture.y0 = point.y;
            s_gesture.x = point.x;
            s_gesture.y = point.y;
            s_gesture.last_y = point.y;
            s_gesture.started_at = millis();
            s_gesture.last_at = s_gesture.started_at;
            s_gesture.velocity_y = 0;
            return;
        }

        s_gesture.x = point.x;
        s_gesture.y = point.y;
        sample_velocity(point);

        if (s_gesture.kind == GestureKind::pending) {
            int dx = point.x - s_gesture.x0;
            int dy = point.y - s_gesture.y0;
            if (abs(dx) < direction_lock && abs(dy) < direction_lock) return;
            if (s_gesture.from_top && dy > 0 && abs(dy) > abs(dx)) {
                s_gesture.kind = GestureKind::control;
                set_home_paging_enabled(false);
                control_center_begin_pull(s_gesture.y0);
                control_center_update_pull(point.y);
            } else if (s_gesture.from_bottom && dy < 0 && abs(dy) > abs(dx) &&
                       !control_center_is_visible()) {
                s_gesture.kind = GestureKind::nav;
            } else {
                s_gesture.kind = GestureKind::ignored;
            }
            return;
        }

        if (s_gesture.kind == GestureKind::control) {
            control_center_update_pull(point.y);
        }
        return;
    }

    GestureKind kind = s_gesture.kind;
    s_gesture.kind = GestureKind::idle;
    if (kind == GestureKind::control) {
        control_center_finish_pull(s_gesture.velocity_y);
        return;
    }
    if (kind == GestureKind::nav && !control_center_is_visible()) {
        finish_nav_gesture();
    }
}

void update_status()
{
    int hours = -1, minutes = -1;
    wifi_svc_clock(&hours, &minutes);
    if (hours >= 0) lv_label_set_text_fmt(s_status_clock, "%02d:%02d", hours, minutes);
    else lv_label_set_text(s_status_clock, "--:--");

    if (hal_is_charging()) {
        lv_obj_add_flag(s_status_battery_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status_battery_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status_battery_charge, LV_OBJ_FLAG_HIDDEN);
    } else {
        int battery = hal_battery_percent();
        int fill_width = (battery * 11 + 99) / 100;
        if (fill_width < 1) fill_width = 1;
        if (fill_width > 11) fill_width = 11;
        lv_obj_add_flag(s_status_battery_charge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status_battery_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status_battery_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_status_battery_fill, fill_width);
        lv_obj_set_style_bg_color(s_status_battery_fill,
                                  lv_color_hex(battery <= 20 ? 0xFF453A : 0xFFFFFF), 0);
    }
    int wifi_level = wifi_svc_rssi_level();
    lv_image_set_src(s_status_wifi, wifi_icons[wifi_level]);
    lv_obj_set_style_image_recolor(s_status_wifi,
        lv_color_hex(wifi_level == 0 ? ui_components::color_muted : ui_components::color_text), 0);
}

void update_foreground_app()
{
    const AppDefinition *app = app_find(s_foreground);
    if (app && app->update) app->update();
}

} // namespace

void ui_go_home()
{
    control_center_close();
    show_home_screen();
}

void ui_go_back() { go_back(); }
void ui_show_shutdown_confirm() { show_shutdown_confirm_impl(); }

void ui_show_recents()
{
    if (!s_recents) return;
    deactivate_running_app();
    control_center_close();
    refresh_recents_card();
    lv_obj_remove_flag(s_recents, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load_anim(s_recents, LV_SCR_LOAD_ANIM_MOVE_TOP, 160, 0, false);
}

void ui_init()
{
    debug_overlay_init();
    volume_overlay_init();
    control_center_init();
    control_center_set_wifi_opener(open_wifi_settings);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(ui_components::color_bg), 0);
    create_home();
    create_recents();
    lv_screen_load(s_home);
}

void ui_tick()
{
    static uint32_t last_status_update = 0;
    handle_gesture();
    set_home_paging_enabled(!control_center_is_visible());
    control_center_tick();
    update_foreground_app();
    if (millis() - last_status_update > 500) {
        last_status_update = millis();
        update_status();
    }
}
