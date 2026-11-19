#include "keyboard.h"

#include "keyboard_hid.h"
#include "ui/ui_components.h"

#include <Arduino.h>

extern "C" {
LV_FONT_DECLARE(font_keyboard)
LV_FONT_DECLARE(font_keyboard_14)
}

namespace {

constexpr uint32_t color_record = 0x0A84FF;
constexpr uint32_t color_record_pressed = 0x0060DF;
constexpr uint32_t color_delete = 0x3A3A3C;
constexpr uint32_t color_delete_pressed = 0x2C2C2E;
constexpr uint32_t color_send = 0x30D158;
constexpr uint32_t color_send_pressed = 0x248A3D;
constexpr uint32_t color_chip = 0x2C2C2E;
constexpr uint32_t color_chip_pressed = 0x48484A;
constexpr uint32_t color_done = 0x2C2C2E;
constexpr uint32_t color_dot_linked = 0x30D158;
constexpr uint32_t color_dot_waiting = 0x636366;
constexpr uint32_t color_ripple = 0xFFFFFF;

constexpr uint32_t kRepeatDelayMs = 350;
constexpr uint32_t kRepeatIntervalMs = 80;
constexpr uint32_t kRippleIntervalMs = 420;
constexpr int32_t kRippleDurationMs = 900;
constexpr int32_t kRippleMax = 190;

lv_obj_t *s_root = nullptr;
lv_obj_t *s_status_dot = nullptr;
lv_obj_t *s_status_label = nullptr;

// Main keypad group
lv_obj_t *s_record = nullptr;
lv_obj_t *s_record_label = nullptr;
lv_obj_t *s_erase = nullptr;
lv_obj_t *s_send = nullptr;

// Inline Mac Keyboard Setup group
lv_obj_t *s_setup_hint = nullptr;
lv_obj_t *s_setup_z_btn = nullptr;
lv_obj_t *s_setup_slash_btn = nullptr;
lv_obj_t *s_setup_sub = nullptr;

int s_last_linked = -1;
int s_last_setup_mode = -1;
bool s_delete_held = false;
uint32_t s_delete_at = 0;
bool s_delete_repeating = false;
bool s_setup_z_done = false;
bool s_setup_slash_done = false;
bool s_record_held = false;
uint32_t s_ripple_at = 0;

void set_visible(lv_obj_t *obj, bool visible)
{
    if (!obj) return;
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, uint32_t pressed,
                      const char *text, const lv_font_t *font)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, h > 60 ? 22 : (h > 40 ? 16 : 12), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_clip_corner(button, true, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_ext_click_area(button, 6);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return button;
}

void ripple_set_size(void *obj, int32_t value)
{
    auto *ripple = static_cast<lv_obj_t *>(obj);
    lv_obj_set_size(ripple, value, value);
    lv_obj_center(ripple);
}

void ripple_set_opa(void *obj, int32_t value)
{
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), 0);
}

void on_ripple_done(lv_anim_t *anim)
{
    if (anim && anim->var) lv_obj_delete(static_cast<lv_obj_t *>(anim->var));
}

void spawn_ripple()
{
    if (!s_record) return;
    lv_obj_t *ripple = lv_obj_create(s_record);
    lv_obj_remove_style_all(ripple);
    lv_obj_set_size(ripple, 24, 24);
    lv_obj_set_style_radius(ripple, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ripple, lv_color_hex(color_ripple), 0);
    lv_obj_set_style_bg_opa(ripple, LV_OPA_40, 0);
    lv_obj_center(ripple);
    lv_obj_remove_flag(ripple, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(ripple);
    if (s_record_label) lv_obj_move_foreground(s_record_label);

    lv_anim_t grow;
    lv_anim_init(&grow);
    lv_anim_set_var(&grow, ripple);
    lv_anim_set_values(&grow, 24, kRippleMax);
    lv_anim_set_duration(&grow, kRippleDurationMs);
    lv_anim_set_exec_cb(&grow, ripple_set_size);
    lv_anim_set_path_cb(&grow, lv_anim_path_ease_out);
    lv_anim_start(&grow);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, ripple);
    lv_anim_set_values(&fade, LV_OPA_40, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade, kRippleDurationMs);
    lv_anim_set_exec_cb(&fade, ripple_set_opa);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&fade, on_ripple_done);
    lv_anim_start(&fade);
}

void stop_ripples()
{
    s_record_held = false;
    if (!s_record) return;
    uint32_t count = lv_obj_get_child_count(s_record);
    for (uint32_t i = count; i > 0; --i) {
        lv_obj_t *child = lv_obj_get_child(s_record, i - 1);
        if (child == s_record_label) continue;
        lv_anim_delete(child, nullptr);
        lv_obj_delete(child);
    }
}

void reset_setup_buttons()
{
    s_setup_z_done = false;
    s_setup_slash_done = false;
    if (s_setup_z_btn) {
        lv_obj_set_style_bg_color(s_setup_z_btn, lv_color_hex(color_record), 0);
    }
    if (s_setup_slash_btn) {
        lv_obj_set_style_bg_color(s_setup_slash_btn, lv_color_hex(color_send), 0);
    }
}

void apply_mode(bool setup_mode)
{
    set_visible(s_record, !setup_mode);
    set_visible(s_erase, !setup_mode);
    set_visible(s_send, !setup_mode);

    set_visible(s_setup_hint, setup_mode);
    set_visible(s_setup_z_btn, setup_mode);
    set_visible(s_setup_slash_btn, setup_mode);
    set_visible(s_setup_sub, setup_mode);

    if (setup_mode) {
        s_delete_held = false;
        stop_ripples();
        reset_setup_buttons();
    }
}

void refresh()
{
    if (!s_status_label || !s_status_dot) return;

    const bool linked = keyboard_hid::secure();
    const bool setup_mode = linked && keyboard_hid::needs_setup();

    if (static_cast<int>(linked) != s_last_linked) {
        s_last_linked = linked ? 1 : 0;
        lv_label_set_text(s_status_label, linked ? "已连接" : "等待配对");
        lv_obj_set_style_text_color(
            s_status_label,
            lv_color_hex(linked ? ui_components::color_text : ui_components::color_muted), 0);
        lv_obj_set_style_bg_color(
            s_status_dot, lv_color_hex(linked ? color_dot_linked : color_dot_waiting), 0);
    }

    if (static_cast<int>(setup_mode) != s_last_setup_mode) {
        s_last_setup_mode = setup_mode ? 1 : 0;
        apply_mode(setup_mode);
    }
}

void on_record(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_record_held = true;
        s_ripple_at = millis();
        spawn_ripple();
        keyboard_hid::set_fn(true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        stop_ripples();
        keyboard_hid::set_fn(false);
    }
}

void on_delete(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_delete_held = true;
        s_delete_repeating = false;
        s_delete_at = millis();
        keyboard_hid::set_backspace(true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_delete_held = false;
        keyboard_hid::set_backspace(false);
    }
}

void on_send(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    keyboard_hid::tap(keyboard_hid::key_enter);
}

void on_setup_key(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (!keyboard_hid::secure()) return;

    auto usage = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    keyboard_hid::tap(usage);

    if (usage == keyboard_hid::key_z) {
        s_setup_z_done = true;
        if (s_setup_z_btn) {
            lv_obj_set_style_bg_color(s_setup_z_btn, lv_color_hex(color_done), 0);
        }
    } else if (usage == keyboard_hid::key_slash) {
        s_setup_slash_done = true;
        if (s_setup_slash_btn) {
            lv_obj_set_style_bg_color(s_setup_slash_btn, lv_color_hex(color_done), 0);
        }
    }

    if (s_setup_z_done && s_setup_slash_done) {
        keyboard_hid::finish_setup();
        refresh();
    }
}

void on_forget(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    s_delete_held = false;
    stop_ripples();
    reset_setup_buttons();
    keyboard_hid::forget();
    s_last_linked = -1;
    s_last_setup_mode = -1;
    refresh();
}

} // namespace

void keyboard_app::create(const AppContext &context)
{
    s_root = context.root;
    s_delete_held = false;
    s_delete_repeating = false;
    s_setup_z_done = false;
    s_setup_slash_done = false;
    s_record_held = false;
    s_last_linked = -1;
    s_last_setup_mode = -1;
    keyboard_hid::start();

    // Top bar: left status indicator + right single "重新配对" button
    s_status_dot = lv_obj_create(context.root);
    lv_obj_remove_style_all(s_status_dot);
    lv_obj_set_size(s_status_dot, 8, 8);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(color_dot_waiting), 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_status_dot, 16, 26);
    lv_obj_remove_flag(s_status_dot, LV_OBJ_FLAG_CLICKABLE);

    s_status_label = lv_label_create(context.root);
    lv_obj_set_style_text_font(s_status_label, &font_keyboard_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_pos(s_status_label, 30, 21);
    lv_obj_remove_flag(s_status_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *forget = make_button(context.root, 128, 12, 100, 34, color_chip, color_chip_pressed,
                                   "重新配对", &font_keyboard_14);
    lv_obj_set_ext_click_area(forget, 10);
    lv_obj_add_event_cb(forget, on_forget, LV_EVENT_CLICKED, nullptr);

    // Normal mode keypad: Record (Fn), Delete (Backspace), Send (Enter)
    s_record = make_button(context.root, 12, 56, 216, 100, color_record, color_record_pressed,
                           "录音", &font_keyboard);
    s_record_label = lv_obj_get_child(s_record, 0);
    lv_obj_add_event_cb(s_record, on_record, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_record, on_record, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_record, on_record, LV_EVENT_PRESS_LOST, nullptr);

    s_erase = make_button(context.root, 12, 166, 104, 52, color_delete, color_delete_pressed,
                          "删除", &font_keyboard);
    lv_obj_add_event_cb(s_erase, on_delete, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_erase, on_delete, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_erase, on_delete, LV_EVENT_PRESS_LOST, nullptr);

    s_send = make_button(context.root, 124, 166, 104, 52, color_send, color_send_pressed, "发送",
                         &font_keyboard);
    lv_obj_add_event_cb(s_send, on_send, LV_EVENT_CLICKED, nullptr);

    // Inline Mac Keyboard Setup controls (only shown right after a fresh bond connects)
    s_setup_hint = lv_label_create(context.root);
    lv_label_set_text(s_setup_hint, "Mac Keyboard Setup");
    lv_obj_set_style_text_font(s_setup_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_setup_hint, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_align(s_setup_hint, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_remove_flag(s_setup_hint, LV_OBJ_FLAG_CLICKABLE);

    s_setup_z_btn = make_button(context.root, 16, 84, 100, 92, color_record, color_record_pressed,
                                "Z", &lv_font_montserrat_40);
    lv_obj_add_event_cb(s_setup_z_btn, on_setup_key, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(keyboard_hid::key_z)));

    s_setup_slash_btn = make_button(context.root, 124, 84, 100, 92, color_send, color_send_pressed,
                                    "/", &lv_font_montserrat_40);
    lv_obj_add_event_cb(s_setup_slash_btn, on_setup_key, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(keyboard_hid::key_slash)));

    s_setup_sub = lv_label_create(context.root);
    lv_label_set_text(s_setup_sub, "Tap Z and / once");
    lv_obj_set_style_text_font(s_setup_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_setup_sub, lv_color_hex(ui_components::color_text), 0);
    lv_obj_align(s_setup_sub, LV_ALIGN_TOP_MID, 0, 188);
    lv_obj_remove_flag(s_setup_sub, LV_OBJ_FLAG_CLICKABLE);

    refresh();
}

void keyboard_app::update()
{
    if (!s_root || lv_screen_active() != s_root) return;
    keyboard_hid::start();
    keyboard_hid::poll();
    if (s_record_held && millis() - s_ripple_at >= kRippleIntervalMs) {
        s_ripple_at = millis();
        spawn_ripple();
    }
    if (s_delete_held) {
        uint32_t now = millis();
        uint32_t wait = s_delete_repeating ? kRepeatIntervalMs : kRepeatDelayMs;
        if (now - s_delete_at >= wait) {
            s_delete_repeating = true;
            s_delete_at = now;
            keyboard_hid::pulse_backspace();
        }
    }
    refresh();
}

void keyboard_app::destroy()
{
    s_delete_held = false;
    stop_ripples();
    keyboard_hid::end_session();
    s_root = nullptr;
    s_status_dot = nullptr;
    s_status_label = nullptr;
    s_record = nullptr;
    s_record_label = nullptr;
    s_erase = nullptr;
    s_send = nullptr;
    s_setup_hint = nullptr;
    s_setup_z_btn = nullptr;
    s_setup_slash_btn = nullptr;
    s_setup_sub = nullptr;
    s_last_linked = -1;
    s_last_setup_mode = -1;
}

void keyboard_app::deactivate()
{
    s_delete_held = false;
    stop_ripples();
    keyboard_hid::set_fn(false);
    keyboard_hid::set_backspace(false);
    keyboard_hid::stop();
}
