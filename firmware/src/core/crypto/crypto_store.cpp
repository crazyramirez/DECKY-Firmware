#include "crypto_store.h"

#include <Arduino.h>
#include <new>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace crypto {
namespace {

constexpr int kChartSlots = 4;

struct Slot {
  Chart chart;
  uint32_t rx_ms;
  uint32_t used_ms;
  bool used;
};

SemaphoreHandle_t s_mtx = nullptr;
Market *s_market = nullptr;
bool s_have_market = false;
uint32_t s_market_rx = 0;
char s_error[24] = "";
Slot *s_slots = nullptr;

struct Lock {
  Lock() { xSemaphoreTake(s_mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mtx); }
};

}  // namespace

bool store_init() {
  if (s_mtx) return true;
  s_mtx = xSemaphoreCreateMutex();
  void *m = heap_caps_malloc(sizeof(Market), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  void *sl = heap_caps_calloc(kChartSlots, sizeof(Slot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_mtx || !m || !sl) return false;
  s_market = new (m) Market;
  reset(*s_market);
  s_slots = static_cast<Slot *>(sl);
  return true;
}

void store_market(const Market &m) {
  if (!s_mtx) return;
  Lock l;
  strlcpy(s_error, m.ok ? "" : m.error, sizeof(s_error));
  if (!m.ok) return;  // keep showing the last good prices
  memcpy(s_market, &m, sizeof(Market));
  s_have_market = true;
  s_market_rx = millis();
}

void store_chart(const Chart &c) {
  if (!s_mtx) return;
  Lock l;
  Slot *slot = nullptr;
  for (int i = 0; i < kChartSlots; i++) {
    Slot &s = s_slots[i];
    if (s.used && !strcmp(s.chart.pair, c.pair) && s.chart.range == c.range && !strcmp(s.chart.cur, c.cur)) {
      slot = &s;
      break;
    }
  }
  if (slot && !c.ok && slot->chart.ok) return;  // keep stale data rather than an error
  if (!slot) {  // free slot, else least recently used
    slot = &s_slots[0];
    for (int i = 0; i < kChartSlots; i++) {
      if (!s_slots[i].used) {
        slot = &s_slots[i];
        break;
      }
      if (s_slots[i].used_ms < slot->used_ms) slot = &s_slots[i];
    }
  }
  memcpy(&slot->chart, &c, sizeof(Chart));
  slot->used = true;
  slot->rx_ms = slot->used_ms = millis();
}

bool get_market(Market &out, uint32_t *rx_ms) {
  if (!s_mtx) return false;
  Lock l;
  if (!s_have_market) return false;
  memcpy(&out, s_market, sizeof(Market));
  if (rx_ms) *rx_ms = s_market_rx;
  return true;
}

void market_error(char *out, unsigned n) {
  if (!n) return;
  out[0] = '\0';
  if (!s_mtx) return;
  Lock l;
  strlcpy(out, s_error, n);
}

bool get_chart(const char *pair, uint8_t range, const char *cur, Chart &out, uint32_t *age_ms) {
  if (!s_mtx) return false;
  Lock l;
  for (int i = 0; i < kChartSlots; i++) {
    Slot &s = s_slots[i];
    if (s.used && !strcmp(s.chart.pair, pair) && s.chart.range == range && !strcmp(s.chart.cur, cur)) {
      memcpy(&out, &s.chart, sizeof(Chart));
      s.used_ms = millis();
      if (age_ms) *age_ms = millis() - s.rx_ms;
      return true;
    }
  }
  return false;
}

void clear_charts() {
  if (!s_mtx) return;
  Lock l;
  for (int i = 0; i < kChartSlots; i++) s_slots[i].used = false;
}

}  // namespace crypto
