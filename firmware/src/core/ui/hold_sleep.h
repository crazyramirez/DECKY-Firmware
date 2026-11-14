#pragma once
// Hold to sleep, shared by every deck's screensaver: keep a finger anywhere on it for 2 s and the
// deck goes to sleep, as auto sleep would (deep sleep on the decks that wake from it,
// PCD_HAS_DEEP_SLEEP; else the screen turns off). After a short moment a ring fills in the middle of
// the screen while the picture dims, so the hold is visible and lifting the finger (or sliding it
// away) cancels. A short tap keeps its job (close, or open a card's page).

#include <lvgl.h>

namespace ui {
namespace hold_sleep {

// The screensaver's root: the ring and the dimming are drawn over it (created on demand).
void begin(lv_obj_t *root);
// Every object of the screensaver that takes the touch (the root, its cards / shortcuts).
void attach(lv_obj_t *o);
// The screensaver closed: forget the root (its children go with it).
void end();
// Developer aid (simulator): show the ring at this progress (0-100); -1 hides it.
void preview(int pct);

}  // namespace hold_sleep
}  // namespace ui
