#include "squareline.h"
#include "ui/ui_components.h"
#include <Arduino.h>
#include <cstdio>

namespace {
lv_obj_t *s_system_label = nullptr;
}

void squareline_app::create(const AppContext &context)
{
    ui_components::add_app_header(context.root, "Squareline", 0x5E5CE6);
    s_system_label = lv_label_create(context.root);
    lv_obj_set_style_text_font(s_system_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_system_label, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_width(s_system_label, 212);
    lv_label_set_long_mode(s_system_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_system_label, LV_ALIGN_TOP_LEFT, 14, 70);
}

void squareline_app::update()
{
    if (!s_system_label) return;
    char buffer[192];
    snprintf(buffer, sizeof(buffer),
             "ESP32-S3-Touch-LCD-1.54\nCPU  240 MHz\nHeap  %u / %u KB\n"
             "PSRAM %u / %u KB\nUptime %lu s",
             (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getHeapSize() / 1024),
             (unsigned)(ESP.getFreePsram() / 1024),
             (unsigned)(ESP.getPsramSize() / 1024),
             (unsigned long)(millis() / 1000));
    lv_label_set_text(s_system_label, buffer);
}

void squareline_app::destroy()
{
    s_system_label = nullptr;
}
