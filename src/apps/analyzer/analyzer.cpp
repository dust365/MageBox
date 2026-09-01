#include "analyzer.h"
#include "ui/ui_components.h"

namespace {
lv_obj_t *s_bars[12] = {};
}

void analyzer_app::create(const AppContext &context)
{
    ui_components::add_app_header(context.root, "Analyzer", 0xFF375F);
    for (int i = 0; i < 12; ++i) {
        s_bars[i] = lv_obj_create(context.root);
        lv_obj_remove_style_all(s_bars[i]);
        lv_obj_set_size(s_bars[i], 12, 20);
        lv_obj_set_style_radius(s_bars[i], 3, 0);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(0xFF375F), 0);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_align(s_bars[i], LV_ALIGN_BOTTOM_LEFT, 18 + i * 17, -36);
    }
}

void analyzer_app::update()
{
    for (int i = 0; i < 12; ++i) {
        if (!s_bars[i]) continue;
        int height = 16 + lv_rand(0, 70);
        lv_obj_set_height(s_bars[i], height);
        lv_obj_align(s_bars[i], LV_ALIGN_BOTTOM_LEFT, 18 + i * 17, -36);
    }
}

void analyzer_app::destroy()
{
    for (lv_obj_t *&bar : s_bars) bar = nullptr;
}
