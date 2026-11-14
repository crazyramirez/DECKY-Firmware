#pragma once
// The deck's battery as a small badge, shared by every deck (the screensaver's top-right corner): a
// drawn battery whose fill follows the charge, optionally with the percentage before it. Shown only
// while the deck runs on its battery (never on USB) and as Settings > Screensaver > Battery says
// (off / icon / icon and %). Decks that cannot sense a battery (battery::sensed()) never show it.

#include <lvgl.h>
#include <stdint.h>

namespace ui {

struct BatteryBadge {
  lv_obj_t *root = nullptr;  // row: [78 %] [battery]; align it where the deck wants it
  lv_obj_t *pct = nullptr, *body = nullptr, *fill = nullptr, *cap = nullptr;
  int inner_w = 0;  // fill width at 100 %
  int8_t shown_pct = -2;
  int8_t shown_mode = -1;
};

// body_w x body_h: the battery's outline (the cap adds a little to the right).
void battery_badge_create(BatteryBadge &b, lv_obj_t *parent, const lv_font_t *font, int body_w, int body_h);
// From battery::state() and the setting; cheap when nothing changed (call it every second).
void battery_badge_update(BatteryBadge &b);
// Developer aid (simulator): show this level as if on battery, on any deck; -1 = the real one.
void battery_badge_dev(int pct);

}  // namespace ui
