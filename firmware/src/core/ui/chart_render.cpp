#include "chart_render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "lv_conf.h"

namespace ui {
namespace chart {
namespace {

uint8_t *s_cov_line = nullptr;
uint8_t *s_cov_glow = nullptr;
int s_cov_cap = 0;
float *s_xs = nullptr;
float *s_ys = nullptr;

const uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

struct RGBf {
  float r, g, b;
};

inline RGBf hex_rgb(uint32_t c) {
  return {static_cast<float>((c >> 16) & 0xFF), static_cast<float>((c >> 8) & 0xFF), static_cast<float>(c & 0xFF)};
}
// Canvas pixels are lv_color_t: byte-swapped only when LVGL is built with LV_COLOR_16_SWAP.
#if LV_COLOR_16_SWAP
inline uint16_t swap16(uint16_t v) { return static_cast<uint16_t>((v << 8) | (v >> 8)); }
#else
inline uint16_t swap16(uint16_t v) { return v; }
#endif
inline RGBf unpack(uint16_t px) {
  const uint16_t c = swap16(px);
  const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return {static_cast<float>((r << 3) | (r >> 2)), static_cast<float>((g << 2) | (g >> 4)),
          static_cast<float>((b << 3) | (b >> 2))};
}
inline uint16_t pack_plain(uint32_t c) {
  return swap16(static_cast<uint16_t>((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xFF) >> 3)));
}
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline uint16_t pack_dither(const RGBf &c, int x, int y) {
  const float t = (kBayer4[y & 3][x & 3] + 0.5f) / 16.0f;
  const int r = clampi(static_cast<int>(c.r * (31.0f / 255.0f) + t), 0, 31);
  const int g = clampi(static_cast<int>(c.g * (63.0f / 255.0f) + t), 0, 63);
  const int b = clampi(static_cast<int>(c.b * (31.0f / 255.0f) + t), 0, 31);
  return swap16(static_cast<uint16_t>((r << 11) | (g << 5) | b));
}
inline RGBf mix(const RGBf &a, const RGBf &b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

struct Surf {
  uint16_t *b;
  int w, h;
  void clear(uint32_t bg) {
    const uint16_t v = pack_plain(bg);
    for (int i = 0; i < w * h; i++) b[i] = v;
  }
  inline void blend(int x, int y, const RGBf &c, float a) {
    uint16_t *p = &b[y * w + x];
    *p = pack_dither(a >= 0.999f ? c : mix(unpack(*p), c, a), x, y);
  }
};

bool ensure_buffers(int pixels) {
  if (!s_xs) {
    s_xs = static_cast<float *>(heap_caps_malloc(sizeof(float) * crypto::kMaxCandles, MALLOC_CAP_SPIRAM));
    s_ys = static_cast<float *>(heap_caps_malloc(sizeof(float) * crypto::kMaxCandles, MALLOC_CAP_SPIRAM));
  }
  if (pixels > s_cov_cap) {
    heap_caps_free(s_cov_line);
    heap_caps_free(s_cov_glow);
    s_cov_line = static_cast<uint8_t *>(heap_caps_malloc(pixels, MALLOC_CAP_SPIRAM));
    s_cov_glow = static_cast<uint8_t *>(heap_caps_malloc(pixels, MALLOC_CAP_SPIRAM));
    s_cov_cap = (s_cov_line && s_cov_glow) ? pixels : 0;
  }
  return s_xs && s_ys && s_cov_cap >= pixels;
}

void draw_grid(Surf &s, const Paint &p) {
  if (!p.grid || p.grid_rows < 2) return;
  const RGBf gc = hex_rgb(p.grid);
  const float ga = p.grid_a ? p.grid_a / 255.0f : 1.0f;
  const float ph = static_cast<float>(s.h - p.pad_top - p.pad_bottom);
  for (int k = 0; k < p.grid_rows; k++) {
    const int y = static_cast<int>(lroundf(p.pad_top + k * ph / (p.grid_rows - 1)));
    if (y < 0 || y >= s.h) continue;
    for (int x = 0; x < s.w; x++)
      if ((x % 6) < 2) s.blend(x, y, gc, ga);
  }
}

void blend_rect(Surf &s, int x0, int y0, int x1, int y1, uint32_t color, float a) {
  x0 = clampi(x0, 0, s.w);
  x1 = clampi(x1, 0, s.w);
  y0 = clampi(y0, 0, s.h);
  y1 = clampi(y1, 0, s.h);
  const RGBf c = hex_rgb(color);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) s.blend(x, y, c, a);
}

// Rasterise the polyline into the coverage buffers.
void raster_polyline(int w, int h, const float *xs, const float *ys, int n, float line_w, float glow_r, int *ymin_out,
                     int *ymax_out) {
  memset(s_cov_line, 0, static_cast<size_t>(w) * h);
  if (glow_r > 0) memset(s_cov_glow, 0, static_cast<size_t>(w) * h);
  const float hw = line_w * 0.5f;
  const float R = (glow_r > hw + 1.0f) ? glow_r : hw + 1.0f;
  int gymin = h, gymax = 0;
  for (int i = 0; i + 1 < n; i++) {
    const float x0 = xs[i], y0 = ys[i], x1 = xs[i + 1], y1 = ys[i + 1];
    const float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    if (len2 < 1e-6f) len2 = 1e-6f;
    const int bx0 = clampi(static_cast<int>(floorf(fminf(x0, x1) - R)), 0, w - 1);
    const int bx1 = clampi(static_cast<int>(ceilf(fmaxf(x0, x1) + R)), 0, w - 1);
    const int by0 = clampi(static_cast<int>(floorf(fminf(y0, y1) - R)), 0, h - 1);
    const int by1 = clampi(static_cast<int>(ceilf(fmaxf(y0, y1) + R)), 0, h - 1);
    if (by0 < gymin) gymin = by0;
    if (by1 > gymax) gymax = by1;
    const float inv = 1.0f / len2;
    for (int py = by0; py <= by1; py++) {
      const float cy = py + 0.5f;
      uint8_t *lrow = s_cov_line + py * w;
      uint8_t *grow = s_cov_glow + py * w;
      for (int px = bx0; px <= bx1; px++) {
        const float cx = px + 0.5f;
        float t = ((cx - x0) * dx + (cy - y0) * dy) * inv;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float ex = x0 + t * dx - cx, ey = y0 + t * dy - cy;
        const float d = sqrtf(ex * ex + ey * ey);
        const float lc = hw + 0.5f - d;
        if (lc > 0) {
          const uint8_t v = lc >= 1 ? 255 : static_cast<uint8_t>(lc * 255);
          if (v > lrow[px]) lrow[px] = v;
        }
        if (glow_r > 0 && d < glow_r) {
          const float g = 1.0f - d / glow_r;
          const uint8_t v = static_cast<uint8_t>(g * g * 255);
          if (v > grow[px]) grow[px] = v;
        }
      }
    }
  }
  *ymin_out = gymin;
  *ymax_out = gymax;
}

}  // namespace

void clear(uint16_t *buf, int w, int h, uint32_t color) {
  Surf s{buf, w, h};
  s.clear(color);
}

void range(const float *v, int n, float *vmin, float *vmax) {
  float mn = INFINITY, mx = -INFINITY;
  for (int i = 0; i < n; i++) {
    if (v[i] < mn) mn = v[i];
    if (v[i] > mx) mx = v[i];
  }
  if (!(mx > mn)) {
    const float d = fabsf(mx) * 0.01f + 1e-9f;
    mn -= d;
    mx += d;
  }
  *vmin = mn;
  *vmax = mx;
}

void range_candles(const crypto::Candle *c, int n, float *vmin, float *vmax) {
  float mn = INFINITY, mx = -INFINITY;
  for (int i = 0; i < n; i++) {
    if (c[i].l < mn) mn = c[i].l;
    if (c[i].h > mx) mx = c[i].h;
  }
  if (!(mx > mn)) {
    const float d = fabsf(mx) * 0.01f + 1e-9f;
    mn -= d;
    mx += d;
  }
  *vmin = mn;
  *vmax = mx;
}

float value_to_y(float v, float vmin, float vmax, int h, const Paint &p) {
  const float ph = static_cast<float>(h - p.pad_top - p.pad_bottom);
  return p.pad_top + (vmax - v) / (vmax - vmin) * ph;
}

void render_line(uint16_t *buf, int w, int h, const float *vals, int n, float vmin, float vmax, const Paint &p,
                 const float *vols, const bool *vol_up) {
  Surf s{buf, w, h};
  s.clear(p.bg);
  if (n < 2 || !ensure_buffers(w * h)) return;
  if (n > crypto::kMaxCandles) n = crypto::kMaxCandles;
  draw_grid(s, p);

  // Volume histogram (aggregated to ~4 px bars) along the bottom of the canvas.
  if (vols && p.vol_h > 0) {
    float vmx = 0;
    for (int i = 0; i < n; i++) vmx = fmaxf(vmx, vols[i]);
    if (vmx > 0) {
      int bars = w / 4;
      if (bars > n) bars = n;
      const int base = h - 1;
      for (int b = 0; b < bars; b++) {
        const int i0 = b * n / bars, i1 = (b + 1) * n / bars;
        float v = 0;
        for (int i = i0; i < i1; i++) v = fmaxf(v, vols[i]);
        const bool up = vol_up ? vol_up[i1 - 1] : true;
        const int bh = static_cast<int>(v / vmx * p.vol_h);
        const int x0 = b * w / bars, x1 = (b + 1) * w / bars - 1;
        blend_rect(s, x0, base - bh, x1, base, up ? p.up : p.down, 0.24f);
      }
    }
  }

  const float xspan = static_cast<float>(w - 1) - p.line_w;  // keep the round cap inside
  float ytop = 1e9f;
  for (int i = 0; i < n; i++) {
    s_xs[i] = p.line_w * 0.5f + xspan * i / (n - 1);
    s_ys[i] = value_to_y(vals[i], vmin, vmax, h, p);
    if (s_ys[i] < ytop) ytop = s_ys[i];
  }
  const RGBf lc = hex_rgb(p.line);

  // Gradient fill under the curve.
  if (p.fill_a) {
    const float ybot = static_cast<float>(h - p.pad_bottom);
    float span = ybot - ytop;
    if (span < 1) span = 1;
    const float a0 = p.fill_a / 255.0f;
    int seg = 0;
    for (int x = 0; x < w; x++) {
      const float cx = x + 0.5f;
      while (seg + 2 < n && s_xs[seg + 1] < cx) seg++;
      float t = (cx - s_xs[seg]) / (s_xs[seg + 1] - s_xs[seg]);
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      const float yl = s_ys[seg] + (s_ys[seg + 1] - s_ys[seg]) * t;
      const int y0 = static_cast<int>(floorf(yl));
      for (int y = y0 < 0 ? 0 : y0; y < static_cast<int>(ybot) && y < h; y++) {
        const float k = (ybot - (y + 0.5f)) / span;
        float a = a0 * k * k;
        if (y == y0) a *= 1.0f - (yl - y0);
        if (a <= 0.002f) continue;
        s.blend(x, y, lc, a);
      }
    }
  }

  // Glow + anti-aliased line.
  int ymin, ymax;
  raster_polyline(w, h, s_xs, s_ys, n, p.line_w, p.glow_r, &ymin, &ymax);
  const float ga = p.glow_a / 255.0f;
  for (int y = ymin; y <= ymax; y++) {
    const uint8_t *lr = s_cov_line + y * w;
    const uint8_t *gr = s_cov_glow + y * w;
    for (int x = 0; x < w; x++) {
      const uint8_t l = lr[x];
      const uint8_t g = p.glow_r > 0 ? gr[x] : 0;
      if (g) s.blend(x, y, lc, ga * g / 255.0f);
      if (l) s.blend(x, y, lc, l / 255.0f);
    }
  }
}

void render_candles(uint16_t *buf, int w, int h, const crypto::Candle *c, int n, float vmin, float vmax, const Paint &p) {
  Surf s{buf, w, h};
  s.clear(p.bg);
  if (n < 1) return;
  draw_grid(s, p);
  const float cw = static_cast<float>(w) / n;
  float bw = cw * 0.64f;
  if (bw < 1) bw = 1;
  const int wick = bw >= 9 ? 2 : 1;

  if (p.vol_h > 0) {
    float vmx = 0;
    for (int i = 0; i < n; i++) vmx = fmaxf(vmx, c[i].v);
    if (vmx > 0) {
      const int base = h - 1;
      for (int i = 0; i < n; i++) {
        const float cx = (i + 0.5f) * cw;
        const int bh = static_cast<int>(c[i].v / vmx * p.vol_h);
        blend_rect(s, static_cast<int>(lroundf(cx - bw / 2)), base - bh, static_cast<int>(lroundf(cx + bw / 2)), base,
                   c[i].c >= c[i].o ? p.up : p.down, 0.24f);
      }
    }
  }
  for (int i = 0; i < n; i++) {
    const bool up = c[i].c >= c[i].o;
    const uint32_t col = up ? p.up : p.down;
    const float cx = (i + 0.5f) * cw;
    const int yh = static_cast<int>(lroundf(value_to_y(c[i].h, vmin, vmax, h, p)));
    const int yl = static_cast<int>(lroundf(value_to_y(c[i].l, vmin, vmax, h, p)));
    const int yo = static_cast<int>(lroundf(value_to_y(c[i].o, vmin, vmax, h, p)));
    const int yc = static_cast<int>(lroundf(value_to_y(c[i].c, vmin, vmax, h, p)));
    const int wx = static_cast<int>(lroundf(cx - wick / 2.0f));
    blend_rect(s, wx, yh, wx + wick, yl + 1, col, 1.0f);
    const int bt = yo < yc ? yo : yc;
    int bb = yo < yc ? yc : yo;
    if (bb - bt < 1) bb = bt + 1;
    blend_rect(s, static_cast<int>(lroundf(cx - bw / 2)), bt, static_cast<int>(lroundf(cx + bw / 2)), bb + 1, col, 1.0f);
  }
}

int aggregate(const crypto::Candle *in, int n, crypto::Candle *out, int max_out) {
  if (n <= max_out) {
    memcpy(out, in, sizeof(crypto::Candle) * n);
    return n;
  }
  const int k = (n + max_out - 1) / max_out;
  int m = 0;
  const int start = n - ((n + k - 1) / k) * k;  // groups aligned to the end: the newest one is complete
  for (int g = start; g < n; g += k) {
    const int a = g < 0 ? 0 : g;
    const int b = g + k > n ? n : g + k;
    if (a >= b) continue;
    crypto::Candle c = in[a];
    c.v = 0;
    for (int i = a; i < b; i++) {
      if (in[i].h > c.h) c.h = in[i].h;
      if (in[i].l < c.l) c.l = in[i].l;
      c.v += in[i].v;
    }
    c.c = in[b - 1].c;
    out[m++] = c;
  }
  return m;
}

}  // namespace chart
}  // namespace ui
