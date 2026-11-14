// Cities sheet (core/ui/cities_sheet.cpp) on the 480x272 panel: the city search's header and
// margins, compact two-line rows (four fit on the screen).

#include "ui/cities_ui.h"
#include "ui/theme/theme.h"

namespace ui {
namespace cities {

const Layout &layout() {
  using namespace theme;
  static const Layout l = {
      kMargin,  // margin
      6,        // gap
      32,       // head_h
      32,       // close_d
      44,       // title_x
      50,       // row_h
      12,       // row_pad
      12,       // row_radius
      30,       // icon_w
      36,       // btn_d
      false,    // stack
      font_title(),  // f_title
      font_body(),   // f_name
      font_tiny(),   // f_sub
      font_title(),  // f_time
      font_body(),   // f_temp
      font_title(),  // f_icon
      font_small(),  // f_btn
  };
  return l;
}

}  // namespace cities
}  // namespace ui
