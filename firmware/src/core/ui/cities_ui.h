#pragma once
// Today's cities, the parts every deck shares (the model is app/cities.h):
//   - the city switcher of the Today page: a pill with the shown city, how far ahead its clock is and
//     one dot per city (a "+" while there is only one); a horizontal swipe on the page changes city;
//   - the Cities sheet (top layer): every city with its local time and weather, like a world clock.
//     Tap a city to see it on Today; "Edit" shows the actions (remove, make it the main one, change
//     the main one); the last row adds a city through the city search.
// Each deck gives the sheet its metrics and fonts (ui/screens/cities_layout.cpp) and places the pill
// on its own Today page.

#include <lvgl.h>
#include <stddef.h>
#include <time.h>

namespace ui {
namespace cities {

// ---- text ----------------------------------------------------------------------------------------
void fmt_offset(char *buf, size_t n, int minutes);        // "+8 h", "-5:30 h", "Same time"
void fmt_time(char *buf, size_t n, const struct tm &t);   // "02:05", or "2:05 AM" with the 12 h clock
// `s` cut to fit max_w pixels, letter by letter, ending in "..." (U+2026) when it had to be cut. LVGL's
// own LONG_DOT cuts at the last whole word, which wastes half the room on names of two words.
void fit_text(char *out, size_t n, const char *s, const lv_font_t *font, int max_w);

// ---- Today: city switcher --------------------------------------------------------------------------
struct Pill {
  lv_obj_t *root = nullptr;  // the tap target (opens the Cities sheet unless the deck says otherwise)
  lv_obj_t *pin = nullptr, *name = nullptr, *off = nullptr, *spacer = nullptr, *dots = nullptr, *plus = nullptr;
  const lv_font_t *font = nullptr;
  int row_w = 0;   // > 0: a row this wide (city on the left, dots on the right); 0: a chip as wide as its text
  int max_w = 0;   // chip: the widest it may get
  int dot = 6;     // dot diameter
  int pad = 0;     // chip: horizontal padding
  bool offset = true;  // show how far ahead the city's clock is (off where the deck says it elsewhere)
  int dots_n = -1, dots_cur = -1;
};
// chip = true: a translucent capsule (for decks that show it straight on the weather picture).
void pill_create(Pill &p, lv_obj_t *parent, const lv_font_t *font, bool chip, int width, int dot);
void pill_update(Pill &p);  // from app::today_city(); hidden without a main place
// Horizontal swipes on `page` (and on everything inside it) go to the next / previous city.
void attach_swipe(lv_obj_t *page);

// ---- Cities sheet ----------------------------------------------------------------------------------
struct Layout {
  int margin;      // sheet padding
  int gap;         // between the header and the rows, and between rows
  int head_h;      // header height
  int close_d;     // round close button
  int title_x;     // left edge of the title
  int row_h, row_pad, row_radius;
  int icon_w;      // weather icon column
  int btn_d;       // edit mode: round action buttons
  bool stack;      // narrow panel: the temperature under the time instead of beside it
  const lv_font_t *f_title, *f_name, *f_sub, *f_time, *f_temp, *f_icon, *f_btn;
};
const Layout &layout();  // each deck: ui/screens/cities_layout.cpp

void open();        // without a main place yet: the city search instead
void close();
bool active();
void on_weather();  // forecasts, the list or the shown city changed
void tick();        // every second: the rows' clocks
void dev_edit(bool on);  // developer aid (simulator): the sheet's edit mode

}  // namespace cities
}  // namespace ui
