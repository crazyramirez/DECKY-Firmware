#include "ui/screen_cover.h"

namespace ui {

namespace {

// A visible top-layer object that fully and opaquely covers the display (LVGL's own cover check:
// background opacity, radius, opacity, transforms...).
bool top_layer_covers(lv_disp_t *disp) {
  lv_obj_t *top = lv_disp_get_layer_top(disp);
  if (!top) return false;
  lv_area_t full;
  lv_area_set(&full, 0, 0, lv_disp_get_hor_res(disp) - 1, lv_disp_get_ver_res(disp) - 1);
  const uint32_t n = lv_obj_get_child_cnt(top);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *o = lv_obj_get_child(top, i);
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || !_lv_area_is_in(&full, &o->coords, 0)) continue;
    lv_cover_check_info_t info;
    info.res = LV_COVER_RES_COVER;
    info.area = &full;
    lv_event_send(o, LV_EVENT_COVER_CHECK, &info);
    if (info.res == LV_COVER_RES_COVER) return true;
  }
  return false;
}

}  // namespace

void skip_covered_screen(lv_disp_drv_t *) {
  lv_disp_t *disp = _lv_refr_get_disp_refreshing();
  if (!disp) disp = lv_disp_get_default();
  lv_obj_t *scr = disp ? lv_disp_get_scr_act(disp) : nullptr;
  if (!scr) return;
  // The flag is set directly: lv_obj_add/clear_flag would invalidate the screen, and this refresh
  // already covers what changed (a sheet appearing or going away invalidates the whole display).
  if (top_layer_covers(disp)) {
    scr->flags |= LV_OBJ_FLAG_HIDDEN;
  } else {
    scr->flags &= ~static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace ui
