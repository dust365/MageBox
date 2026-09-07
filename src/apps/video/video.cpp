#include "video.h"

#include "board_config.h"
#include "services/sd_service.h"
#include "services/video_service.h"
#include "ui/ui_components.h"

#include <Arduino.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr const char *video_dir = "/video";
constexpr uint32_t controls_timeout_ms = 2000;
constexpr uint32_t accent = 0xFF2D55;

char (*s_files)[sd_svc_name_max] = nullptr;
VideoFileInfo *s_file_info = nullptr;
int s_count = 0;
int s_index = -1;
bool s_on_player = false;
bool s_controls_visible = true;
uint32_t s_controls_activity_ms = 0;

lv_obj_t *s_library = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_empty = nullptr;
lv_obj_t *s_player = nullptr;
lv_obj_t *s_image = nullptr;
lv_obj_t *s_controls = nullptr;
lv_obj_t *s_play_button = nullptr;
lv_obj_t *s_title = nullptr;
lv_obj_t *s_message = nullptr;
lv_obj_t *s_progress = nullptr;
lv_obj_t *s_elapsed = nullptr;
lv_obj_t *s_duration = nullptr;
lv_image_dsc_t s_frame_dsc = {};
VideoPlayState s_applied_state = VideoPlayState::idle;
uint32_t s_applied_second = UINT32_MAX;
uint32_t s_applied_duration = UINT32_MAX;

const char *file_base(const char *path)
{
    const char *base = path ? strrchr(path, '/') : nullptr;
    return base ? base + 1 : (path ? path : "");
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

bool is_video_name(const char *path)
{
    const char *base = file_base(path);
    return base[0] != '.' && ends_with_ci(base, ".avi");
}

uint32_t next_codepoint(const char *text, uint32_t &offset)
{
    const uint8_t first = (uint8_t)text[offset++];
    if (first < 0x80) return first;
    uint8_t count = 0;
    uint32_t codepoint = 0;
    if ((first & 0xE0) == 0xC0) {
        count = 1;
        codepoint = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        count = 2;
        codepoint = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        count = 3;
        codepoint = first & 0x07;
    } else {
        return 0xFFFD;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t next = (uint8_t)text[offset];
        if ((next & 0xC0) != 0x80) return 0xFFFD;
        offset++;
        codepoint = (codepoint << 6) | (next & 0x3F);
    }
    return codepoint;
}

void display_name(const char *path, char *out, size_t capacity)
{
    if (!out || capacity == 0) return;
    char raw[sd_svc_name_max];
    strncpy(raw, file_base(path), sizeof(raw) - 1);
    raw[sizeof(raw) - 1] = 0;
    char *dot = strrchr(raw, '.');
    if (dot) *dot = 0;

    size_t written = 0;
    uint32_t offset = 0;
    while (raw[offset] && written + 1 < capacity) {
        uint32_t start = offset;
        uint32_t codepoint = next_codepoint(raw, offset);
        if (!codepoint || offset <= start) break;
        lv_font_glyph_dsc_t glyph = {};
        if (!lv_font_get_glyph_dsc(&lv_font_source_han_sans_sc_14_cjk, &glyph, codepoint, 0)) {
            out[written++] = '?';
            continue;
        }
        size_t bytes = offset - start;
        if (written + bytes >= capacity) break;
        memcpy(out + written, raw + start, bytes);
        written += bytes;
    }
    out[written] = 0;
    if (!out[0]) strncpy(out, "Video", capacity - 1);
    out[capacity - 1] = 0;
}

void format_time(uint32_t milliseconds, char *out, size_t capacity)
{
    uint32_t seconds = milliseconds / 1000;
    snprintf(out, capacity, "%lu:%02lu", (unsigned long)(seconds / 60),
             (unsigned long)(seconds % 60));
}

void set_message(const char *text)
{
    if (!s_message) return;
    if (!text || !text[0]) {
        lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(s_message, text);
    lv_obj_remove_flag(s_message, LV_OBJ_FLAG_HIDDEN);
}

void show_controls(bool show)
{
    s_controls_visible = show;
    if (!s_controls) return;
    if (show) {
        lv_obj_remove_flag(s_controls, LV_OBJ_FLAG_HIDDEN);
        s_controls_activity_ms = millis();
    } else {
        lv_obj_add_flag(s_controls, LV_OBJ_FLAG_HIDDEN);
    }
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

void refresh_play_icon(VideoPlayState state)
{
    if (!s_play_button) return;
    lv_obj_t *icon = lv_obj_get_child(s_play_button, 0);
    if (!icon) return;
    const char *symbol = state == VideoPlayState::playing ? LV_SYMBOL_PAUSE :
                         state == VideoPlayState::ended ? LV_SYMBOL_REFRESH : LV_SYMBOL_PLAY;
    if (strcmp(lv_label_get_text(icon), symbol) != 0) lv_label_set_text(icon, symbol);
}

void apply_status(const VideoStatus &status)
{
    const bool state_changed = status.state != s_applied_state;
    const uint32_t second = status.position_ms / 1000;
    if (state_changed) {
        refresh_play_icon(status.state);
        s_applied_state = status.state;
    }
    if (s_progress && second != s_applied_second) {
        int value = status.duration_ms ? (int)((uint64_t)status.position_ms * 1000 / status.duration_ms) : 0;
        if (value > 1000) value = 1000;
        lv_bar_set_value(s_progress, value, LV_ANIM_OFF);
        char time[12];
        if (s_elapsed) {
            format_time(status.position_ms, time, sizeof(time));
            lv_label_set_text(s_elapsed, time);
        }
        s_applied_second = second;
    }
    if (s_duration && status.duration_ms != s_applied_duration) {
        char time[12];
        format_time(status.duration_ms, time, sizeof(time));
        lv_label_set_text(s_duration, time);
        s_applied_duration = status.duration_ms;
    }
    if (state_changed) {
        if (status.state == VideoPlayState::loading && !video_svc_framebuffer()) set_message("Loading...");
        else if (status.state == VideoPlayState::error) set_message(status.error ? status.error : "Video error");
        else set_message(nullptr);
        if (status.state == VideoPlayState::paused || status.state == VideoPlayState::ended ||
            status.state == VideoPlayState::error) show_controls(true);
    }
}

void play_index(int index)
{
    if (index < 0 || index >= s_count) return;
    s_index = index;
    char title[48];
    display_name(s_files[index], title, sizeof(title));
    if (s_title) lv_label_set_text(s_title, title);
    if (s_image) lv_obj_add_flag(s_image, LV_OBJ_FLAG_HIDDEN);
    s_frame_dsc = {};
    set_page(true);
    show_controls(true);
    video_svc_play(s_files[index]);
    apply_status(video_svc_status());
}

void on_row(lv_event_t *event)
{
    lv_obj_t *row = (lv_obj_t *)lv_event_get_current_target(event);
    play_index((int)(intptr_t)lv_obj_get_user_data(row));
}

void fill_list()
{
    if (!s_list) return;
    lv_obj_clean(s_list);
    for (int i = 0; i < s_count; ++i) {
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 212, 50);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(ui_components::color_card), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED, nullptr);

        char name[48];
        display_name(s_files[i], name, sizeof(name));
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_width(label, 188);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, name);
        lv_obj_set_style_text_color(label, lv_color_hex(ui_components::color_text), 0);
        lv_obj_set_style_text_font(label, &lv_font_source_han_sans_sc_14_cjk, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, 6);

        char details[64];
        const VideoFileInfo &info = s_file_info[i];
        if (info.supported) {
            char duration[12];
            format_time(info.duration_ms, duration, sizeof(duration));
            if (info.has_audio) {
                snprintf(details, sizeof(details), "AVI MJPEG %ux%u %ufps %s +PCM",
                         info.width, info.height, info.fps, duration);
            } else {
                snprintf(details, sizeof(details), "AVI MJPEG %ux%u %ufps %s",
                         info.width, info.height, info.fps, duration);
            }
        } else if (info.has_audio && !info.audio_supported) {
            strncpy(details, "Unsupported audio codec", sizeof(details) - 1);
            details[sizeof(details) - 1] = 0;
        } else {
            strncpy(details, "Unsupported AVI", sizeof(details) - 1);
            details[sizeof(details) - 1] = 0;
        }
        lv_obj_t *meta = lv_label_create(row);
        lv_obj_set_width(meta, 188);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
        lv_label_set_text(meta, details);
        lv_obj_set_style_text_color(meta, lv_color_hex(ui_components::color_muted), 0);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_10, 0);
        lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 12, -6);
    }
}

void scan_library()
{
    video_svc_stop();
    s_count = 0;
    s_index = -1;
    if (!s_files || !s_file_info) {
        lv_label_set_text(s_empty, "Not enough memory");
        lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        fill_list();
        return;
    }
    if (!sd_svc_begin()) {
        lv_label_set_text(s_empty, "No TF card");
        lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        fill_list();
        return;
    }
    s_count = sd_svc_list(video_dir, s_files, sd_svc_list_max, is_video_name);
    for (int i = 0; i < s_count; ++i) {
        s_file_info[i] = {};
        video_svc_probe(s_files[i], &s_file_info[i]);
    }
    if (s_count <= 0) {
        lv_label_set_text(s_empty, "No videos in /video");
        lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    }
    fill_list();
}

void on_toggle(lv_event_t *)
{
    video_svc_toggle();
    show_controls(true);
    apply_status(video_svc_status());
}

void on_image_tap(lv_event_t *)
{
    show_controls(true);
}

void on_controls_tap(lv_event_t *)
{
    show_controls(false);
}

void on_player_back(lv_event_t *)
{
    if (s_image) lv_obj_add_flag(s_image, LV_OBJ_FLAG_HIDDEN);
    video_svc_stop();
    set_page(false);
    show_controls(false);
}

void create_library(lv_obj_t *root)
{
    s_library = lv_obj_create(root);
    lv_obj_remove_style_all(s_library);
    lv_obj_set_size(s_library, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_clear_flag(s_library, LV_OBJ_FLAG_SCROLLABLE);

    ui_components::add_app_header(s_library, "Video", accent);

    lv_obj_t *rescan = lv_obj_create(s_library);
    lv_obj_remove_style_all(rescan);
    lv_obj_set_size(rescan, 44, 44);
    lv_obj_align(rescan, LV_ALIGN_TOP_RIGHT, -2, 18);
    lv_obj_add_flag(rescan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rescan, [](lv_event_t *) { scan_library(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *rescan_icon = lv_label_create(rescan);
    lv_label_set_text(rescan_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rescan_icon, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(rescan_icon, &lv_font_montserrat_14, 0);
    lv_obj_center(rescan_icon);

    s_list = lv_obj_create(s_library);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, 220, 158);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(s_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(accent), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_70, LV_PART_SCROLLBAR);

    s_empty = lv_label_create(s_library);
    lv_obj_set_width(s_empty, 200);
    lv_label_set_long_mode(s_empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_empty, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_empty, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_empty);
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

    ui_components::make_back_hint(s_library);
}

void create_player(lv_obj_t *root)
{
    s_player = lv_obj_create(root);
    lv_obj_remove_style_all(s_player);
    lv_obj_set_size(s_player, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_player, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_player, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_player, LV_OBJ_FLAG_SCROLLABLE);

    s_image = lv_image_create(s_player);
    lv_obj_set_size(s_image, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_image, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_image, on_image_tap, LV_EVENT_CLICKED, nullptr);

    s_message = lv_label_create(s_player);
    lv_obj_set_width(s_message, 200);
    lv_label_set_long_mode(s_message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_message, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_message, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_message);
    lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);

    s_controls = lv_obj_create(s_player);
    lv_obj_remove_style_all(s_controls);
    lv_obj_set_size(s_controls, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_controls, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_controls, LV_OPA_30, 0);
    lv_obj_clear_flag(s_controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_controls, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_controls, on_controls_tap, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back = lv_obj_create(s_controls);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 48, 44);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_80, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_event_cb(back, on_player_back, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_18, 0);
    lv_obj_center(back_icon);

    s_title = lv_label_create(s_controls);
    lv_obj_set_width(s_title, 208);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 16);

    s_play_button = lv_obj_create(s_controls);
    lv_obj_remove_style_all(s_play_button);
    lv_obj_set_size(s_play_button, 58, 58);
    lv_obj_set_style_radius(s_play_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_play_button, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(s_play_button, LV_OPA_90, 0);
    lv_obj_add_flag(s_play_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_play_button, LV_ALIGN_CENTER, 0, -4);
    lv_obj_add_event_cb(s_play_button, on_toggle, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *play_icon = lv_label_create(s_play_button);
    lv_label_set_text(play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(play_icon, &lv_font_montserrat_20, 0);
    lv_obj_center(play_icon);

    s_progress = lv_bar_create(s_controls);
    lv_obj_set_size(s_progress, 208, 5);
    lv_bar_set_range(s_progress, 0, 1000);
    lv_obj_set_style_radius(s_progress, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(0x636366), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progress, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_align(s_progress, LV_ALIGN_BOTTOM_MID, 0, -29);

    s_elapsed = lv_label_create(s_controls);
    lv_obj_set_style_text_color(s_elapsed, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_elapsed, &lv_font_montserrat_10, 0);
    lv_obj_align(s_elapsed, LV_ALIGN_BOTTOM_LEFT, 16, -12);

    s_duration = lv_label_create(s_controls);
    lv_obj_set_style_text_color(s_duration, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_duration, &lv_font_montserrat_10, 0);
    lv_obj_align(s_duration, LV_ALIGN_BOTTOM_RIGHT, -16, -12);

    ui_components::make_back_hint(s_controls);
    lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

void video_app::create(const AppContext &context)
{
    s_count = 0;
    s_index = -1;
    s_on_player = false;
    s_frame_dsc = {};
    s_applied_state = VideoPlayState::idle;
    s_applied_second = UINT32_MAX;
    s_applied_duration = UINT32_MAX;
    s_files = (char (*)[sd_svc_name_max])heap_caps_calloc(
        sd_svc_list_max, sd_svc_name_max, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_file_info = (VideoFileInfo *)heap_caps_calloc(
        sd_svc_list_max, sizeof(VideoFileInfo), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    create_library(context.root);
    create_player(context.root);
    scan_library();
}

void video_app::update()
{
    if (!s_on_player) return;
    video_svc_poll();
    if (video_svc_take_frame()) {
        const uint16_t *frame = video_svc_framebuffer();
        if (frame && s_image) {
            s_frame_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            s_frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            s_frame_dsc.header.flags = 0;
            s_frame_dsc.header.w = BOARD_LCD_H_RES;
            s_frame_dsc.header.h = BOARD_LCD_V_RES;
            s_frame_dsc.header.stride = BOARD_LCD_H_RES * sizeof(uint16_t);
            s_frame_dsc.data_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t);
            s_frame_dsc.data = (const uint8_t *)frame;
            lv_image_set_src(s_image, &s_frame_dsc);
            lv_obj_remove_flag(s_image, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(s_image);
        }
    }
    VideoStatus status = video_svc_status();
    apply_status(status);
    if (s_controls_visible && status.state == VideoPlayState::playing &&
        millis() - s_controls_activity_ms >= controls_timeout_ms) {
        show_controls(false);
    }
}

void video_app::destroy()
{
    video_svc_stop();
    if (s_files) heap_caps_free(s_files);
    if (s_file_info) heap_caps_free(s_file_info);
    s_files = nullptr;
    s_file_info = nullptr;
    s_library = nullptr;
    s_list = nullptr;
    s_empty = nullptr;
    s_player = nullptr;
    s_image = nullptr;
    s_controls = nullptr;
    s_play_button = nullptr;
    s_title = nullptr;
    s_message = nullptr;
    s_progress = nullptr;
    s_elapsed = nullptr;
    s_duration = nullptr;
    s_count = 0;
    s_index = -1;
    s_on_player = false;
    s_frame_dsc = {};
    s_applied_state = VideoPlayState::idle;
    s_applied_second = UINT32_MAX;
    s_applied_duration = UINT32_MAX;
}

void video_app::deactivate()
{
    video_svc_stop();
    set_page(false);
}
