#include "aichats.h"
#include "assets/assets.h"
#include "ui/ui_components.h"

void aichats_app::create(const AppContext &context)
{
    ui_components::add_app_header(context.root, "AIChats", 0x2F7BFF);
    lv_obj_t *icon = lv_image_create(context.root);
    lv_image_set_src(icon, &img_app_aichats);
    lv_obj_set_size(icon, 72, 72);
    lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t *title = lv_label_create(context.root);
    lv_label_set_text(title, "Hi, I'm Xiaozhi");
    lv_obj_set_style_text_color(title, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 148);

    lv_obj_t *hint = lv_label_create(context.root);
    lv_label_set_text(hint, "Hold to talk");
    lv_obj_set_style_text_color(hint, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
}
