#pragma once
// Only a real finger wakes the deck (every deck). A stray report of the touch controller, ESD, a
// charger's noise or a brush of the hand is a frame or two; a finger stays on the glass.
//  - Screen off (lvgl_port): Filter, fed with every touch read. A press wakes the screen once it has
//    stayed down kHoldMs (kMinReads reads) on about the same spot, and only a press that started
//    after the screen went dark (the finger that turned it off must lift first).
//  - Deep sleep (PCD_HAS_DEEP_SLEEP): a wake from the touch INT line restarts the chip, so one glitch
//    on INT was enough. boot_check() looks for the finger right after the restart, before the
//    backlight comes on; none → the deck goes back to sleep and the screen never lights up. A tap
//    too short to still be there after the restart wakes it when repeated within kRetapMs.
#include <stdint.h>

namespace wake_touch {

constexpr uint32_t kHoldMs = 60;     // screen off: finger down this long
constexpr uint8_t kMinReads = 3;     // ... over at least this many reads (LVGL reads every 20 ms)
constexpr uint32_t kRetapMs = 4000;  // deep sleep: a second touch wake within this long is a real tap

class Filter {
 public:
  // The screen just went dark: a finger already down must lift before it can wake it.
  void reset() { *this = Filter(); }
  // One touch read while the screen is dark; true (once) when the press is confirmed. `tol` is the
  // drift allowed, in pixels.
  bool feed(bool down, int x, int y, uint32_t now_ms, int tol);

 private:
  bool lifted_ = false, active_ = false, fired_ = false;
  uint32_t t0_ = 0;
  int x0_ = 0, y0_ = 0;
  uint8_t reads_ = 0;
};

// Drift allowed to a waking finger on a w x h panel: 6 % of the shorter side.
inline int tolerance(int w, int h) { return (w < h ? w : h) * 6 / 100; }

enum class Boot : uint8_t { kNone, kFinger, kRetap, kFalse };

// Deep-sleep decks, right after a wake from the touch INT line: `read` polls the controller once and
// returns true while a finger is down. kFinger / kRetap: wake; kFalse: go back to sleep.
Boot boot_check(bool (*read)(), uint32_t window_ms);

struct Stats {
  uint32_t ignored = 0;       // screen off: touches too short / moving to wake it (since boot)
  uint32_t boot_false = 0;    // deep sleep: wakes sent back to sleep (since the last cold boot)
  uint32_t boot_real = 0;     // deep sleep: touch wakes confirmed (since the last cold boot)
  uint32_t check_at_ms = 0;   // last boot_check: millis() when it started (time from the wake)
  Boot last = Boot::kNone;    // last boot_check result
};
Stats stats();
const char *name(Boot b);

// Deep-sleep decks: call right before going back to sleep after kFalse (stamps how long it was up).
void going_back_to_sleep();

// Diagnostics, kept through deep sleep: the last touch wakes, oldest first.
struct TrailItem {
  Boot result;
  uint16_t check_ms;  // millis() when the check started
  uint16_t sleep_ms;  // millis() when it went back to sleep (0: it stayed awake)
};
constexpr int kTrailLen = 8;
int trail(TrailItem *out);  // returns how many (<= kTrailLen)

}  // namespace wake_touch
