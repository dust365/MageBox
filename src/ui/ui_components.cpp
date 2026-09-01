#include "ui_components.h"

#include "board_config.h"

namespace ui_components {

lv_obj_t *make_back_hint(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 72, 4);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    return bar;
}

void add_app_header(lv_obj_t *parent, const char *title, uint32_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 14, 36);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(color_text), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align_to(label, dot, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
}

void add_page_title(lv_obj_t *parent, const char *title, uint32_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 14, 14);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(color_text), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align_to(label, dot, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
}

lv_obj_t *make_overlay(lv_obj_t *parent)
{
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(color_bg), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    make_back_hint(overlay);
    return overlay;
}

lv_obj_t *make_row(lv_obj_t *parent, const char *title, int y)
{
    lv_obj_t *row = make_card(parent, y, 36);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(color_text), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

lv_obj_t *make_card(lv_obj_t *parent, int y, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 212, height);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(color_card), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *make_track(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 188, 10);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    return bar;
}

lv_obj_t *make_switch(lv_obj_t *parent, bool checked)
{
    lv_obj_t *control = lv_switch_create(parent);
    lv_obj_set_size(control, 36, 20);
    lv_obj_set_style_bg_color(control, lv_color_hex(0x48484A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(control, lv_color_hex(0x0A84FF), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(control, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(control, 2, LV_PART_MAIN);
    if (checked) {
        lv_obj_add_state(control, LV_STATE_CHECKED);
    }
    return control;
}

} // namespace ui_components
