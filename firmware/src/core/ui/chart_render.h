#pragma once
/*
 * Software chart rasteriser that draws straight into an LVGL canvas buffer (ported from the
 * JC1060P470C CryptoTracker):
 *  - sub-pixel anti-aliased polyline with an optional soft glow
 *  - vertical gradient area fill (ordered dithering)
 *  - candlesticks and a volume histogram
 * Target: RGB565 as lv_color_t (little-endian here, LV_COLOR_16_SWAP=0; byte-swapped when the build
 * sets LV_COLOR_16_SWAP=1). Rendering happens once per data change; LVGL then just blits the canvas.
 */
#include <stdint.h>

#include "crypto/models.h"

namespace ui {
namespace chart {

struct Paint {
  uint32_t bg;          // canvas background (must match the parent)
  uint32_t line;        // series colour
  uint32_t grid;        // grid colour (0 = no grid)
  uint8_t grid_a;       // grid alpha, 0 = opaque
  float line_w;         // px
  float glow_r;         // px, 0 = off
  uint8_t glow_a;       // 0..255
  uint8_t fill_a;       // 0..255 at the top of the fill
  int grid_rows;        // horizontal grid lines
  int pad_top, pad_bottom;  // plot insets inside the canvas
  int vol_h;            // volume histogram height (0 = off)
  uint32_t up, down;    // candle colours
};

void range(const float *v, int n, float *vmin, float *vmax);
void range_candles(const crypto::Candle *c, int n, float *vmin, float *vmax);
float value_to_y(float v, float vmin, float vmax, int h, const Paint &p);

// buf = w*h uint16_t (RGB565 as lv_color_t). vols / vol_up optional (volume histogram).
void render_line(uint16_t *buf, int w, int h, const float *vals, int n, float vmin, float vmax, const Paint &p,
                 const float *vols = nullptr, const bool *vol_up = nullptr);
void render_candles(uint16_t *buf, int w, int h, const crypto::Candle *c, int n, float vmin, float vmax, const Paint &p);
void clear(uint16_t *buf, int w, int h, uint32_t color);

// Aggregate candles into at most max_out buckets (OHLC merge, aligned to the newest). Returns count.
int aggregate(const crypto::Candle *in, int n, crypto::Candle *out, int max_out);

}  // namespace chart
}  // namespace ui
