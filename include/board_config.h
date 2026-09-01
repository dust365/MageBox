#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-1.54
 * 引脚来自官方 BSP / Arduino 示例（examples/ESP32-S3-Touch-LCD-1.54-demo）
 */

#define BOARD_LCD_H_RES                 240
#define BOARD_LCD_V_RES                 240

/* ST7789  4-wire SPI  (SPI2) */
#define PIN_LCD_DC                      45
#define PIN_LCD_CS                      21
#define PIN_LCD_SCLK                    38
#define PIN_LCD_MOSI                    39
#define PIN_LCD_RST                     40
#define PIN_LCD_BL                      46
#define LCD_SPI_FREQ_HZ                40000000
#define LCD_SPI_MODE                    3

/* CST816 电容触摸 + QMI8658 / ES8311 / ES7210 共用 I2C */
#define PIN_I2C_SDA                     42
#define PIN_I2C_SCL                     41
#define PIN_TP_INT                      48
#define PIN_TP_RST                      47

/* 音频 ES8311 + ES7210 + NS4150B */
#define PIN_I2S_MCLK                    8
#define PIN_I2S_BCLK                    9
#define PIN_I2S_WS                      10
#define PIN_I2S_DIN                     11
#define PIN_I2S_DOUT                    12
#define PIN_PA_CTRL                     7

/* TF 卡 SDMMC 4-bit */
#define PIN_SD_CLK                      16
#define PIN_SD_CMD                      15
#define PIN_SD_D0                       17
#define PIN_SD_D1                       18
#define PIN_SD_D2                       13
#define PIN_SD_D3                       14

/* 电源 */
#define PIN_BAT_ADC                     1
#define PIN_BAT_EN                      2
#define PIN_BAT_CHG                     3

/* 实体按键：BOOT 保留系统导航，PWR / PLUS 用于音量减 / 加 */
#define PIN_BTN_BOOT                    0
#define PIN_BTN_PLUS                    4
#define PIN_BTN_PWR                     5

#define LCD_BL_LEDC_FREQ                5000
#define LCD_BL_LEDC_RES                 10
#define LCD_BRIGHTNESS_DEFAULT          80
