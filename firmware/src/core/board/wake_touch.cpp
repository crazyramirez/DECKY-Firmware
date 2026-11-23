#include "board/wake_touch.h"

#include <Arduino.h>
#include <sys/time.h>

#include "deck_config.h"
#include "esp_attr.h"

namespace wake_touch {
namespace {

const char *TAG = "wake";
uint32_t s_ignored = 0;
uint32_t s_check_at = 0;
Boot s_last = Boot::kNone;

#if PCD_HAS_DEEP_SLEEP
// Survive deep sleep (zeroed at a cold boot).
RTC_DATA_ATTR uint32_t r_false = 0, r_real = 0;
RTC_DATA_ATTR int64_t r_false_at_us = 0;  // when the last wake was sent back to sleep (0: none pending)
RTC_DATA_ATTR TrailItem r_trail[kTrailLen];
RTC_DATA_ATTR uint32_t r_trail_n = 0;  // items ever written (ring index = n % kTrailLen)

uint16_t ms16(uint32_t ms) { return static_cast<uint16_t>(ms > 0xFFFF ? 0xFFFF : ms); }

// System time keeps running through deep sleep (RTC timer), unlike millis().
int64_t now_us() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}
#endif

}  // namespace

bool Filter::feed(bool down, int x, int y, uint32_t now_ms, int tol) {
  if (!down) {
    if (active_) s_ignored++;  // lifted before it counted: a glitch, or a brush of the hand
    active_ = false;
    lifted_ = true;
    return false;
  }
  if (!lifted_ || fired_) return false;
  const int dx = x - x0_, dy = y - y0_;
  if (!active_ || dx * dx + dy * dy > tol * tol) {
    // New press, or the point jumped (noise reports land anywhere; a finger stays put): start over here.
    active_ = true;
    t0_ = now_ms;
    x0_ = x;
    y0_ = y;
    reads_ = 1;
    return false;
  }
  if (reads_ < 255) reads_++;
  if (reads_ >= kMinReads && now_ms - t0_ >= kHoldMs) {
    active_ = false;
    fired_ = true;
    return true;
  }
  return false;
}

Boot boot_check(bool (*read)(), uint32_t window_ms) {
  s_check_at = millis();
#if PCD_HAS_DEEP_SLEEP
  // Two reads in a row with the finger down (a single report can be noise).
  Boot r = Boot::kFalse;
  int run = 0;
  for (const uint32_t t0 = millis(); millis() - t0 < window_ms;) {
    run = read() ? run + 1 : 0;
    if (run >= 2) {
      r = Boot::kFinger;
      break;
    }
    delay(10);
  }
  const int64_t now = now_us();
  if (r == Boot::kFalse && r_false_at_us && now >= r_false_at_us &&
      now - r_false_at_us < static_cast<int64_t>(kRetapMs) * 1000)
    r = Boot::kRetap;  // the same quick tap, repeated: the user wants the deck
  if (r == Boot::kFalse) {
    r_false++;
    r_false_at_us = now;
  } else {
    r_real++;
    r_false_at_us = 0;
  }
  s_last = r;
  r_trail[r_trail_n % kTrailLen] = {r, ms16(s_check_at), 0};
  r_trail_n++;
  ESP_LOGI(TAG, "touch wake at %u ms: %s (%u real, %u sent back to sleep)", static_cast<unsigned>(s_check_at),
           name(r), static_cast<unsigned>(r_real), static_cast<unsigned>(r_false));
  return r;
#else
  (void)read;
  (void)window_ms;
  s_last = Boot::kFinger;
  return Boot::kFinger;
#endif
}

Stats stats() {
  Stats s;
  s.ignored = s_ignored;
#if PCD_HAS_DEEP_SLEEP
  s.boot_false = r_false;
  s.boot_real = r_real;
#endif
  s.check_at_ms = s_check_at;
  s.last = s_last;
  return s;
}

void going_back_to_sleep() {
#if PCD_HAS_DEEP_SLEEP
  if (r_trail_n) r_trail[(r_trail_n - 1) % kTrailLen].sleep_ms = ms16(millis());
#endif
}

int trail(TrailItem *out) {
#if PCD_HAS_DEEP_SLEEP
  const uint32_t n = r_trail_n < static_cast<uint32_t>(kTrailLen) ? r_trail_n : kTrailLen;
  for (uint32_t i = 0; i < n; i++) out[i] = r_trail[(r_trail_n - n + i) % kTrailLen];
  return static_cast<int>(n);
#else
  (void)out;
  return 0;
#endif
}

const char *name(Boot b) {
  switch (b) {
    case Boot::kFinger: return "finger";
    case Boot::kRetap: return "repeated tap";
    case Boot::kFalse: return "no finger";
    default: return "-";
  }
}

}  // namespace wake_touch
