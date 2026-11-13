#pragma once
// Screensaver wallpapers (src/assets/wallpapers.c): JPEG streams in flash, decoded with JPEGDEC into
// RGB565 buffers in PSRAM (lv_color_t layout, so LVGL draws them as LV_IMG_CF_TRUE_COLOR images).

#include <lvgl.h>
#include <stdint.h>

namespace ui {
namespace wallpaper {

int count();
const char *name(int idx);  // in the UI language

// A decoded image: owns its PSRAM pixels; dsc points at them.
struct Image {
  lv_img_dsc_t dsc = {};
  uint16_t *px = nullptr;
  int idx = -1;
};

// Full size (WALLPAPER_W x WALLPAPER_H) or quarter size (thumbnails). Reuses img's buffer when the
// size matches. False when out of memory or the stream is corrupt (img keeps its previous pixels).
bool decode(int idx, Image &img, bool quarter = false);
// Any baseline JPEG stream of w x h pixels (the Today weather backgrounds use it too).
bool decode_jpeg(const uint8_t *jpg, uint32_t size, int w, int h, Image &img, bool quarter = false);
void release(Image &img);

}  // namespace wallpaper
}  // namespace ui
