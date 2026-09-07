#include "photos.h"

#include "board_config.h"
#include "services/jpeg_service.h"
#include "services/sd_service.h"
#include "ui/ui_components.h"

#include <Arduino.h>
#include <FS.h>
#include <JPEGDEC.h>
#include <SD_MMC.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr int photo_w = BOARD_LCD_H_RES;
constexpr int photo_h = BOARD_LCD_V_RES;
constexpr int swipe_minimum = 24;
constexpr const char *photo_dir = "/photo";
constexpr uint16_t fill_color = 0x18C3; /* 0x1A1A1A in RGB565 */

lv_image_dsc_t s_dsc = {};
uint16_t *s_pixels = nullptr;
char (*s_files)[sd_svc_name_max] = nullptr;
int s_count = 0;
int s_wanted = 0;
int s_shown = -1;

lv_obj_t *s_image = nullptr;
lv_obj_t *s_message = nullptr;
lv_obj_t *s_index = nullptr;

int s_draw_x = 0;
int s_draw_y = 0;
int32_t s_press_x = 0;
int32_t s_press_y = 0;

bool is_jpeg_name(const char *path)
{
    if (!path) return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t n = strlen(base);
    auto match = [](const char *text, const char *suffix) {
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
    };
    return match(base, ".jpg") || match(base, ".jpeg");
}

void set_message(const char *text)
{
    if (!s_message) return;
    if (!text) {
        lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(s_message, text);
    lv_obj_remove_flag(s_message, LV_OBJ_FLAG_HIDDEN);
}

void refresh_index()
{
    if (!s_index) return;
    if (s_count <= 0) {
        lv_obj_add_flag(s_index, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text_fmt(s_index, "%d / %d", s_wanted + 1, s_count);
    lv_obj_remove_flag(s_index, LV_OBJ_FLAG_HIDDEN);
}

int jpeg_draw(JPEGDRAW *draw)
{
    if (!s_pixels || !draw || !draw->pPixels) return 0;
    const int stride = draw->iWidth > 0 ? draw->iWidth : draw->iWidthUsed;
    if (stride <= 0) return 0;
    for (int row = 0; row < draw->iHeight; ++row) {
        int dy = draw->y + s_draw_y + row;
        if (dy < 0 || dy >= photo_h) continue;
        int src_x = 0;
        int dst_x = draw->x + s_draw_x;
        int width = draw->iWidthUsed > 0 ? draw->iWidthUsed : draw->iWidth;
        if (dst_x < 0) {
            src_x = -dst_x;
            width += dst_x;
            dst_x = 0;
        }
        if (dst_x + width > photo_w) width = photo_w - dst_x;
        if (width <= 0) continue;
        memcpy(s_pixels + dy * photo_w + dst_x, draw->pPixels + row * stride + src_x,
               (size_t)width * sizeof(uint16_t));
    }
    return 1;
}

void *jpeg_open(const char *filename, int32_t *size)
{
    static File file;
    file = SD_MMC.open(filename, FILE_READ);
    if (!file) return nullptr;
    if (size) *size = (int32_t)file.size();
    return &file;
}

void jpeg_close(void *handle)
{
    File *file = (File *)handle;
    if (file) file->close();
}

int32_t jpeg_read(JPEGFILE *file, uint8_t *buffer, int32_t length)
{
    File *handle = file ? (File *)file->fHandle : nullptr;
    if (!handle) return 0;
    return (int32_t)handle->read(buffer, length);
}

int32_t jpeg_seek(JPEGFILE *file, int32_t position)
{
    File *handle = file ? (File *)file->fHandle : nullptr;
    if (!handle) return 0;
    handle->seek((uint32_t)position);
    return position;
}

bool ensure_buffer()
{
    if (s_pixels) return true;
    s_pixels = (uint16_t *)heap_caps_malloc((size_t)photo_w * photo_h * sizeof(uint16_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pixels) {
        Serial.println("Photos buffer alloc failed");
        return false;
    }
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.flags = 0;
    s_dsc.header.w = photo_w;
    s_dsc.header.h = photo_h;
    s_dsc.header.stride = photo_w * (int)sizeof(uint16_t);
    s_dsc.data_size = (uint32_t)photo_w * photo_h * sizeof(uint16_t);
    s_dsc.data = (const uint8_t *)s_pixels;
    return true;
}

void fill_background()
{
    if (!s_pixels) return;
    for (int i = 0; i < photo_w * photo_h; ++i) s_pixels[i] = fill_color;
}

bool decode_photo(const char *path)
{
    if (!ensure_buffer() || !path) return false;
    fill_background();
    JPEGDEC *decoder = jpeg_svc_acquire();
    if (!decoder) {
        Serial.println("Photos decoder busy");
        return false;
    }
    if (!decoder->open(path, jpeg_open, jpeg_close, jpeg_read, jpeg_seek, jpeg_draw)) {
        Serial.printf("Photos open failed: %s\n", path);
        jpeg_svc_release(decoder);
        return false;
    }
    decoder->setPixelType(RGB565_LITTLE_ENDIAN);

    const int src_w = decoder->getWidth();
    const int src_h = decoder->getHeight();
    int scale = 0;
    int out_w = src_w;
    int out_h = src_h;
    if (out_w > photo_w || out_h > photo_h) {
        scale = JPEG_SCALE_HALF;
        out_w = src_w / 2;
        out_h = src_h / 2;
    }
    if (out_w > photo_w || out_h > photo_h) {
        scale = JPEG_SCALE_QUARTER;
        out_w = src_w / 4;
        out_h = src_h / 4;
    }
    if (out_w > photo_w || out_h > photo_h) {
        scale = JPEG_SCALE_EIGHTH;
        out_w = src_w / 8;
        out_h = src_h / 8;
    }
    s_draw_x = (photo_w - out_w) / 2;
    s_draw_y = (photo_h - out_h) / 2;

    const int ok = decoder->decode(0, 0, scale);
    decoder->close();
    jpeg_svc_release(decoder);
    if (!ok) {
        Serial.printf("Photos decode failed: %s (%dx%d)\n", path, src_w, src_h);
        fill_background();
        return false;
    }
    Serial.printf("Photos decoded %s (%dx%d -> %dx%d)\n", path, src_w, src_h, out_w, out_h);
    return true;
}

void show_photo(int index)
{
    if (index < 0 || index >= s_count) return;
    s_wanted = index;
    const bool ok = decode_photo(s_files[index]);
    s_shown = index;
    if (s_image) {
        lv_image_set_src(s_image, &s_dsc);
        lv_obj_remove_flag(s_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_image);
    }
    set_message(ok ? nullptr : "Can't decode");
    refresh_index();
}

void on_image_event(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point = {};
    if (indev) lv_indev_get_point(indev, &point);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_press_x = point.x;
        s_press_y = point.y;
        return;
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    if (s_count <= 1) return;
    int dx = point.x - s_press_x;
    int dy = point.y - s_press_y;
    if (abs(dx) < swipe_minimum || abs(dx) <= abs(dy)) return;
    if (dx < 0) s_wanted = (s_wanted + 1) % s_count;
    else s_wanted = (s_wanted + s_count - 1) % s_count;
    refresh_index();
}

} // namespace

void photos_app::create(const AppContext &context)
{
    s_count = 0;
    s_wanted = 0;
    s_shown = -1;
    s_files = (char (*)[sd_svc_name_max])heap_caps_calloc(
        sd_svc_list_max, sd_svc_name_max, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    s_image = lv_image_create(context.root);
    lv_obj_set_size(s_image, photo_w, photo_h);
    lv_obj_align(s_image, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_image, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_image, on_image_event, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_image, on_image_event, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_image, on_image_event, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_flag(s_image, LV_OBJ_FLAG_HIDDEN);

    s_message = lv_label_create(context.root);
    lv_obj_set_style_text_color(s_message, lv_color_hex(ui_components::color_muted), 0);
    lv_obj_set_style_text_font(s_message, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_message, 200);
    lv_label_set_long_mode(s_message, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_message);

    s_index = lv_label_create(context.root);
    lv_obj_set_style_text_color(s_index, lv_color_hex(ui_components::color_text), 0);
    lv_obj_set_style_text_font(s_index, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_opa(s_index, LV_OPA_80, 0);
    lv_obj_align(s_index, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_flag(s_index, LV_OBJ_FLAG_HIDDEN);

    ui_components::make_back_hint(context.root);

    if (!s_files) {
        set_message("Not enough memory");
        return;
    }

    if (!sd_svc_begin()) {
        set_message("No TF card");
        return;
    }

    s_count = sd_svc_list(photo_dir, s_files, sd_svc_list_max, is_jpeg_name);
    if (s_count <= 0) {
        set_message("No photos in /photo");
        return;
    }

    set_message(nullptr);
    refresh_index();
}

void photos_app::update()
{
    if (s_count <= 0 || s_wanted == s_shown) return;
    show_photo(s_wanted);
}

void photos_app::destroy()
{
    if (s_pixels) heap_caps_free(s_pixels);
    if (s_files) heap_caps_free(s_files);
    s_pixels = nullptr;
    s_files = nullptr;
    s_dsc = {};
    s_image = nullptr;
    s_message = nullptr;
    s_index = nullptr;
    s_count = 0;
    s_wanted = 0;
    s_shown = -1;
}
