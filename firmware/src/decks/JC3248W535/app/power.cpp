#include "power.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "board/backlight.h"
#include "board/lcd_axs15231b.h"
#include "board/lvgl_port.h"
#include "board/touch_axs15231b.h"
#include "board/wake_touch.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "network/net_task.h"
#include "ui/ui.h"

namespace power {
namespace {

const char *TAG = "power";
constexpr gpio_num_t kTouchInt = static_cast<gpio_num_t>(PCD_PIN_TOUCH_INT);
constexpr gpio_num_t kBootBtn = GPIO_NUM_0;
constexpr gpio_num_t kBacklight = static_cast<gpio_num_t>(PCD_PIN_LCD_BL);

bool s_woke = false;
uint32_t s_int_lows = 0;

void on_touch_wake() {
  backlight::set(app::screen_brightness());
  ESP_LOGI(TAG, "screen on (touch)");
}

bool finger_down() {
  touch::Point p;
  return touch::read(p) && p.pressed;
}

// Waits (<= max_ms) until INT has stayed released `quiet_ms`. Reading the controller meanwhile clears
// any report still pending, so INT is released. With `watch`, returns true as soon as a finger is
// down in two reads in a row (it is a real touch: stay awake).
bool wait_int_released(uint32_t quiet_ms, uint32_t max_ms, bool watch) {
  const uint32_t t0 = millis();
  uint32_t high_since = 0;
  int run = 0;
  while (millis() - t0 < max_ms) {
    run = finger_down() ? run + 1 : 0;
    if (watch && run >= 2) return true;
    if (digitalRead(PCD_PIN_TOUCH_INT) == HIGH) {
      if (!high_since) high_since = millis();
      if (millis() - high_since > quiet_ms) break;
    } else {
      high_since = 0;
    }
    delay(10);
  }
  return false;
}

// Wake lines (touch INT, BOOT) armed, then deep sleep. The backlight pin must already be held low.
[[noreturn]] void arm_and_sleep() {
  gpio_deep_sleep_hold_en();
  rtc_gpio_init(kTouchInt);
  rtc_gpio_set_direction(kTouchInt, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(kTouchInt);
  rtc_gpio_pulldown_dis(kTouchInt);
  rtc_gpio_init(kBootBtn);
  rtc_gpio_set_direction(kBootBtn, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(kBootBtn);
  rtc_gpio_pulldown_dis(kBootBtn);
  // ext1 alone lets the RTC peripherals power down, which drops the internal pull-ups above: INT
  // would float. Keep the domain on so the lines idle high.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup((1ULL << kTouchInt) | (1ULL << kBootBtn), ESP_EXT1_WAKEUP_ANY_LOW);
  Serial.flush();
  delay(50);
  esp_deep_sleep_start();
}

// Woken by the touch INT line: was it a finger? Runs before anything else is up (Wi-Fi off, panel
// as it was left, backlight still held low), so a glitch goes back to sleep without lighting the
// screen. Returns only when the deck stays awake.
void check_touch_wake() {
  touch::init();
  if (wake_touch::boot_check(finger_down, 150) != wake_touch::Boot::kFalse) return;
  if (wait_int_released(300, 1000, true)) {  // the second tap came while we were still awake
    ESP_LOGI(TAG, "touch wake: finger while going back to sleep, staying awake");
    return;
  }
  wake_touch::going_back_to_sleep();
  arm_and_sleep();
}

}  // namespace

void init() {
  s_woke = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
  const uint64_t mask = s_woke ? esp_sleep_get_ext1_wakeup_status() : 0;
  // Pins configured for deep sleep stay in RTC mode / held until released explicitly. The backlight
  // stays held low until the wake is known to be a real one.
  gpio_deep_sleep_hold_dis();
  rtc_gpio_deinit(kTouchInt);
  rtc_gpio_deinit(kBootBtn);
  pinMode(PCD_PIN_TOUCH_INT, INPUT_PULLUP);
  pinMode(kBootBtn, INPUT_PULLUP);
  if (s_woke) ESP_LOGI(TAG, "woke from deep sleep (ext1 mask 0x%llx)", mask);
  if (s_woke && (mask & (1ULL << kTouchInt)) && !(mask & (1ULL << kBootBtn))) check_touch_wake();
  gpio_hold_dis(kBacklight);
  lvgl_port::set_wake_callback(on_touch_wake);
}

void poll(uint32_t) {
  if (digitalRead(PCD_PIN_TOUCH_INT) == LOW) s_int_lows++;
  // BOOT button: always turns a dark screen back on (fallback to touch); closes the screensaver.
  static bool boot_was_low = false;
  const bool boot_low = digitalRead(kBootBtn) == LOW;
  if (boot_low && !boot_was_low) {
    if (ui::screensaver::active())
      ui::screensaver::stop();
    else
      screen_on();
  }
  boot_was_low = boot_low;
}

void screen_off() {
  if (lvgl_port::screen_off()) return;
  ui::screensaver::stop();
  // Backlight only. The AXS15231B is a TDDI chip: DISPOFF also stops touch scanning, so a
  // touch could never wake the screen again. Frames stop being pushed (lvgl_port).
  backlight::set(0);
  lvgl_port::set_screen_off(true);
  ESP_LOGI(TAG, "screen off");
}

void screen_on() {
  if (!lvgl_port::screen_off()) return;
  lvgl_port::set_screen_off(false);
  on_touch_wake();
}

bool is_screen_off() { return lvgl_port::screen_off(); }
bool woke_from_deep_sleep() { return s_woke; }
uint32_t touch_int_lows() { return s_int_lows; }

void deep_sleep() {
  ESP_LOGI(TAG, "entering deep sleep (wake: touch INT GPIO%d or BOOT)", PCD_PIN_TOUCH_INT);
  ui::screensaver::stop();  // (restores the backlight level in RAM only; it goes dark right below)
  backlight::set(0);
  // The finger that asked for sleep must be gone, or INT (active low) wakes us at once.
  wait_int_released(400, 4000, false);
  net::set_enabled(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
#if PCD_DEEP_SLEEP_PANEL_SLPIN
  // Lowest power, but the AXS15231B is a TDDI chip: in SLPIN its touch core only scans in a slow
  // low-power mode, so it usually takes several taps before INT fires and wakes the deck.
  lcd::sleep_in();
#else
  // The panel stays in normal mode (backlight held off, nothing visible) so the touch core keeps
  // scanning at its normal rate and the first tap pulls INT low. Costs a few mA while asleep; the
  // panel is re-initialised from scratch on wake.
#endif

  // Backlight PWM stops in deep sleep: drive the pin low and hold it there.
  ledcDetach(PCD_PIN_LCD_BL);
  pinMode(PCD_PIN_LCD_BL, OUTPUT);
  digitalWrite(PCD_PIN_LCD_BL, LOW);
  gpio_hold_en(kBacklight);
  arm_and_sleep();
}

}  // namespace power
