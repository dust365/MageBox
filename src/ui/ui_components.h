#pragma once

#include <lvgl.h>
#include <cstdint>

namespace ui_components {

constexpr uint32_t color_bg = 0x1A1A1A;
constexpr uint32_t color_card = 0x2C2C2E;
constexpr uint32_t color_text = 0xFFFFFF;
constexpr uint32_t color_muted = 0x8E8E93;

lv_obj_t *make_back_hint(lv_obj_t *parent);
void add_app_header(lv_obj_t *parent, const char *title, uint32_t color);
void add_page_title(lv_obj_t *parent, const char *title, uint32_t color);
lv_obj_t *make_overlay(lv_obj_t *parent);
lv_obj_t *make_row(lv_obj_t *parent, const char *title, int y);
lv_obj_t *make_card(lv_obj_t *parent, int y, int height);
lv_obj_t *make_track(lv_obj_t *parent, uint32_t color);
lv_obj_t *make_switch(lv_obj_t *parent, bool checked);

} // namespace ui_components
