#include "gravity.h"
#include "hal.h"
#include "ui/ui_components.h"

namespace {
lv_obj_t *s_ball = nullptr;
}

void gravity_app::create(const AppContext &context)
{
    ui_components::add_app_header(context.root, "Gravity Ball", 0xFF9F0A);
    lv_obj_t *arena = lv_obj_create(context.root);
    lv_obj_set_size(arena, 200, 140);
    lv_obj_set_style_radius(arena, 16, 0);
    lv_obj_set_style_bg_color(arena, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(arena, 0, 0);
    lv_obj_align(arena, LV_ALIGN_CENTER, 0, 12);
    lv_obj_clear_flag(arena, LV_OBJ_FLAG_SCROLLABLE);

    s_ball = lv_obj_create(arena);
    lv_obj_remove_style_all(s_ball);
    lv_obj_set_size(s_ball, 22, 22);
    lv_obj_set_style_radius(s_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ball, lv_color_hex(0x2F7BFF), 0);
    lv_obj_set_style_bg_opa(s_ball, LV_OPA_COVER, 0);
    lv_obj_align(s_ball, LV_ALIGN_CENTER, 0, 0);

    if (!hal_imu_ready()) {
        lv_obj_t *message = lv_label_create(context.root);
        lv_label_set_text(message, "IMU not found");
        lv_obj_set_style_text_color(message, lv_color_hex(0xFF453A), 0);
        lv_obj_align(message, LV_ALIGN_BOTTOM_MID, 0, -28);
    }
}

void gravity_app::update()
{
    if (!s_ball || !hal_imu_ready()) return;
    float ax = 0, ay = 0, az = 0;
    if (!hal_imu_read_screen(&ax, &ay, &az)) return;
    int x = (int)(ax * 70.0f);
    int y = (int)(ay * 50.0f);
    if (x > 80) x = 80;
    if (x < -80) x = -80;
    if (y > 50) y = 50;
    if (y < -50) y = -50;
    lv_obj_align(s_ball, LV_ALIGN_CENTER, x, y);
}

void gravity_app::destroy()
{
    s_ball = nullptr;
}
