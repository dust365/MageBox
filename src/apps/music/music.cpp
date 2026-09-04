#include "music.h"

#include "board_config.h"
#include "services/audio_service.h"
#include "services/sd_service.h"
#include "ui/audio_player_view.h"
#include "ui/ui_components.h"

#include <Arduino.h>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char *music_dir = "/music";
constexpr uint32_t restart_after_sec = 3;

char s_files[sd_svc_list_max][sd_svc_name_max] = {};
int s_count = 0;
int s_index = -1;
bool s_on_player = false;
char s_empty_text[32] = {};

lv_obj_t *s_library = nullptr;
lv_obj_t *s_player = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_empty = nullptr;
lv_obj_t *s_mini = nullptr;
lv_obj_t *s_mini_title = nullptr;
lv_obj_t *s_mini_play = nullptr;
lv_obj_t *s_current_mark = nullptr;

const char *file_base(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

bool ends_with_ci(const char *text, const char *suffix)
{
    if (!text || !suffix) return false;
    size_t text_n = strlen(text);
    size_t suffix_n = strlen(suffix);
    if (text_n < suffix_n) return false;
    text += text_n - suffix_n;
    while (*suffix) {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*suffix)) return false;
        text++;
        suffix++;
    }
    return true;
}

bool is_music_name(const char *path)
{
    const char *base = file_base(path);
    return ends_with_ci(base, ".mp3") || ends_with_ci(base, ".wav");
}

void display_name(const char *path, char *out, size_t n)
{
    const char *base = file_base(path);
    strncpy(out, base, n - 1);
    out[n - 1] = 0;
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

void set_page(bool player)
{
    s_on_player = player;
    if (s_library) {
        if (player) lv_obj_add_flag(s_library, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_library, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_player) {
        if (player) lv_obj_remove_flag(s_player, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);
    }
}

void mark_current()
{
    if (s_current_mark) {
        lv_obj_add_flag(s_current_mark, LV_OBJ_FLAG_HIDDEN);
        s_current_mark = nullptr;
    }
    if (!s_list || s_index < 0) return;
    uint32_t count = lv_obj_get_child_count(s_list);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t *row = lv_obj_get_child(s_list, i);
        if (!row) continue;
        int index = (int)(intptr_t)lv_obj_get_user_data(row);
        lv_obj_t *dot = lv_obj_get_child(row, 1);
        if (!dot) continue;
        if (index == s_index) {
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            s_current_mark = dot;
        } else {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void update_mini(const AudioStatus &status)
{
    if (!s_mini || !s_mini_title || !s_mini_play) return;
    if (s_index < 0 || s_index >= s_count) {
        lv_obj_add_flag(s_mini, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_mini, LV_OBJ_FLAG_HIDDEN);
    char name[48];
    if (status.title[0]) strncpy(name, status.title, sizeof(name) - 1);
    else display_name(s_files[s_index], name, sizeof(name));
    name[sizeof(name) - 1] = 0;
    if (strcmp(lv_label_get_text(s_mini_title), name) != 0) {
        lv_label_set_text(s_mini_title, name);
    }
    lv_obj_t *icon = lv_obj_get_child(s_mini_play, 0);
    if (icon) {
        const char *symbol = status.state == AudioPlayState::playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
        if (strcmp(lv_label_get_text(icon), symbol) != 0) lv_label_set_text(icon, symbol);
    }
}

void apply_player(const AudioStatus &status)
{
    if (!s_on_player) return;
    AudioPlayerViewState view;
    char song[64];
    if (status.title[0]) strncpy(song, status.title, sizeof(song) - 1);
    else if (s_index >= 0) display_name(s_files[s_index], song, sizeof(song));
    else strncpy(song, "Music", sizeof(song) - 1);
    song[sizeof(song) - 1] = 0;

    view.title = song;
    switch (status.state) {
    case AudioPlayState::loading: view.state = AudioUiState::loading; break;
    case AudioPlayState::playing: view.state = AudioUiState::playing; break;
    case AudioPlayState::paused: view.state = AudioUiState::paused; break;
    case AudioPlayState::error: view.state = AudioUiState::error; break;
    default: view.state = AudioUiState::idle; break;
    }
    view.position_sec = status.position_sec;
    view.duration_sec = status.duration_sec;
    view.seekable = status.state == AudioPlayState::playing || status.state == AudioPlayState::paused;
    view.has_prev = s_index > 0;
    view.has_next = s_index >= 0 && s_index + 1 < s_count;
    view.vu_left = status.vu_left;
    view.vu_right = status.vu_right;
    audio_player_view_apply(view);
}

void play_index(int index)
{
    if (index < 0 || index >= s_count) return;
    s_index = index;
    mark_current();
    if (!audio_svc_play(s_files[index])) {
        AudioStatus status = audio_svc_status();
        update_mini(status);
        apply_player(status);
        return;
    }
    set_page(true);
    AudioStatus status = audio_svc_status();
    update_mini(status);
    apply_player(status);
}

void on_prev()
{
    AudioStatus status = audio_svc_status();
    if (status.position_sec > restart_after_sec) {
        audio_svc_seek(0);
        return;
    }
    if (s_index > 0) play_index(s_index - 1);
    else if (s_index == 0) audio_svc_seek(0);
}

void on_next()
{
    if (s_index >= 0 && s_index + 1 < s_count) play_index(s_index + 1);
}

void on_toggle()
{
    AudioStatus status = audio_svc_status();
    if (status.state == AudioPlayState::playing || status.state == AudioPlayState::paused ||
        status.state == AudioPlayState::loading) {
        audio_svc_toggle();
        return;
    }
    if (s_index >= 0) play_index(s_index);
}

void on_seek(uint32_t sec)
{
    audio_svc_seek(sec);
}

void on_back()
{
    set_page(false);
}

void on_row(lv_event_t *event)
{
    lv_obj_t *row = (lv_obj_t *)lv_event_get_current_target(event);
    int index = (int)(intptr_t)lv_obj_get_user_data(row);
    play_index(index);
}

void fill_list()
{
    if (!s_list) return;
    lv_obj_clean(s_list);
    s_current_mark = nullptr;
    for (int i = 0; i < s_count; ++i) {
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 212, 36);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(ui_components::color_card), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 12, 0);
        lv_obj_set_style_pad_right(row, 10, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED, nullptr);

        char name[48];
        display_name(s_files[i], name, sizeof(name));
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_width(label, 168);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, name);
        lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_text), 0);
        lv_obj_set_style_text_font(label, &lv_font_source_han_sans_sc_14_cjk, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x5856D6), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
    mark_current();
}

void set_empty(const char *text)
{
    if (!s_empty) return;
    if (!text) {
        s_empty_text[0] = 0;
        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    strncpy(s_empty_text, text, sizeof(s_empty_text) - 1);
    s_empty_text[sizeof(s_empty_text) - 1] = 0;
    lv_label_set_text(s_empty, s_empty_text);
    lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
}

void scan_library()
{
    s_count = 0;
    if (!sd_svc_begin()) {
        set_empty("No TF card");
        fill_list();
        return;
    }
    s_count = sd_svc_list(music_dir, s_files, sd_svc_list_max, is_music_name);
    if (s_count <= 0) {
        set_empty("No music");
        s_index = -1;
        fill_list();
        return;
    }
    if (s_index >= s_count) s_index = -1;
    set_empty(nullptr);
    fill_list();
}

void on_rescan(lv_event_t *)
{
    scan_library();
    update_mini(audio_svc_status());
}

void on_mini(lv_event_t *)
{
    if (s_index >= 0) set_page(true);
}

void on_mini_play(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    on_toggle();
}

} // namespace

void music_app::create(const AppContext &context)
{
    s_count = 0;
    s_index = -1;
    s_on_player = false;
    s_current_mark = nullptr;
    audio_svc_begin();

    s_library = lv_obj_create(context.root);
    lv_obj_remove_style_all(s_library);
    lv_obj_set_size(s_library, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_clear_flag(s_library, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_library, LV_ALIGN_TOP_LEFT, 0, 0);

    ui_components::add_app_header(s_library, "Music", 0x5856D6);

    lv_obj_t *rescan = lv_obj_create(s_library);
    lv_obj_remove_style_all(rescan);
    lv_obj_set_size(rescan, 40, 40);
    lv_obj_align(rescan, LV_ALIGN_TOP_RIGHT, -4, 20);
    lv_obj_add_flag(rescan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rescan, on_rescan, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *rescan_label = lv_label_create(rescan);
    lv_label_set_text(rescan_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rescan_label, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(rescan_label, &lv_font_montserrat_14, 0);
    lv_obj_center(rescan_label);

    s_list = lv_obj_create(s_library);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, 220, 126);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(s_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x5856D6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_70, LV_PART_SCROLLBAR);

    s_empty = lv_label_create(s_library);
    lv_obj_set_width(s_empty, 200);
    lv_label_set_long_mode(s_empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_empty, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_empty, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_empty, LV_ALIGN_CENTER, 0, -8);
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

    s_mini = lv_obj_create(s_library);
    lv_obj_remove_style_all(s_mini);
    lv_obj_set_size(s_mini, 220, 40);
    lv_obj_align(s_mini, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_radius(s_mini, 12, 0);
    lv_obj_set_style_bg_color(s_mini, lv_color_hex(ui_components::color_card), 0);
    lv_obj_set_style_bg_opa(s_mini, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_mini, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_mini, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_mini, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_mini, on_mini, LV_EVENT_CLICKED, nullptr);

    s_mini_title = lv_label_create(s_mini);
    lv_obj_set_width(s_mini_title, 148);
    lv_label_set_long_mode(s_mini_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_mini_title, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(s_mini_title, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_align(s_mini_title, LV_ALIGN_LEFT_MID, 12, 0);
    lv_label_set_text(s_mini_title, "");

    s_mini_play = lv_obj_create(s_mini);
    lv_obj_remove_style_all(s_mini_play);
    lv_obj_set_size(s_mini_play, 40, 40);
    lv_obj_align(s_mini_play, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(s_mini_play, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_mini_play, on_mini_play, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *mini_icon = lv_label_create(s_mini_play);
    lv_label_set_text(mini_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(mini_icon, lv_color_hex(0x5856D6), 0);
    lv_obj_set_style_text_font(mini_icon, &lv_font_montserrat_14, 0);
    lv_obj_center(mini_icon);

    AudioPlayerViewCallbacks callbacks = {
        .toggle = on_toggle,
        .prev = on_prev,
        .next = on_next,
        .seek = on_seek,
        .back = on_back,
    };
    s_player = audio_player_view_create(context.root, callbacks);
    lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);

    scan_library();
}

void music_app::update()
{
    if (audio_svc_take_eof()) {
        if (s_index >= 0 && s_index + 1 < s_count) play_index(s_index + 1);
        else audio_svc_stop();
    }
    AudioStatus status = audio_svc_status();
    update_mini(status);
    apply_player(status);
}

void music_app::destroy()
{
    audio_svc_stop();
    audio_player_view_reset();
    s_library = nullptr;
    s_player = nullptr;
    s_list = nullptr;
    s_empty = nullptr;
    s_mini = nullptr;
    s_mini_title = nullptr;
    s_mini_play = nullptr;
    s_current_mark = nullptr;
    s_count = 0;
    s_index = -1;
    s_on_player = false;
}

void music_app::deactivate()
{
    audio_svc_stop();
    AudioStatus status = audio_svc_status();
    update_mini(status);
    apply_player(status);
}
