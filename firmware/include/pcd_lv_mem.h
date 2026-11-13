#pragma once
/*
 * LVGL heap in PSRAM (lv_conf.h LV_MEM_CUSTOM_*).
 *
 * Wi-Fi, the WebSocket client, FreeRTOS and the DMA bounce buffers need the ~300 KB of internal RAM;
 * with every page built (Crypto lists, the coin picker...) LVGL's many small objects left only
 * ~9 KB free once Wi-Fi was up. Widgets, styles and draw layers live in the 8 MB PSRAM instead
 * (cached, fast enough for the UI); internal RAM is only used if PSRAM is exhausted.
 */
#include <stddef.h>

#include "esp_heap_caps.h"

#define PCD_LV_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static inline void *pcd_lv_malloc(size_t size) {
  void *p = heap_caps_malloc(size, PCD_LV_CAPS);
  return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

static inline void *pcd_lv_realloc(void *ptr, size_t size) {
  void *p = heap_caps_realloc(ptr, size, PCD_LV_CAPS);
  return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}

static inline void pcd_lv_free(void *ptr) { heap_caps_free(ptr); }
