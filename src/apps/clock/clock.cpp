#include "clock.h"

#include "assets/assets.h"
#include "board_config.h"
#include "services/power_service.h"
#include "ui/ui_components.h"
#include "wifi_svc.h"

#include <Arduino.h>
#include <cstdio>

namespace {

constexpr uint8_t digit_count = 6;
constexpr int digit_width = 28;
constexpr int digit_height = 92;
constexpr int digit_half_height = digit_height / 2;
constexpr int digit_face_height = 44;
constexpr int digit_face_y = (digit_height - digit_face_height) / 2;
constexpr int digit_gap = 2;
constexpr int colon_width = 9;
constexpr int separator_gap = 4;
constexpr int layout_x = 16;
constexpr int layout_y = 72;
constexpr uint32_t flip_duration_ms = 135;
constexpr uint32_t color_digit = 0xF2F2F7;
constexpr uint32_t color_card = 0x2C2C2E;
constexpr uint32_t color_card_edge = 0x48484A;
constexpr uint32_t color_seam = 0x09090A;

struct Digit {
    lv_obj_t *card = nullptr;
    lv_obj_t *current_label = nullptr;
    lv_obj_t *next_label = nullptr;
    int value = -1;
    int pending = -1;
    bool animating = false;
};

lv_obj_t *s_root = nullptr;
lv_obj_t *s_colons[2] = {};
Digit s_digits[digit_count] = {};
bool s_has_time = false;
bool s_backgrounded = false;
int s_last_second_of_day = -1;

void set_digit_text(lv_obj_t *label, int value)
{
    if (!label) return;
    char text[2] = {value >= 0 && value <= 9 ? (char)('0' + value) : '-', 0};
    lv_label_set_text(label, text);
}

void set_page_offset(Digit *digit, int32_t offset)
{
    if (!digit) return;
    if (digit->current_label) lv_obj_set_y(digit->current_label, digit_face_y - offset);
    if (digit->next_label) lv_obj_set_y(digit->next_label, digit_face_y + digit_height - offset);
}

void animate_page_offset(void *var, int32_t offset)
{
    set_page_offset((Digit *)var, offset);
}

void cancel_animation(Digit &digit)
{
    lv_anim_delete(&digit, animate_page_offset);
    set_page_offset(&digit, 0);
    digit.animating = false;
}

void finish_flip(lv_anim_t *animation)
{
    Digit *digit = animation ? (Digit *)lv_anim_get_user_data(animation) : nullptr;
    if (!digit) return;
    set_page_offset(digit, 0);
    set_digit_text(digit->current_label, digit->pending);
    set_digit_text(digit->next_label, digit->pending);
    digit->value = digit->pending;
    digit->pending = digit->value;
    digit->animating = false;
}

void start_second_phase(Digit *digit)
{
    if (!digit || !digit->current_label || !digit->next_label) return;

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, digit);
    lv_anim_set_user_data(&animation, digit);
    lv_anim_set_values(&animation, digit_half_height, digit_height);
    lv_anim_set_duration(&animation, flip_duration_ms);
    lv_anim_set_exec_cb(&animation, animate_page_offset);
    lv_anim_set_completed_cb(&animation, finish_flip);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void finish_first_phase(lv_anim_t *animation)
{
    Digit *digit = animation ? (Digit *)lv_anim_get_user_data(animation) : nullptr;
    start_second_phase(digit);
}

void animate_digit(Digit &digit, int value)
{
    if (value < 0 || value > 9 || digit.value == value) return;
    cancel_animation(digit);
    digit.pending = value;
    digit.animating = true;
    set_digit_text(digit.next_label, value);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &digit);
    lv_anim_set_user_data(&animation, &digit);
    lv_anim_set_values(&animation, 0, digit_half_height);
    lv_anim_set_duration(&animation, flip_duration_ms);
    lv_anim_set_exec_cb(&animation, animate_page_offset);
    lv_anim_set_completed_cb(&animation, finish_first_phase);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
    lv_anim_start(&animation);
}

void set_digit_now(Digit &digit, int value)
{
    cancel_animation(digit);
    digit.value = value;
    digit.pending = value;
    set_digit_text(digit.current_label, value);
    set_digit_text(digit.next_label, value);
    set_page_offset(&digit, 0);
}

void update_colons(int second)
{
    const lv_opa_t opacity = (second & 1) ? LV_OPA_70 : LV_OPA_COVER;
    for (lv_obj_t *colon : s_colons) {
        if (colon) lv_obj_set_style_text_opa(colon, opacity, 0);
    }
}

void apply_time(int hour, int minute, int second, bool animate)
{
    int values[digit_count] = {
        hour / 10, hour % 10, minute / 10, minute % 10, second / 10, second % 10,
    };
    for (uint8_t i = 0; i < digit_count; ++i) {
        if (animate) animate_digit(s_digits[i], values[i]);
        else set_digit_now(s_digits[i], values[i]);
    }
    update_colons(second);
}

lv_obj_t *make_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_size(label, digit_width, digit_face_height);
    lv_obj_set_style_text_color(label, lv_color_hex(color_digit), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_pos(label, 0, digit_face_y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

void make_digit(Digit &digit, lv_obj_t *parent, int x)
{
    digit.card = lv_obj_create(parent);
    lv_obj_remove_style_all(digit.card);
    lv_obj_set_size(digit.card, digit_width, digit_height);
    lv_obj_set_pos(digit.card, x, layout_y);
    lv_obj_set_style_radius(digit.card, 5, 0);
    lv_obj_set_style_bg_color(digit.card, lv_color_hex(color_card), 0);
    lv_obj_set_style_bg_opa(digit.card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(digit.card, 1, 0);
    lv_obj_set_style_border_color(digit.card, lv_color_hex(color_card_edge), 0);
    lv_obj_clear_flag(digit.card, LV_OBJ_FLAG_SCROLLABLE);

    digit.current_label = make_label(digit.card);
    digit.next_label = make_label(digit.card);
    lv_obj_set_pos(digit.next_label, 0, digit_face_y + digit_height);

    lv_obj_t *seam = lv_obj_create(digit.card);
    lv_obj_remove_style_all(seam);
    lv_obj_set_size(seam, digit_width, 2);
    lv_obj_set_pos(seam, 0, digit_half_height - 1);
    lv_obj_set_style_bg_color(seam, lv_color_hex(color_seam), 0);
    lv_obj_set_style_bg_opa(seam, LV_OPA_COVER, 0);
    lv_obj_add_flag(seam, LV_OBJ_FLAG_IGNORE_LAYOUT);

    set_digit_now(digit, -1);
}

void create_layout(lv_obj_t *parent)
{
    const int pair_width = digit_width * 2 + digit_gap;
    const int first_colon_x = layout_x + pair_width + separator_gap;
    const int second_pair_x = first_colon_x + colon_width + separator_gap;
    const int second_colon_x = second_pair_x + pair_width + separator_gap;
    const int third_pair_x = second_colon_x + colon_width + separator_gap;
    const int positions[digit_count] = {
        layout_x, layout_x + digit_width + digit_gap,
        second_pair_x, second_pair_x + digit_width + digit_gap,
        third_pair_x, third_pair_x + digit_width + digit_gap,
    };
    for (uint8_t i = 0; i < digit_count; ++i) make_digit(s_digits[i], parent, positions[i]);

    const int colon_x[2] = {first_colon_x, second_colon_x};
    for (uint8_t i = 0; i < 2; ++i) {
        s_colons[i] = lv_label_create(parent);
        lv_label_set_text(s_colons[i], ":");
        lv_obj_set_size(s_colons[i], colon_width, digit_face_height);
        lv_obj_set_pos(s_colons[i], colon_x[i], layout_y + digit_face_y);
        lv_obj_set_style_text_color(s_colons[i], lv_color_hex(color_digit), 0);
        lv_obj_set_style_text_font(s_colons[i], &lv_font_montserrat_40, 0);
        lv_obj_set_style_text_align(s_colons[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(s_colons[i], 0, 0);
        lv_obj_clear_flag(s_colons[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

void show_current_time(bool animate)
{
    int hour = -1;
    int minute = -1;
    int second = -1;
    if (!wifi_svc_clock_hms(&hour, &minute, &second)) {
        if (s_has_time) {
            for (Digit &digit : s_digits) set_digit_now(digit, -1);
        }
        s_has_time = false;
        s_last_second_of_day = -1;
        for (lv_obj_t *colon : s_colons) {
            if (colon) lv_obj_set_style_text_opa(colon, LV_OPA_40, 0);
        }
        return;
    }

    const int second_of_day = hour * 3600 + minute * 60 + second;
    if (!s_has_time) {
        apply_time(hour, minute, second, false);
        s_has_time = true;
    } else if (second_of_day != s_last_second_of_day) {
        apply_time(hour, minute, second, animate);
    }
    s_last_second_of_day = second_of_day;
}

} // namespace

void clock_app::create(const AppContext &context)
{
    s_root = context.root;
    s_has_time = false;
    s_backgrounded = false;
    s_last_second_of_day = -1;
    for (Digit &digit : s_digits) digit = {};
    for (lv_obj_t *&colon : s_colons) colon = nullptr;

    lv_obj_set_style_bg_color(context.root, lv_color_hex(0x111113), 0);
    create_layout(context.root);
    show_current_time(false);
}

void clock_app::update()
{
    if (!s_root) return;
    const bool visible = lv_screen_active() == s_root;
    if (!visible || power_svc_screen_off()) {
        s_backgrounded = true;
        show_current_time(false);
        return;
    }
    // Animate normal foreground ticks; resuming from sleep should jump to now.
    const bool animate = !s_backgrounded;
    s_backgrounded = false;
    show_current_time(animate);
}

void clock_app::destroy()
{
    for (Digit &digit : s_digits) {
        cancel_animation(digit);
        digit = {};
    }
    for (lv_obj_t *&colon : s_colons) colon = nullptr;
    s_root = nullptr;
    s_has_time = false;
    s_backgrounded = false;
    s_last_second_of_day = -1;
}

void clock_app::deactivate()
{
    s_backgrounded = true;
    for (Digit &digit : s_digits) cancel_animation(digit);
}
