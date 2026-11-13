#pragma once
/*
 * Guition JC1060P470C pin map: ESP32-P4 (32 MB PSRAM, 16 MB flash) + ESP32-C6 (Wi-Fi over
 * ESP-Hosted / SDIO), 7" 1024x600 IPS with a JD9165 MIPI-DSI controller, GT911 capacitive touch.
 *
 * Sources (see docs/HARDWARE_VALIDATION.md): the vendor LVGL demo (pins_config.h, jd9165_lcd.cpp,
 * gt911_touch.cpp) and the JC1060P470C CryptoTracker firmware, which runs on this board with the
 * same panel, touch, backlight and Wi-Fi set-up.
 */

// ---- LCD: JD9165, 1024x600 IPS, MIPI-DSI 2 lanes ----
#define PCD_LCD_H_RES        1024
#define PCD_LCD_V_RES        600
#define PCD_PIN_LCD_RST      5
#define PCD_PIN_LCD_BL       23   // backlight, PWM (LEDC)
#define PCD_DSI_LANES        2
#define PCD_DSI_LANE_MBPS    750
#define PCD_DPI_CLOCK_MHZ    52   // ~60 Hz with the vendor timings (esp_lcd_jd9165.h)
// The MIPI D-PHY is powered from the P4's internal LDO channel 3 at 2.5 V.
#define PCD_DSI_PHY_LDO_CHAN 3
#define PCD_DSI_PHY_LDO_MV   2500

// LVGL renders in partial mode into two internal-RAM buffers of this many lines; each finished band
// is copied into the panel's PSRAM frame buffer by the DMA2D engine while the next one renders.
#ifndef PCD_LVGL_BUF_LINES
#define PCD_LVGL_BUF_LINES   40
#endif

// ---- Capacitive touch: GT911 over I2C ----
#define PCD_PIN_TOUCH_SDA    7
#define PCD_PIN_TOUCH_SCL    8
#define PCD_PIN_TOUCH_INT    -1   // not wired to the P4 (vendor demo: TP_INT -1): the GT911 is polled
#define PCD_PIN_TOUCH_RST    -1   // not wired (vendor demo: TP_RST -1)
#define PCD_TOUCH_I2C_ADDR   0x5D // 0x14 is probed too (address depends on INT at power-on)
#define PCD_TOUCH_I2C_HZ     400000

// Touch -> display coordinate transform. The vendor demo maps GT911 coordinates 1:1; adjust only
// with evidence (env:hwtest corner targets).
#ifndef PCD_TOUCH_SWAP_XY
#define PCD_TOUCH_SWAP_XY    0
#endif
#ifndef PCD_TOUCH_MIRROR_X
#define PCD_TOUCH_MIRROR_X   0
#endif
#ifndef PCD_TOUCH_MIRROR_Y
#define PCD_TOUCH_MIRROR_Y   0
#endif

// ---- Buttons ----
// BOOT (GPIO35, active low, strapping pin read as a plain input after boot): turns the screen
// off / on. RST restarts the board. There is no deep-sleep wake line on this board (the touch INT is
// not wired and GPIO35 is not an LP GPIO), so "sleep" means screen off with Wi-Fi kept up.
#define PCD_PIN_BOOT_BTN     35

// ---- Deck power ----
// No battery-sense line: only "USB host present" is detected. Set to a GPIO number if you add a
// divider (2:1 to the cell).
#ifndef PCD_PIN_BAT_ADC
#define PCD_PIN_BAT_ADC      -1
#endif
