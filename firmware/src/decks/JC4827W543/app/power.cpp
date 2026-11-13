#include "power.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app/state_store.h"
#include "board/backlight.h"
#include "board/lcd_nv3041a.h"
#include "board/lvgl_port.h"
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
constexpr gpio_num_t kBtn[2] = {static_cast<gpio_num_t>(PCD_PIN_SLEEP_BTN_A), static_cast<gpio_num_t>(PCD_PIN_SLEEP_BTN_B)};

constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kArmIdleMs = 250;      // a pin must be idle this long before a tap counts (wake finger)
constexpr uint32_t kRelearnMs = 10000;    // "active" for this long: the learnt idle level was wrong

// Survives deep sleep: idle level learnt at cold boot (a woken deck may still feel the finger).
RTC_DATA_ATTR int8_t r_idle[2] = {-1, -1};

bool s_woke = false;
const char *s_wake = "";
uint32_t s_int_lows = 0;
uint32_t s_taps = 0;

struct Btn {
  int idle = 0;
  int stable = 0;        // debounced level
  int raw = 0;
  uint32_t raw_since = 0;
  uint32_t stable_since = 0;
  bool armed = false;
};
Btn s_btn[2];

void on_touch_wake() {
  backlight::set(app::state().settings.brightness);
  ESP_LOGI(TAG, "screen on (touch)");
}

// Majority of a few samples, ~1 ms apart.
int sample(gpio_num_t pin, int n = 15) {
  int hi = 0;
  for (int i = 0; i < n; i++) {
    hi += gpio_get_level(pin);
    delayMicroseconds(1000);
  }
  return hi * 2 > n ? 1 : 0;
}

// Level the line keeps while nobody touches it: wait (<= max_ms) for `quiet_ms` without a change.
int idle_level(gpio_num_t pin, uint32_t quiet_ms, uint32_t max_ms) {
  const uint32_t t0 = millis();
  int last = gpio_get_level(pin);
  uint32_t since = t0;
  while (millis() - t0 < max_ms) {
    const int v = gpio_get_level(pin);
    if (v != last) {
      last = v;
      since = millis();
    } else if (millis() - since >= quiet_ms) {
      break;
    }
    delay(2);
  }
  return last;
}

void button_tap() {
  s_taps++;
  const uint8_t action = app::state().settings.button_action;
  ESP_LOGI(TAG, "sleep button tap (action %u)", action);
  ui::screensaver::stop();  // then the configured action, as without the screensaver
  if (action == 1) {  // screen off/on
    if (lvgl_port::screen_off())
      screen_on();
    else
      screen_off();
    return;
  }
  deep_sleep();
}

void poll_button(int i, uint32_t now) {
  Btn &b = s_btn[i];
  const int v = gpio_get_level(kBtn[i]);
  if (v != b.raw) {
    b.raw = v;
    b.raw_since = now;
  }
  if (b.raw != b.stable && now - b.raw_since >= kDebounceMs) {
    b.stable = b.raw;
    b.stable_since = now;
    if (b.stable != b.idle && b.armed) {
      b.armed = false;
      button_tap();
    }
  }
  if (b.stable == b.idle) {
    if (!b.armed && now - b.stable_since >= kArmIdleMs) b.armed = true;
  } else if (now - b.stable_since >= kRelearnMs) {
    // Held "active" for 10 s: this is the real idle level (e.g. learnt while the finger was there).
    b.idle = b.stable;
    r_idle[i] = static_cast<int8_t>(b.idle);
    b.stable_since = now;
    ESP_LOGW(TAG, "sleep button GPIO%d: idle level re-learnt as %d", kBtn[i], b.idle);
  }
}

}  // namespace

void init() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  s_woke = cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_EXT0;
  if (s_woke) {
    uint64_t mask = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
    if (cause == ESP_SLEEP_WAKEUP_EXT0) s_wake = "ext0";
    if (mask & ((1ULL << kBtn[0]) | (1ULL << kBtn[1]))) s_wake = "button";
    else if (mask & (1ULL << kTouchInt)) s_wake = "touch";
    else if (mask & (1ULL << kBootBtn)) s_wake = "boot";
    ESP_LOGI(TAG, "woke from deep sleep (%s, ext1 mask 0x%llx)", s_wake, mask);
  }
  // Pins configured for deep sleep stay in RTC mode / held until released explicitly.
  gpio_hold_dis(kBacklight);
  gpio_deep_sleep_hold_dis();
  for (gpio_num_t p : {kTouchInt, kBootBtn, kBtn[0], kBtn[1]}) rtc_gpio_deinit(p);
  pinMode(PCD_PIN_TOUCH_INT, INPUT_PULLUP);
  pinMode(kBootBtn, INPUT_PULLUP);
  // Sleep button: pull-down, so a pin with nothing attached reads a steady low and never fires.
  const uint32_t now = millis();
  for (int i = 0; i < 2; i++) {
    pinMode(kBtn[i], INPUT_PULLDOWN);
    Btn &b = s_btn[i];
    b.idle = (r_idle[i] == 0 || r_idle[i] == 1) && s_woke ? r_idle[i] : sample(kBtn[i]);
    r_idle[i] = static_cast<int8_t>(b.idle);
    b.raw = b.stable = gpio_get_level(kBtn[i]);
    b.raw_since = b.stable_since = now;
    b.armed = false;  // only after kArmIdleMs at the idle level (the finger that woke us must lift)
    ESP_LOGI(TAG, "sleep button GPIO%d idle=%d", kBtn[i], b.idle);
  }
  lvgl_port::set_wake_callback(on_touch_wake);
}

void poll(uint32_t now) {
  if (digitalRead(PCD_PIN_TOUCH_INT) == LOW) s_int_lows++;
  // BOOT button: closes the screensaver, or turns a dark screen back on (fallback to touch). GPIO0
  // also carries the panel's TE output, which stays quiet because tearing output is never enabled.
  static bool boot_was_low = false;
  const bool boot_low = digitalRead(kBootBtn) == LOW;
  if (boot_low && !boot_was_low) {
    if (ui::screensaver::active())
      ui::screensaver::stop();
    else
      screen_on();
  }
  boot_was_low = boot_low;
  for (int i = 0; i < 2; i++) poll_button(i, now);
}

void screen_off() {
  if (lvgl_port::screen_off()) return;
  ui::screensaver::stop();
  // Backlight only: the panel keeps its frame, touch keeps scanning so a tap wakes it.
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
const char *wake_reason() { return s_wake; }
uint32_t touch_int_lows() { return s_int_lows; }

void button_info(int &idle_a, int &idle_b, uint32_t &taps) {
  idle_a = s_btn[0].idle;
  idle_b = s_btn[1].idle;
  taps = s_taps;
}

void deep_sleep() {
  ESP_LOGI(TAG, "entering deep sleep");
  ui::screensaver::stop();
  backlight::set(0);
  // The finger / button that asked for sleep must be gone, or a level wake fires at once.
  const uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    bool released = true;
    for (int i = 0; i < 2; i++)
      if (gpio_get_level(kBtn[i]) != s_btn[i].idle) released = false;
    if (released) break;
    delay(10);
  }
  // Idle level of each wake line, measured now (the GT911 INT polarity depends on its config).
  const int int_idle = idle_level(kTouchInt, 400, 4000);
  const int boot_idle = idle_level(kBootBtn, 100, 500);

  net::set_enabled(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  lcd::sleep_in();

  // Backlight PWM stops in deep sleep: drive the pin low and hold it there.
  ledcDetach(PCD_PIN_LCD_BL);
  pinMode(PCD_PIN_LCD_BL, OUTPUT);
  digitalWrite(PCD_PIN_LCD_BL, LOW);
  gpio_hold_en(kBacklight);
  gpio_deep_sleep_hold_en();

  // Wake lines in priority order; ext1 takes every line of the sleep button's active level, ext0 the
  // first line of the other level (ext1 has a single trigger level on the ESP32-S3).
  struct Line {
    gpio_num_t pin;
    int idle;
  };
  const Line lines[] = {{kBtn[0], s_btn[0].idle}, {kBtn[1], s_btn[1].idle}, {kTouchInt, int_idle}, {kBootBtn, boot_idle}};
  const int ext1_active = s_btn[0].idle ? 0 : 1;  // the button's active level
  uint64_t ext1_mask = 0;
  int ext0_pin = -1, ext0_level = 0;
  for (const Line &l : lines) {
    const int active = l.idle ? 0 : 1;
    rtc_gpio_init(l.pin);
    rtc_gpio_set_direction(l.pin, RTC_GPIO_MODE_INPUT_ONLY);
    if (active) {
      rtc_gpio_pulldown_en(l.pin);
      rtc_gpio_pullup_dis(l.pin);
    } else {
      rtc_gpio_pullup_en(l.pin);
      rtc_gpio_pulldown_dis(l.pin);
    }
    if (active == ext1_active) {
      ext1_mask |= 1ULL << l.pin;
    } else if (ext0_pin < 0) {
      ext0_pin = l.pin;
      ext0_level = active;
    }
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  // keep the pulls alive
  esp_sleep_enable_ext1_wakeup(ext1_mask, ext1_active ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
  if (ext0_pin >= 0) esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(ext0_pin), ext0_level);
  ESP_LOGI(TAG, "wake: ext1 mask 0x%llx on %s, ext0 GPIO%d on %s (INT idle %d, BOOT idle %d)", ext1_mask,
           ext1_active ? "high" : "low", ext0_pin, ext0_level ? "high" : "low", int_idle, boot_idle);
  Serial.flush();
  delay(50);
  esp_deep_sleep_start();
}

}  // namespace power
