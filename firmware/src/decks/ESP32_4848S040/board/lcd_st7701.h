#pragma once
#include <stdint.h>

#include "esp_err.h"

/*
 * ST7701S 480x480 panel of the Guition ESP32-4848S040.
 *  - Init: the vendor's register sequence over a bit-banged 3-wire SPI (9-bit words: D/C + 8 bits),
 *    the "type9" table that Arduino_GFX ships "by manufacture" for this board.
 *  - Pixels: the S3's LCD_CAM RGB peripheral scans one 480x480 RGB565 frame buffer in PSRAM
 *    continuously (DMA, ~43 Hz at 12 MHz PCLK). Whatever reaches that buffer appears on the next
 *    scan, so nothing is drawn there directly: present() copies finished areas into it, timed off
 *    VSYNC so the scan never crosses the copy (no tearing), and writes the cache back band by band.
 *    The driver's own double buffering can't be used: with CONFIG_LCD_RGB_RESTART_IN_VSYNC (set in
 *    the Arduino libraries) the S3 restarts the DMA from frame buffer 0 on every frame.
 */
namespace lcd {

struct Rect {
  int16_t x1, y1, x2, y2;  // inclusive
};

esp_err_t init();
uint16_t *framebuffer();  // the scanned 480x480 little-endian RGB565 frame, in PSRAM
// Copies rects of src (a full 480x480 frame) into the scanned frame, starting at the next VSYNC.
// Reorders rects (top-down).
void present(const uint16_t *src, Rect *rects, int n);
void fill(uint16_t color);  // whole frame, synced (boot only)
uint32_t last_push_us();    // copy + cache write-back time of the last present()

}  // namespace lcd
