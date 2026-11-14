// Cities sheet (core/ui/cities_sheet.cpp) on the 480x480 square panel: the city search's header and
// margins, the JC3248W535's type scale with the time and the temperature side by side.

#include "ui/cities_ui.h"
#include "ui/theme/theme.h"

namespace ui {
namespace cities {

const Layout &layout() {
  using namespace theme;
  static const Layout l = {
      kMargin,    // margin
      8,          // gap
      kTouchMin,  // head_h
      40,         // close_d
      54,         // title_x
      64,         // row_h
      14,         // row_pad
      14,         // row_radius
      34,         // icon_w
      40,         // btn_d
      false,      // stack
      font_title(),  // f_title
      font_body(),   // f_name
      font_small(),  // f_sub (the 12 px font has no symbols)
      font_title(),  // f_time
      font_body(),   // f_temp
      font_title(),  // f_icon
      font_body(),   // f_btn
  };
  return l;
}

}  // namespace cities
}  // namespace ui
