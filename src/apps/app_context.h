#pragma once

#include <lvgl.h>

struct AppContext {
    lv_obj_t *root;
    void (*push_overlay)(lv_obj_t *overlay);
};
