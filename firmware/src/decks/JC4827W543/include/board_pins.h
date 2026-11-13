#pragma once
/*
 * Guition JC4827W543 (C = capacitive touch) pin map.
 *
 * Sources (cross-checked, see docs/HARDWARE_VALIDATION.md):
 *   - Vendor package (github.com/OldNKrusty/JC4827W543-ESP32S3-Guide): demo LvglWidgets.ino
 *     (Arduino_ESP32QSPI + Arduino_NV3041A), touch.h (TouchLib GT911), "4.3 inches IO pin
 *     distribution.xlsx", vendor binary header (4 MB flash, W25Q32 id 0x4016).
 * The guide's README describes a 24-bit RGB panel: that is the Sunton ESP32-4827S043, NOT this
 * board. The vendor demo for the JC4827W543 drives an NV3041A over QSPI.
 */

// ---- LCD: NV3041A, 480x272 IPS, QSPI (same QSPI wiring as the JC3248W535) ----
#define PCD_LCD_H_RES      480
#define PCD_LCD_V_RES      272
#define PCD_PIN_LCD_CS     45
#define PCD_PIN_LCD_SCK    47
#define PCD_PIN_LCD_D0     21
#define PCD_PIN_LCD_D1     48
#define PCD_PIN_LCD_D2     40
#define PCD_PIN_LCD_D3     39
#define PCD_PIN_LCD_TE     0    // shares GPIO0 with BOOT; tearing output is never enabled (no TEON)
#define PCD_PIN_LCD_BL     1    // backlight, PWM (LEDC)
// RST is not wired (software reset); D/C is encoded in the QSPI opcode.

// The NV3041A datasheet / Arduino_GFX cap the interface at 32 MHz.
#ifndef PCD_LCD_PCLK_HZ
#define PCD_LCD_PCLK_HZ    (32 * 1000 * 1000)
#endif

// ---- Capacitive touch: GT911 over I2C ----
#define PCD_PIN_TOUCH_SDA  8
#define PCD_PIN_TOUCH_SCL  4
#define PCD_PIN_TOUCH_INT  3    // GT911 INT (also the deep-sleep wake line)
#define PCD_PIN_TOUCH_RST  38   // GT911 RESET (RTP_CS on the resistive variant)
#define PCD_TOUCH_I2C_ADDR 0x5D // 0x14 is probed too (address depends on INT at reset)
#define PCD_TOUCH_I2C_HZ   400000

// Touch -> display coordinate transform. The vendor demo maps GT911 coordinates 1:1 at rotation 0;
// adjust only with evidence (env:hwtest corner targets).
#ifndef PCD_TOUCH_SWAP_XY
#define PCD_TOUCH_SWAP_XY  0
#endif
#ifndef PCD_TOUCH_MIRROR_X
#define PCD_TOUCH_MIRROR_X 0
#endif
#ifndef PCD_TOUCH_MIRROR_Y
#define PCD_TOUCH_MIRROR_Y 0
#endif

// ---- Sleep button on the IO17/IO18 header ----
// A touch-button module (TTP223-style, momentary) on IO17 and/or IO18: a tap puts the deck into deep
// sleep, another tap wakes it. Both pins are watched; their idle level is learnt at boot (pull-down
// on, so an unconnected pin reads low and never triggers), the active level is the opposite one.
#ifndef PCD_PIN_SLEEP_BTN_A
#define PCD_PIN_SLEEP_BTN_A 17
#endif
#ifndef PCD_PIN_SLEEP_BTN_B
#define PCD_PIN_SLEEP_BTN_B 18
#endif

// ---- Deck power ----
// No battery-sense line is broken out on this board (no ADC divider in the pin map): only "USB host
// present" is detected. Set to a GPIO number if you add a divider (2:1 to the cell).
#ifndef PCD_PIN_BAT_ADC
#define PCD_PIN_BAT_ADC    -1
#endif

// ---- Optional peripherals (not used) ----
#define PCD_PIN_SD_CS      10
#define PCD_PIN_SD_MISO    11
#define PCD_PIN_SD_CLK     12
#define PCD_PIN_SD_MOSI    13
#define PCD_PIN_I2S_LRCLK  2
#define PCD_PIN_I2S_DOUT   41
#define PCD_PIN_I2S_BCLK   42
