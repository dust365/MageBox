#include "debug_overlay.h"

#include <lvgl.h>
#include <cstdio>

namespace {

lv_obj_t *s_label = nullptr;
bool s_enabled = false;

void on_label_changed(lv_event_t *event)
{
    static bool formatting = false;
    if (formatting) {
        return;
    }

    lv_obj_t *label = (lv_obj_t *)lv_event_get_target(event);
    unsigned long fps = 0, cpu = 0, frame = 0, render = 0, flush = 0;
    if (sscanf(lv_label_get_text(label), "%lu FPS, %lu%% CPU\n%lu ms (%lu | %lu)",
               &fps, &cpu, &frame, &render, &flush) != 5) {
        return;
    }

    formatting = true;
    lv_label_set_text_fmt(label, "%lu FPS  |  CPU %lu%%\n%lu ms  |  R%lu F%lu",
                          fps, cpu, frame, render, flush);
    formatting = false;
}

} // namespace

void debug_overlay_init()
{
    lv_display_t *display = lv_display_get_default();
    if (!display) {
        return;
    }
    lv_obj_t *system_layer = lv_display_get_layer_sys(display);
    s_label = lv_obj_get_child_by_type(system_layer, 0, &lv_label_class);
    if (!s_label) {
        return;
    }

    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_label, lv_color_hex(0xE8EDF3), 0);
    lv_obj_set_style_text_line_space(s_label, 1, 0);
    lv_obj_set_style_bg_color(s_label, lv_color_hex(0x171A1F), 0);
    lv_obj_set_style_bg_opa(s_label, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_label, 1, 0);
    lv_obj_set_style_border_color(s_label, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_border_opa(s_label, LV_OPA_30, 0);
    lv_obj_set_style_radius(s_label, 4, 0);
    lv_obj_set_style_pad_hor(s_label, 5, 0);
    lv_obj_set_style_pad_ver(s_label, 3, 0);
    lv_obj_set_style_shadow_width(s_label, 4, 0);
    lv_obj_set_style_shadow_color(s_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_label, LV_OPA_30, 0);
    lv_obj_align(s_label, LV_ALIGN_BOTTOM_RIGHT, -5, -6);
    lv_obj_add_event_cb(s_label, on_label_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_sysmon_hide_performance(display);
}

void debug_overlay_set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (enabled) {
        lv_sysmon_show_performance(nullptr);
        if (s_label) {
            lv_obj_align(s_label, LV_ALIGN_BOTTOM_RIGHT, -5, -6);
        }
    } else {
        lv_sysmon_hide_performance(nullptr);
    }
}

bool debug_overlay_enabled()
{
    return s_enabled;
}
