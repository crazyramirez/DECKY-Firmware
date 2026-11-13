#include "backlight.h"

#include <Arduino.h>

#include "board_pins.h"
#include "deck_config.h"

namespace backlight {
namespace {
constexpr uint32_t kFreqHz = PCD_BACKLIGHT_HZ;  // x 1024 steps: reachable from every LEDC clock source
constexpr uint8_t kBits = 10;
constexpr uint32_t kMaxDuty = (1u << kBits) - 1;
uint8_t s_percent = 0;
bool s_ready = false;
}  // namespace

bool init(uint8_t percent) {
  s_ready = ledcAttach(PCD_PIN_LCD_BL, kFreqHz, kBits);
  if (!s_ready) {
    ESP_LOGE("backlight", "ledcAttach(GPIO%d) failed", PCD_PIN_LCD_BL);
    return false;
  }
  set(percent);
  return true;
}

void set(uint8_t percent) {
  if (percent > 100) percent = 100;
  s_percent = percent;
  if (s_ready) ledcWrite(PCD_PIN_LCD_BL, (kMaxDuty * percent) / 100);
}

uint8_t get() { return s_percent; }

}  // namespace backlight
