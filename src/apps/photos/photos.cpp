#include "photos.h"
#include "ui/ui_components.h"

void photos_app::create(const AppContext &context)
{
    ui_components::add_app_header(context.root, "Photos", 0xFF9F0A);
    lv_obj_t *label = lv_label_create(context.root);
    lv_label_set_text(label, "Need TF card\n/photo");
    lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}
