#include "audio_player_view.h"

#include "assets/assets.h"
#include "board_config.h"
#include "ui/ui_components.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t color_vinyl = 0x111113;
constexpr uint32_t color_groove = 0x2A2A2E;
constexpr uint32_t color_label = 0xC7A27A;
constexpr uint32_t color_needle = 0xD8D8DE;
constexpr uint32_t color_accent = 0x5856D6;
constexpr int disc_size = 78;
constexpr int anim_ms = 50;
constexpr int spin_step = 8;

AudioPlayerViewCallbacks s_cb;
lv_obj_t *s_root = nullptr;
lv_obj_t *s_disc = nullptr;
lv_obj_t *s_vu_left = nullptr;
lv_obj_t *s_vu_right = nullptr;
lv_obj_t *s_needle = nullptr;
lv_obj_t *s_title = nullptr;
lv_obj_t *s_elapsed = nullptr;
lv_obj_t *s_total = nullptr;
lv_obj_t *s_slider = nullptr;
lv_obj_t *s_prev = nullptr;
lv_obj_t *s_play = nullptr;
lv_obj_t *s_next = nullptr;
int16_t s_angle = 0;
uint32_t s_anim_at = 0;
bool s_dragging = false;
uint32_t s_duration = 0;
char s_last_title[96] = {};
uint32_t s_last_pos = 0xFFFFFFFF;
uint32_t s_last_dur = 0xFFFFFFFF;
AudioUiState s_last_state = AudioUiState::idle;
bool s_last_prev = false;
bool s_last_next = false;
bool s_last_seekable = false;

lv_obj_t *make_circle(lv_obj_t *parent, int size, uint32_t color, uint8_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    return obj;
}

lv_obj_t *make_icon_button(lv_obj_t *parent, const char *symbol, int visual_size,
                           uint32_t color, void (*cb)(lv_event_t *))
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 80, 72);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *visual = make_circle(btn, visual_size, color, LV_OPA_COVER);
    lv_obj_t *label = lv_label_create(visual);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *button_label(lv_obj_t *button)
{
    if (!button) return nullptr;
    lv_obj_t *visual = lv_obj_get_child(button, 0);
    return visual ? lv_obj_get_child(visual, 0) : nullptr;
}

void format_time(char *out, size_t n, uint32_t sec)
{
    snprintf(out, n, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}

void on_toggle(lv_event_t *)
{
    if (s_cb.toggle) s_cb.toggle();
}

void on_prev(lv_event_t *)
{
    if (s_cb.prev) s_cb.prev();
}

void on_next(lv_event_t *)
{
    if (s_cb.next) s_cb.next();
}

void on_slider(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_dragging = true;
        return;
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    s_dragging = false;
    if (!s_cb.seek || !s_slider) return;
    int32_t value = lv_slider_get_value(s_slider);
    if (value < 0) value = 0;
    s_cb.seek((uint32_t)value);
}

void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (!obj) return;
    if (enabled) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
    lv_obj_set_style_opa(obj, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
}

void apply_needle(uint32_t position, uint32_t duration)
{
    if (!s_needle) return;
    int progress = 0;
    if (duration > 0) progress = (int)(position * 100 / duration);
    if (progress > 100) progress = 100;
    int16_t angle = (int16_t)(-220 + progress * 6);
    lv_obj_set_style_transform_rotation(s_needle, angle, 0);
}

} // namespace

lv_obj_t *audio_player_view_create(lv_obj_t *parent, const AudioPlayerViewCallbacks &callbacks)
{
    s_cb = callbacks;
    s_angle = 0;
    s_dragging = false;
    s_duration = 0;
    s_anim_at = 0;

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x101012), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_root, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *stage = lv_obj_create(s_root);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, 126, 106);
    lv_obj_align(stage, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(stage, LV_OBJ_FLAG_CLICKABLE);

    s_vu_left = make_circle(stage, 92, 0x0A84FF, LV_OPA_20);
    lv_obj_align(s_vu_left, LV_ALIGN_CENTER, -2, 2);
    s_vu_right = make_circle(stage, 92, 0xFF375F, LV_OPA_20);
    lv_obj_align(s_vu_right, LV_ALIGN_CENTER, 2, 2);

    s_disc = lv_obj_create(stage);
    lv_obj_remove_style_all(s_disc);
    lv_obj_set_size(s_disc, disc_size, disc_size);
    lv_obj_set_style_radius(s_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_disc, lv_color_hex(color_vinyl), 0);
    lv_obj_set_style_bg_opa(s_disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_disc, 2, 0);
    lv_obj_set_style_border_color(s_disc, lv_color_hex(0x3A3A40), 0);
    lv_obj_set_style_transform_pivot_x(s_disc, disc_size / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_disc, disc_size / 2, 0);
    lv_obj_clear_flag(s_disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_disc);

    int grooves[] = {64, 52, 40};
    for (int size : grooves) {
        lv_obj_t *ring = make_circle(s_disc, size, color_groove, LV_OPA_40);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(color_groove), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_50, 0);
    }

    lv_obj_t *label = make_circle(s_disc, 28, color_label, LV_OPA_COVER);
    lv_obj_t *icon = lv_image_create(label);
    lv_image_set_src(icon, &img_app_music);
    lv_obj_set_size(icon, 16, 16);
    lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_center(icon);

    s_needle = lv_obj_create(stage);
    lv_obj_remove_style_all(s_needle);
    lv_obj_set_size(s_needle, 4, 44);
    lv_obj_set_style_radius(s_needle, 2, 0);
    lv_obj_set_style_bg_color(s_needle, lv_color_hex(color_needle), 0);
    lv_obj_set_style_bg_opa(s_needle, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(s_needle, 2, 0);
    lv_obj_set_style_transform_pivot_y(s_needle, 4, 0);
    lv_obj_align(s_needle, LV_ALIGN_TOP_RIGHT, -8, 5);
    lv_obj_clear_flag(s_needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_needle, LV_OBJ_FLAG_SCROLLABLE);
    apply_needle(0, 0);

    lv_obj_t *hub = make_circle(stage, 10, 0x8E8E93, LV_OPA_COVER);
    lv_obj_align(hub, LV_ALIGN_TOP_RIGHT, -5, 3);

    s_title = lv_label_create(s_root);
    lv_obj_set_width(s_title, 224);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_title, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_title, "");
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 108);

    s_elapsed = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_elapsed, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_elapsed, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_elapsed, "0:00");
    lv_obj_set_width(s_elapsed, 34);
    lv_obj_set_style_text_align(s_elapsed, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_elapsed, LV_ALIGN_TOP_LEFT, 8, 136);

    s_total = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_total, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_total, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_total, "0:00");
    lv_obj_set_width(s_total, 34);
    lv_obj_set_style_text_align(s_total, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_total, LV_ALIGN_TOP_RIGHT, -8, 136);

    s_slider = lv_slider_create(s_root);
    lv_obj_set_size(s_slider, 148, 10);
    lv_obj_align(s_slider, LV_ALIGN_TOP_MID, 0, 137);
    lv_slider_set_range(s_slider, 0, 1);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(color_accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider, on_slider, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_slider, on_slider, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_slider, on_slider, LV_EVENT_PRESS_LOST, nullptr);

    s_prev = make_icon_button(s_root, LV_SYMBOL_PREV, 52, ui_components::color_card, on_prev);
    s_play = make_icon_button(s_root, LV_SYMBOL_PLAY, 64, color_accent, on_toggle);
    s_next = make_icon_button(s_root, LV_SYMBOL_NEXT, 52, ui_components::color_card, on_next);
    lv_obj_set_pos(s_prev, 0, 158);
    lv_obj_set_pos(s_play, 80, 158);
    lv_obj_set_pos(s_next, 160, 158);
    set_enabled(s_prev, false);
    set_enabled(s_next, false);

    return s_root;
}

void audio_player_view_apply(const AudioPlayerViewState &state)
{
    if (!s_root) return;

    const char *title = state.title ? state.title : "";
    if (s_title && strcmp(s_last_title, title) != 0) {
        strncpy(s_last_title, title, sizeof(s_last_title) - 1);
        s_last_title[sizeof(s_last_title) - 1] = 0;
        lv_label_set_text(s_title, title);
    }
    const bool time_changed = state.position_sec != s_last_pos || state.duration_sec != s_last_dur;
    if (time_changed) {
        char time[8];
        format_time(time, sizeof(time), state.position_sec);
        if (s_elapsed) lv_label_set_text(s_elapsed, time);
        format_time(time, sizeof(time), state.duration_sec);
        if (s_total) lv_label_set_text(s_total, time);
        apply_needle(state.position_sec, state.duration_sec);
    }

    s_duration = state.duration_sec;
    if (s_slider && !s_dragging && (time_changed || state.seekable != s_last_seekable)) {
        uint32_t max = state.duration_sec > 0 ? state.duration_sec : 1;
        lv_slider_set_range(s_slider, 0, (int32_t)max);
        uint32_t pos = state.position_sec > max ? max : state.position_sec;
        lv_slider_set_value(s_slider, (int32_t)pos, LV_ANIM_OFF);
        if (state.seekable) lv_obj_remove_state(s_slider, LV_STATE_DISABLED);
        else lv_obj_add_state(s_slider, LV_STATE_DISABLED);
        s_last_seekable = state.seekable;
    }
    s_last_pos = state.position_sec;
    s_last_dur = state.duration_sec;

    if (s_play && state.state != s_last_state) {
        lv_obj_t *label = button_label(s_play);
        if (label) {
            lv_label_set_text(label, state.state == AudioUiState::playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        }
    }
    if (state.has_prev != s_last_prev) {
        set_enabled(s_prev, state.has_prev);
        s_last_prev = state.has_prev;
    }
    if (state.has_next != s_last_next) {
        set_enabled(s_next, state.has_next);
        s_last_next = state.has_next;
    }
    const bool was_playing = s_last_state == AudioUiState::playing;
    s_last_state = state.state;

    const bool spinning = state.state == AudioUiState::playing;
    uint32_t now = millis();
    if (!spinning) {
        if (was_playing) {
            if (s_vu_left) lv_obj_set_style_bg_opa(s_vu_left, LV_OPA_10, 0);
            if (s_vu_right) lv_obj_set_style_bg_opa(s_vu_right, LV_OPA_10, 0);
        }
        return;
    }
    if ((now - s_anim_at) < anim_ms) return;
    s_anim_at = now;
    if (s_vu_left) lv_obj_set_style_bg_opa(s_vu_left, LV_OPA_10 + state.vu_left, 0);
    if (s_vu_right) lv_obj_set_style_bg_opa(s_vu_right, LV_OPA_10 + state.vu_right, 0);
    if (s_disc) {
        s_angle = (int16_t)((s_angle + spin_step) % 3600);
        lv_obj_set_style_transform_rotation(s_disc, s_angle, 0);
    }
}

void audio_player_view_reset()
{
    s_root = nullptr;
    s_disc = nullptr;
    s_vu_left = nullptr;
    s_vu_right = nullptr;
    s_needle = nullptr;
    s_title = nullptr;
    s_elapsed = nullptr;
    s_total = nullptr;
    s_slider = nullptr;
    s_prev = nullptr;
    s_play = nullptr;
    s_next = nullptr;
    s_cb = {};
    s_angle = 0;
    s_dragging = false;
    s_duration = 0;
    s_last_title[0] = 0;
    s_last_pos = 0xFFFFFFFF;
    s_last_dur = 0xFFFFFFFF;
    s_last_state = AudioUiState::idle;
    s_last_prev = false;
    s_last_next = false;
    s_last_seekable = false;
}
