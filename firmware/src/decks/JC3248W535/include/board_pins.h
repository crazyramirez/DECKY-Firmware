#pragma once
/*
 * Guition JC3248W535 / JC3248W535EN pin map.
 *
 * Sources (cross-checked, see docs/HARDWARE_VALIDATION.md):
 *   - GthiN89/JC3248W535EN  src/esp_bsp.h        (commit 825f644, 2024-11-18)
 *   - GlomarGadaffi/jc3248-display-driver main/board.h + docs/Pinout-and-Hardware.md (commit 773c5b5, 2026-10-02)
 * Both agree on every LCD/backlight/touch pin below. Touch SCL/SDA mapping is confirmed
 * at boot by an I2C scan (hwtest env) — do not trust this file over the scan log.
 */

// ---- LCD: AXS15231B, 320x480 IPS, QSPI ----
#define PCD_LCD_H_RES      320
#define PCD_LCD_V_RES      480
#define PCD_PIN_LCD_CS     45
#define PCD_PIN_LCD_SCK    47
#define PCD_PIN_LCD_D0     21
#define PCD_PIN_LCD_D1     48
#define PCD_PIN_LCD_D2     40
#define PCD_PIN_LCD_D3     39
#define PCD_PIN_LCD_TE     38   // tearing effect, unused for now
#define PCD_PIN_LCD_BL     1    // backlight, PWM (LEDC)
// DC and RST are not connected on the QSPI variant (D/C is encoded in the opcode; reset is SWRESET).

// 40 MHz: stable ceiling reported by the Glomar driver (80 MHz -> line artefacts + I2C noise).
#ifndef PCD_LCD_PCLK_HZ
#define PCD_LCD_PCLK_HZ    (40 * 1000 * 1000)
#endif

// Init table: 1 = tsebelev/Glomar "verified JC3248W535EN" table (default)
//             2 = Espressif esp_lcd_axs15231b default table (as shipped in GthiN89 repo)
#ifndef PCD_LCD_INIT_TABLE
#define PCD_LCD_INIT_TABLE 1
#endif

// ---- Capacitive touch: AXS15231B touch core over I2C ----
#define PCD_PIN_TOUCH_SDA  4
#define PCD_PIN_TOUCH_SCL  8
#define PCD_PIN_TOUCH_INT  3    // reported by Glomar only; touch is polled, INT is the deep-sleep wake line
#define PCD_TOUCH_I2C_ADDR 0x3B
#define PCD_TOUCH_I2C_HZ   400000

// Deep sleep: 0 (default) keeps the panel in normal mode so its touch core keeps scanning and one
// tap wakes the deck; 1 sends SLPIN first (a little less power, but waking takes several taps).
#ifndef PCD_DEEP_SLEEP_PANEL_SLPIN
#define PCD_DEEP_SLEEP_PANEL_SLPIN 0
#endif

// Touch -> display coordinate transform (validated in hwtest; adjust only with evidence)
#ifndef PCD_TOUCH_SWAP_XY
#define PCD_TOUCH_SWAP_XY  0
#endif
#ifndef PCD_TOUCH_MIRROR_X
#define PCD_TOUCH_MIRROR_X 0
#endif
#ifndef PCD_TOUCH_MIRROR_Y
#define PCD_TOUCH_MIRROR_Y 0
#endif

// ---- Optional peripherals (not used in V1) ----
#define PCD_PIN_SD_CLK     12
#define PCD_PIN_SD_CMD     11
#define PCD_PIN_SD_D0      13
#define PCD_PIN_I2S_BCLK   42
#define PCD_PIN_I2S_WS     2
#define PCD_PIN_I2S_DOUT   41
#define PCD_PIN_BAT_ADC    5
