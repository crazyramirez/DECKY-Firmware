#include "app/event_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace app {
namespace bus {
namespace {
QueueHandle_t s_q = nullptr;
volatile uint32_t s_dropped = 0;
}  // namespace

bool init(int depth) {
  if (s_q) return true;
  s_q = xQueueCreate(depth, sizeof(Event));
  return s_q != nullptr;
}

bool post(const Event &e, uint32_t timeout_ms) {
  if (!s_q) return false;
  if (xQueueSend(s_q, &e, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    s_dropped = s_dropped + 1;
    return false;
  }
  return true;
}

bool poll(Event &out) { return s_q && xQueueReceive(s_q, &out, 0) == pdTRUE; }

uint32_t dropped() { return s_dropped; }

}  // namespace bus
}  // namespace app
