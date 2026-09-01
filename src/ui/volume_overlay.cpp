#include "volume_overlay.h"

#include <lvgl.h>

namespace {

lv_obj_t *s_panel = nullptr;
lv_obj_t *s_direction = nullptr;
lv_obj_t *s_value = nullptr;
lv_obj_t *s_bar = nullptr;
lv_timer_t *s_hide_timer = nullptr;

void hide_overlay(lv_timer_t *)
{
    if (s_panel) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_hide_timer) lv_timer_pause(s_hide_timer);
}

} // namespace

void volume_overlay_init()
{
    lv_display_t *display = lv_display_get_default();
    if (!display) return;

    s_panel = lv_obj_create(lv_display_get_layer_sys(display));
    lv_obj_set_size(s_panel, 132, 50);
    lv_obj_align(s_panel, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_radius(s_panel, 7, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x171A1F), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0x4A5562), 0);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(s_panel, 6, 0);
    lv_obj_set_style_shadow_color(s_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_panel, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_direction = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_direction, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_direction, lv_color_hex(0x0A84FF), 0);
    lv_obj_align(s_direction, LV_ALIGN_TOP_LEFT, 10, 7);

    lv_obj_t *title = lv_label_create(s_panel);
    lv_label_set_text(title, "Volume");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F4F7), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 28, 8);

    s_value = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_value, lv_color_hex(0xC7CDD4), 0);
    lv_obj_align(s_value, LV_ALIGN_TOP_RIGHT, -9, 8);

    s_bar = lv_bar_create(s_panel);
    lv_obj_set_size(s_bar, 112, 6);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x3A3D42), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x0A84FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_hide_timer = lv_timer_create(hide_overlay, 900, nullptr);
    lv_timer_pause(s_hide_timer);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
}

void volume_overlay_show(uint8_t percent, bool increasing)
{
    if (!s_panel) return;
    lv_label_set_text(s_direction, increasing ? "+" : "-");
    lv_label_set_text_fmt(s_value, "%u%%", (unsigned)percent);
    lv_bar_set_value(s_bar, percent, LV_ANIM_ON);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
    if (s_hide_timer) {
        lv_timer_reset(s_hide_timer);
        lv_timer_resume(s_hide_timer);
    }
}
