#include "ui/status_tap.h"

#include "app/state_store.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace {

void (*s_single)() = nullptr;
lv_timer_t *s_pending = nullptr;  // a first tap waiting for a second one

void cancel_pending() {
  if (s_pending) lv_timer_del(s_pending);
  s_pending = nullptr;
}

void pending_cb(lv_timer_t *) {
  s_pending = nullptr;  // one-shot: LVGL deletes the timer after this call
  if (s_single) s_single();
}

void clicked_cb(lv_event_t *) {
  if (!app::state().settings.ss.enabled) {  // no double tap to wait for
    if (s_single) s_single();
    return;
  }
  if (s_pending) {  // second tap in time: the screensaver instead of the single tap's action
    cancel_pending();
    if (!screensaver::active() && !setup::active()) screensaver::start();
    return;
  }
  s_pending = lv_timer_create(pending_cb, kDoubleTapMs, nullptr);
  lv_timer_set_repeat_count(s_pending, 1);
}

void clear_clickable(lv_obj_t *obj) {
  const uint32_t n = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *ch = lv_obj_get_child(obj, static_cast<int32_t>(i));
    lv_obj_clear_flag(ch, LV_OBJ_FLAG_CLICKABLE);
    clear_clickable(ch);
  }
}

}  // namespace

void attach_status_tap(lv_obj_t *area, void (*on_single)()) {
  cancel_pending();  // a UI rebuild deletes the old area
  s_single = on_single;
  lv_obj_add_flag(area, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);
  clear_clickable(area);
  lv_obj_add_event_cb(area, clicked_cb, LV_EVENT_CLICKED, nullptr);
}

}  // namespace ui
