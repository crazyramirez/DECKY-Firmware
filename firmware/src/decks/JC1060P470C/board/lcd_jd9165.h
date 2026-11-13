#pragma once
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * JD9165 1024x600 MIPI-DSI panel (Guition JC1060P470C), on ESP-IDF's DSI/DPI driver plus the
 * Espressif esp_lcd_jd9165 vendor sequence (lib/esp_lcd_jd9165).
 *
 * The DPI controller scans one RGB565 frame buffer in PSRAM continuously (~60 Hz). draw() hands a
 * rectangle of pixels to the DMA2D engine, which copies it into that frame buffer asynchronously;
 * wait_done() blocks until the copy has finished (the source buffer may be reused afterwards).
 */
namespace lcd {

esp_err_t init();  // LDO for the D-PHY, DSI bus, DBI command interface, DPI panel, vendor init
// Copy pixels (x1..x2-1, y1..y2-1, little-endian RGB565) into the frame buffer. Asynchronous.
esp_err_t draw(int x1, int y1, int x2, int y2, const void *pixels);
bool wait_done(uint32_t timeout_ms);  // false on timeout
uint16_t *framebuffer();               // the scanned frame (PSRAM, written by DMA2D: sync the cache before reading)
void sync_framebuffer_for_read();      // invalidate the CPU cache lines of the frame buffer
esp_err_t set_display_on(bool on);
uint32_t last_draw_us();  // duration of the last DMA2D copy, diagnostics

}  // namespace lcd
