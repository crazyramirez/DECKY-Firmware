#pragma once
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Minimal NV3041A QSPI panel driver (Guition JC4827W543, 480x272 landscape, native orientation).
 *
 * Framing mirrors the vendor-verified Arduino_GFX path (Arduino_ESP32QSPI + Arduino_NV3041A 1.4.4):
 *   - command / 1-byte parameter: opcode 0x02, address = cmd << 8, data on one line;
 *   - CASET/RASET parameters are sent "split": the command alone, then each byte as its own
 *     opcode-0x32 transaction (PCD_LCD_SPLIT_PARAMS=0 sends them in one 0x02 transaction instead);
 *   - pixels: RAMWR (0x2C) as a command, then opcode 0x32 / address 0x003C00 with the data on 4 lines,
 *     CS held low for the whole transfer (manual CS).
 * The vendor demos always push whole frames (Arduino_Canvas); so does push_frame(). push_rows()
 * pushes a full-width row band (used only with PCD_LCD_PARTIAL_ROWS=1).
 * Pixels must already be big-endian RGB565 (LVGL: LV_COLOR_16_SWAP=1). The frame normally lives in
 * PSRAM; each band is copied to an internal-RAM DMA bounce buffer (ping-pong) before it is sent.
 */
namespace lcd {

esp_err_t init();                          // SPI bus + device + init table + INVON + DISPON
esp_err_t push_frame(const uint16_t *fb);  // blocking; fb = PCD_LCD_H_RES * PCD_LCD_V_RES pixels
esp_err_t push_rows(const uint16_t *fb, int y0, int y1);  // rows y0..y1 (inclusive) of the frame
esp_err_t set_display_on(bool on);
esp_err_t sleep_in();                      // DISPOFF + SLPIN before deep sleep (wake = full re-init)
uint32_t last_push_us();                   // duration of the last push, diagnostics

}  // namespace lcd
