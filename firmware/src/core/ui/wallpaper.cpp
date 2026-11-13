#include "wallpaper.h"

#include <JPEGDEC.h>
#include <new>
#include <string.h>

#include "app/state_store.h"
#include "assets/wallpapers.h"
#include "esp_heap_caps.h"
#include "lv_conf.h"

namespace ui {
namespace wallpaper {
namespace {

struct Target {
  uint16_t *px;
  int w, h;
};

// One decoded block (MCU row chunk): copy the visible part into the target frame.
int draw_cb(JPEGDRAW *d) {
  const Target *t = static_cast<const Target *>(d->pUser);
  const int w = d->x + d->iWidth > t->w ? t->w - d->x : d->iWidth;
  for (int row = 0; row < d->iHeight && d->y + row < t->h; row++) {
    if (w > 0) memcpy(t->px + (d->y + row) * t->w + d->x, d->pPixels + row * d->iWidth, w * sizeof(uint16_t));
  }
  return 1;
}

}  // namespace

int count() { return WALLPAPER_COUNT; }

const char *name(int idx) {
  if (idx < 0 || idx >= WALLPAPER_COUNT) return "";
  return app::state().settings.lang == storage::Lang::En ? WALLPAPERS[idx].name_en : WALLPAPERS[idx].name_es;
}

bool decode(int idx, Image &img, bool quarter) {
  if (idx < 0 || idx >= WALLPAPER_COUNT) return false;
  if (!decode_jpeg(WALLPAPERS[idx].jpg, WALLPAPERS[idx].jpg_size, WALLPAPER_W, WALLPAPER_H, img, quarter)) return false;
  img.idx = idx;
  return true;
}

bool decode_jpeg(const uint8_t *jpg, uint32_t size, int full_w, int full_h, Image &img, bool quarter) {
  const int w = quarter ? full_w / 4 : full_w;
  const int h = quarter ? full_h / 4 : full_h;
  const size_t bytes = static_cast<size_t>(w) * h * sizeof(uint16_t);
  if (img.px && (img.dsc.header.w != w || img.dsc.header.h != h)) release(img);
  if (!img.px) {
    img.px = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!img.px) return false;
  }
  // The decoder state (~20 KB) lives in PSRAM only while decoding: internal RAM is for Wi-Fi.
  void *mem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) return false;
  JPEGDEC *jpeg = new (mem) JPEGDEC();
  Target t{img.px, w, h};
  bool ok = jpeg->openFLASH(const_cast<uint8_t *>(jpg), static_cast<int>(size), draw_cb) != 0;
  if (ok) {
    jpeg->setUserPointer(&t);
    jpeg->setPixelType(LV_COLOR_16_SWAP ? RGB565_BIG_ENDIAN : RGB565_LITTLE_ENDIAN);
    ok = jpeg->decode(0, 0, quarter ? JPEG_SCALE_QUARTER : 0) != 0;
    jpeg->close();
  }
  jpeg->~JPEGDEC();
  heap_caps_free(mem);
  if (!ok) return false;
  memset(&img.dsc, 0, sizeof(img.dsc));
  img.dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  img.dsc.header.w = w;
  img.dsc.header.h = h;
  img.dsc.data_size = bytes;
  img.dsc.data = reinterpret_cast<const uint8_t *>(img.px);
  return true;
}

void release(Image &img) {
  if (img.px) heap_caps_free(img.px);
  img = Image{};
}

}  // namespace wallpaper
}  // namespace ui
