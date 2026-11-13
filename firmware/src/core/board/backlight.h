#pragma once
#include <stdint.h>

// Backlight (PCD_PIN_LCD_BL) is PWM-driven (LEDC) at PCD_BACKLIGHT_HZ (deck_config.h): a plain
// digital HIGH is dim/unreliable on the S3 boards; the 7" runs at 20 kHz so its driver never whines.
namespace backlight {

bool init(uint8_t percent);  // PCD_BACKLIGHT_HZ, 10-bit
void set(uint8_t percent);   // 0..100, clamped
uint8_t get();

}  // namespace backlight
