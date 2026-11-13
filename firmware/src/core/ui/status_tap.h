#pragma once
// The status area at the top right of every deck (deck power + PC link LED) answers taps:
//   - a double tap starts the screensaver when it is enabled (Settings > Screensaver);
//   - a single tap runs the deck's own action (the grey LED leads to Wi-Fi setup or pairing).
// LVGL 8 has no double-click event: two clicks within kDoubleTapMs make one. While the screensaver
// is enabled a lone tap waits out that window before acting; while it is disabled it acts at once.

#include <lvgl.h>

namespace ui {

constexpr uint32_t kDoubleTapMs = 400;  // from the first release to the second

// `area` becomes the only target inside it (its children stop taking clicks, so a tap on the LED or
// the battery label counts as a tap on the area). Call it again after a UI rebuild.
void attach_status_tap(lv_obj_t *area, void (*on_single)());

}  // namespace ui
