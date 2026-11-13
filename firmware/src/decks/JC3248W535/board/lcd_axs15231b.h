#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * Minimal AXS15231B QSPI panel driver.
 *
 * The panel only accepts a full frame written top-to-bottom: RAMWR (0x2C) for the
 * first band, RAMWRC (0x3C) continuation for the rest (RASET is not honoured over
 * QSPI in the reference drivers). So the public API is "push a whole 320x480 frame".
 * Pixels must already be big-endian RGB565 (LVGL: LV_COLOR_16_SWAP=1).
 *
 * The frame normally lives in PSRAM; DMA straight from PSRAM can underflow the SPI
 * FIFO, so each band is copied to one of two internal-RAM bounce buffers (ping-pong)
 * and DMA'd from there.
 */
namespace lcd {

esp_err_t init();                         // SPI bus + panel IO + init table + DISPON
esp_err_t push_frame(const uint16_t *fb); // blocking; fb = PCD_LCD_H_RES*PCD_LCD_V_RES pixels
esp_err_t set_display_on(bool on);
esp_err_t sleep_in();                     // DISPOFF + SLPIN before deep sleep (wake = full re-init)
uint32_t last_push_us();                  // duration of the last push_frame, diagnostics

}  // namespace lcd
