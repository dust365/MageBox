#include "hal.h"

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TouchDrvCSTXXX.hpp>
#include <SensorQMI8658.hpp>
#include <math.h>

/* Keep the ST7789 in its native orientation. The Waveshare factory example
 * uses software rotation because hardware MADCTL rotation can address the
 * hidden 80 rows of this controller's 240x320 GRAM. */
class LcdST7789 : public Arduino_ST7789 {
public:
    LcdST7789(Arduino_DataBus *bus, int8_t rst)
        : Arduino_ST7789(bus, rst, 0, true, BOARD_LCD_H_RES, BOARD_LCD_V_RES)
    {
    }

    void setRotation(uint8_t r) override
    {
        Arduino_ST7789::setRotation(r);
        _xStart = 0;
        _yStart = 0;
        _currentX = 0xFFFF;
        _currentY = 0xFFFF;
        _currentW = 0xFFFF;
        _currentH = 0xFFFF;
    }

    void clearGram()
    {
        _bus->beginWrite();
        _bus->writeC8D16D16(0x2A, 0, 239);
        _bus->writeC8D16D16(0x2B, 0, 319);
        _bus->writeCommand(0x2C);
        _bus->writeRepeat(0x0000, 240u * 320u);
        _bus->endWrite();
        _currentX = 0xFFFF;
        _currentY = 0xFFFF;
        _currentW = 0xFFFF;
        _currentH = 0xFFFF;
    }
};

static Arduino_DataBus *s_bus = nullptr;
static LcdST7789 *s_panel = nullptr;
static Arduino_GFX *s_gfx = nullptr;
static TouchDrvCSTXXX s_touch;
static SensorQMI8658 s_imu;

static lv_display_t *s_disp = nullptr;
static uint8_t s_brightness = LCD_BRIGHTNESS_DEFAULT;
static bool s_touch_ok = false;
static bool s_imu_ok = false;
static int s_rot = 0;
static bool s_autorotate = true;
static bool s_finger_down = false;
static float s_acc_x = 0;
static float s_acc_y = 0;
static float s_acc_z = 1;
static bool s_acc_ok = false;
static int s_rot_cand = 0;
static uint32_t s_rot_cand_ms = 0;
static uint32_t s_rot_last_ms = 0;
static uint16_t *s_rotate_buf = nullptr;

volatile bool g_touch_irq = false;

static constexpr int k_max = BOARD_LCD_H_RES - 1;
static constexpr uint32_t k_draw_buf_pixels = BOARD_LCD_H_RES * 40;

static void map_touch(int16_t *x, int16_t *y)
{
    int16_t nx = *x;
    int16_t ny = *y;
    switch (s_rot) {
    case 1:
        *x = ny;
        *y = k_max - nx;
        break;
    case 2:
        *x = k_max - nx;
        *y = k_max - ny;
        break;
    case 3:
        *x = k_max - ny;
        *y = nx;
        break;
    default:
        break;
    }
}

static void clamp_xy(int16_t *x, int16_t *y)
{
    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
    }
    if (*x > k_max) {
        *x = k_max;
    }
    if (*y > k_max) {
        *y = k_max;
    }
}

static float absf(float v)
{
    return v < 0 ? -v : v;
}

static int rotation_from_gravity(float ax, float ay, float az)
{
    /* Default pose (USB on the right, status on top): gravity is +ay.
     * Screen right = +ax, screen down = +ay.
     * Pixel rotation runs AGAINST the device turn so the UI stays upright. */
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (mag < 0.4f) {
        return -1;
    }
    if (absf(az) > 0.75f * mag) {
        return -1;
    }
    float ang = atan2f(ax, ay);
    const float near = 0.45f;
    auto wrap = [](float a) {
        while (a > (float)M_PI) {
            a -= 2.f * (float)M_PI;
        }
        while (a < -(float)M_PI) {
            a += 2.f * (float)M_PI;
        }
        return a < 0 ? -a : a;
    };
    if (wrap(ang) <= near) {
        return 0;
    }
    if (wrap(ang - (float)M_PI / 2) <= near) {
        return 3;
    }
    if (wrap(ang + (float)M_PI / 2) <= near) {
        return 1;
    }
    if (wrap(ang - (float)M_PI) <= near || wrap(ang + (float)M_PI) <= near) {
        return 2;
    }
    return -1;
}

static void apply_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    uint32_t duty = (uint32_t)percent * ((1 << LCD_BL_LEDC_RES) - 1) / 100;
    ledcWrite(PIN_LCD_BL, duty);
}

static uint32_t lv_tick_cb(void)
{
    return millis();
}

static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint16_t *src = (uint16_t *)px_map;

    switch (s_rot) {
    case 1:
        for (uint32_t dy = 0; dy < w; ++dy) {
            for (uint32_t dx = 0; dx < h; ++dx) {
                s_rotate_buf[dy * h + dx] = src[(h - 1 - dx) * w + dy];
            }
        }
        s_gfx->draw16bitRGBBitmap(k_max - area->y2, area->x1, s_rotate_buf, h, w);
        break;
    case 2:
        for (uint32_t i = 0, count = w * h; i < count; ++i) {
            s_rotate_buf[i] = src[count - 1 - i];
        }
        s_gfx->draw16bitRGBBitmap(k_max - area->x2, k_max - area->y2, s_rotate_buf, w, h);
        break;
    case 3:
        for (uint32_t dy = 0; dy < w; ++dy) {
            for (uint32_t dx = 0; dx < h; ++dx) {
                s_rotate_buf[dy * h + dx] = src[dx * w + (w - 1 - dy)];
            }
        }
        s_gfx->draw16bitRGBBitmap(area->y1, k_max - area->x2, s_rotate_buf, h, w);
        break;
    default:
        s_gfx->draw16bitRGBBitmap(area->x1, area->y1, src, w, h);
        break;
    }
    lv_display_flush_ready(disp);
}

static void lv_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);
    int16_t x = 0;
    int16_t y = 0;

    bool irq = g_touch_irq;
    if (irq) {
        g_touch_irq = false;
    }

    if (!s_touch_ok) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /* 中断优先，同时轮询 INT 脚，避免丢点 */
    if (irq || digitalRead(PIN_TP_INT) == LOW) {
        uint8_t n = s_touch.getPoint(&x, &y, 1);
        if (n > 0) {
            map_touch(&x, &y);
            clamp_xy(&x, &y);
            data->point.x = x;
            data->point.y = y;
            data->state = LV_INDEV_STATE_PRESSED;
            s_finger_down = true;
            return;
        }
    }

    s_finger_down = false;
    data->state = LV_INDEV_STATE_RELEASED;
}

bool hal_init()
{
    pinMode(PIN_BAT_EN, OUTPUT);
    digitalWrite(PIN_BAT_EN, HIGH);

    pinMode(PIN_BAT_CHG, INPUT_PULLUP);
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);

    s_bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);
    s_panel = new LcdST7789(s_bus, PIN_LCD_RST);
    s_gfx = s_panel;

    if (!s_panel->begin(LCD_SPI_FREQ_HZ)) {
        Serial.println("LCD begin failed");
        return false;
    }
    s_gfx->fillScreen(RGB565_BLACK);

    ledcAttach(PIN_LCD_BL, LCD_BL_LEDC_FREQ, LCD_BL_LEDC_RES);
    apply_brightness(LCD_BRIGHTNESS_DEFAULT);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);

    s_touch.setPins(PIN_TP_RST, PIN_TP_INT);
    s_touch_ok = s_touch.begin(Wire, CST816_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (s_touch_ok) {
        pinMode(PIN_TP_INT, INPUT_PULLUP);
        attachInterrupt(
            PIN_TP_INT,
            []() {
                g_touch_irq = true;
            },
            FALLING);
        Serial.printf("Touch OK: %s\n", s_touch.getModelName());
        s_touch.setMaxCoordinates(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    } else {
        Serial.println("Touch init failed");
    }

    if (s_imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL)) {
        s_imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
        s_imu.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS, SensorQMI8658::GYR_ODR_112_1Hz, SensorQMI8658::LPF_MODE_3);
        s_imu.enableAccelerometer();
        s_imu.enableGyroscope();
        s_imu_ok = true;
        Serial.printf("IMU OK, id=0x%02X\n", s_imu.getChipID());
    } else {
        Serial.println("IMU not found");
    }

    lv_init();
    lv_tick_set_cb(lv_tick_cb);

    s_disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_display_set_flush_cb(s_disp, lv_flush_cb);

    const size_t buf_bytes = k_draw_buf_pixels * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rotate_buf = (uint16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1) {
        buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf2) {
        buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_rotate_buf) {
        s_rotate_buf = (uint16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf1 || !s_rotate_buf) {
        Serial.println("LVGL buffer alloc failed");
        return false;
    }
    lv_display_set_buffers(s_disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lv_touch_cb);

    Serial.println("HAL ready");
    return true;
}

void hal_set_brightness(uint8_t percent)
{
    apply_brightness(percent);
}

uint8_t hal_get_brightness()
{
    return s_brightness;
}

float hal_battery_voltage()
{
    int mv = analogReadMilliVolts(PIN_BAT_ADC);
    return (mv / 1000.0f) * 3.0f;
}

int hal_battery_percent()
{
    float v = hal_battery_voltage();
    if (v < 3.52f) {
        return 1;
    }
    if (v < 3.64f) {
        return 20;
    }
    if (v < 3.76f) {
        return 40;
    }
    if (v < 3.88f) {
        return 60;
    }
    if (v < 4.0f) {
        return 80;
    }
    return 100;
}

bool hal_is_charging()
{
    return digitalRead(PIN_BAT_CHG) == LOW;
}

bool hal_imu_ready()
{
    return s_imu_ok;
}

bool hal_imu_read(float *ax, float *ay, float *az)
{
    if (!s_imu_ok || !ax || !ay || !az) {
        return false;
    }
    IMUdata acc;
    if (s_imu.getDataReady() && s_imu.getAccelerometer(acc.x, acc.y, acc.z)) {
        s_acc_x = acc.x;
        s_acc_y = acc.y;
        s_acc_z = acc.z;
        s_acc_ok = true;
    }
    if (!s_acc_ok) {
        return false;
    }
    *ax = s_acc_x;
    *ay = s_acc_y;
    *az = s_acc_z;
    return true;
}

bool hal_imu_read_screen(float *ax, float *ay, float *az)
{
    float rax = 0, ray = 0, raz = 0;
    if (!hal_imu_read(&rax, &ray, &raz)) {
        return false;
    }
    float sx0 = rax;
    float sy0 = ray;
    switch (s_rot) {
    case 1:
        *ax = sy0;
        *ay = -sx0;
        break;
    case 2:
        *ax = -sx0;
        *ay = -sy0;
        break;
    case 3:
        *ax = -sy0;
        *ay = sx0;
        break;
    default:
        *ax = sx0;
        *ay = sy0;
        break;
    }
    if (az) {
        *az = raz;
    }
    return true;
}

int hal_get_rotation()
{
    return s_rot;
}

void hal_set_rotation(int rot)
{
    rot &= 3;
    if (rot == s_rot) {
        return;
    }
    s_rot = rot;
    if (s_gfx) {
        /* All four orientations are performed in lv_flush_cb. */
        s_gfx->setRotation(0);
        s_panel->clearGram();
        s_gfx->fillScreen(RGB565_BLACK);
    }
    Serial.printf("LCD rot=%d\n", s_rot);
    if (s_disp) {
        lv_obj_t *layers[] = {
            lv_display_get_screen_active(s_disp),
            lv_display_get_screen_prev(s_disp),
            lv_display_get_layer_sys(s_disp),
            lv_display_get_layer_top(s_disp),
            lv_display_get_layer_bottom(s_disp),
        };
        for (lv_obj_t *obj : layers) {
            if (obj) {
                lv_obj_invalidate(obj);
            }
        }
        lv_refr_now(s_disp);
    }
    s_rot_last_ms = millis();
}

void hal_autorotate_enable(bool on)
{
    s_autorotate = on;
    s_rot_cand = s_rot;
    s_rot_cand_ms = 0;
}

bool hal_autorotate_enabled()
{
    return s_autorotate;
}

void hal_poll_autorotate()
{
    if (!s_autorotate || !s_imu_ok) {
        return;
    }
    if (s_finger_down) {
        s_rot_cand_ms = 0;
        return;
    }

    float ax = 0, ay = 0, az = 0;
    if (!hal_imu_read(&ax, &ay, &az)) {
        return;
    }

    int want = rotation_from_gravity(ax, ay, az);
    if (want < 0 || want == s_rot) {
        s_rot_cand_ms = 0;
        return;
    }

    uint32_t now = millis();
    if (want != s_rot_cand || s_rot_cand_ms == 0) {
        s_rot_cand = want;
        s_rot_cand_ms = now;
        return;
    }
    if (now - s_rot_cand_ms < 500) {
        return;
    }
    if (now - s_rot_last_ms < 400) {
        return;
    }
    hal_set_rotation(want);
}
