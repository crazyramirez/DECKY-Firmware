#pragma once
#include <lvgl.h>

namespace ui {

// LVGL 8 draws the active screen under the top layer even where an opaque top-layer object covers
// the whole display: under a full-screen sheet (Wi-Fi with its keyboard, place search, pairing, the
// setup assistant, the screensaver) every refresh also drew the page nobody could see. Called when
// LVGL starts drawing a refresh (the display driver's render_start_cb): the active screen is skipped
// while such a sheet is shown and drawn again in the refresh that removes it (that refresh redraws
// the sheet's whole area, i.e. the whole display).
void skip_covered_screen(lv_disp_drv_t *drv);

}  // namespace ui
