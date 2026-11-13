#pragma once
/*
 * Guition ESP32-4848S040 (4" 480x480 "86 box" panel, ESP32-S3 N16R8) pin map.
 *
 * Sources (cross-checked 2026-11-13, they agree on every pin below):
 *   - moononournation/Arduino_GFX  examples/PDQgraphicstest/Arduino_GFX_dev_device.h,
 *     block ESP32_4848S040_86BOX_GUITION (+ st7701_type9_init_operations, "by manufacture")
 *   - rzeldent/platformio-espressif32-sunton  esp32-4848S040CIY1.json
 * USB-C goes to a CH340 on UART0: GPIO19/20 (the S3's native USB) are the touch SDA and a pixel line.
 */

// ---- LCD: ST7701S, 480x480 IPS, 16-bit parallel RGB (RGB565) + 3-wire SPI for its init ----
#define PCD_LCD_H_RES      480
#define PCD_LCD_V_RES      480

// 3-wire SPI (9-bit words: D/C bit + 8 data bits), bit-banged; only used for the init sequence.
#define PCD_PIN_LCD_CS     39
#define PCD_PIN_LCD_SCK    48
#define PCD_PIN_LCD_SDA    47   // shared with the TF card's MOSI (the card is not used)

#define PCD_PIN_LCD_DE     18
#define PCD_PIN_LCD_VSYNC  17
#define PCD_PIN_LCD_HSYNC  16
#define PCD_PIN_LCD_PCLK   21
// Data lines, RGB565 bit order B0..B4, G0..G5, R0..R4. R4 is GPIO0 (BOOT): no BOOT button at runtime.
#define PCD_PIN_LCD_B0     4
#define PCD_PIN_LCD_B1     5
#define PCD_PIN_LCD_B2     6
#define PCD_PIN_LCD_B3     7
#define PCD_PIN_LCD_B4     15
#define PCD_PIN_LCD_G0     8
#define PCD_PIN_LCD_G1     20
#define PCD_PIN_LCD_G2     3
#define PCD_PIN_LCD_G3     46
#define PCD_PIN_LCD_G4     9
#define PCD_PIN_LCD_G5     10
#define PCD_PIN_LCD_R0     11
#define PCD_PIN_LCD_R1     12
#define PCD_PIN_LCD_R2     13
#define PCD_PIN_LCD_R3     14
#define PCD_PIN_LCD_R4     0
#define PCD_PIN_LCD_BL     38   // backlight, PWM (LEDC)
// No panel reset line: the init sequence starts with the panel's own power-on reset.

// Timings (both sources): HSYNC 8 / back 50 / front 10, VSYNC 8 / back 20 / front 10, syncs idle
// high, data on the PCLK rising edge. Arduino_GFX runs 12 MHz, the Sunton board file 12.5 MHz.
#ifndef PCD_LCD_PCLK_HZ
#define PCD_LCD_PCLK_HZ    (12 * 1000 * 1000)
#endif

// ---- Capacitive touch: GT911 over I2C (RST and INT not wired) ----
#define PCD_PIN_TOUCH_SDA  19
#define PCD_PIN_TOUCH_SCL  45
#define PCD_PIN_TOUCH_INT  -1
#define PCD_PIN_TOUCH_RST  -1
#define PCD_TOUCH_I2C_ADDR 0x5D // 0x14 is probed too (address depends on INT at power-on)
#define PCD_TOUCH_I2C_HZ   400000

// Touch -> display coordinate transform (adjust only with evidence: env:hwtest corner targets).
#ifndef PCD_TOUCH_SWAP_XY
#define PCD_TOUCH_SWAP_XY  0
#endif
#ifndef PCD_TOUCH_MIRROR_X
#define PCD_TOUCH_MIRROR_X 0
#endif
#ifndef PCD_TOUCH_MIRROR_Y
#define PCD_TOUCH_MIRROR_Y 0
#endif

// ---- Not used ----
#define PCD_PIN_BAT_ADC    -1   // USB powered, no battery
// TF card: CS 42, MOSI 47, CLK 48, MISO 41. Relays (relay variants): GPIO1, 2, 40 — left untouched.
