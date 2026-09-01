#include "music.h"
#include "assets/assets.h"
#include "ui/ui_components.h"

void music_app::create(const AppContext &context)
{
    ui_components::add_app_header(context.root, "Music", 0x5856D6);
    lv_obj_t *icon = lv_image_create(context.root);
    lv_image_set_src(icon, &img_app_music);
    lv_obj_set_size(icon, 88, 88);
    lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *label = lv_label_create(context.root);
    lv_label_set_text(label, "Need TF card /music");
    lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -28);
}
