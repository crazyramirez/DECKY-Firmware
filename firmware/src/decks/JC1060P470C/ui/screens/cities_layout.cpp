// Cities sheet (core/ui/cities_sheet.cpp) on the 1024x600 panel: the city search's header and
// margins, roomy two-line rows.

#include "ui/cities_ui.h"
#include "ui/theme/theme.h"

namespace ui {
namespace cities {

const Layout &layout() {
  using namespace theme;
  static const Layout l = {
      kMargin,  // margin
      9,        // gap
      48,       // head_h
      48,       // close_d
      66,       // title_x
      90,       // row_h
      24,       // row_pad
      20,       // row_radius
      60,       // icon_w
      54,       // btn_d
      false,    // stack
      font_title(),      // f_title
      font_body(),       // f_name
      font_tiny(),       // f_sub
      font_title(),      // f_time
      font_body(),       // f_temp
      font_weather_m(),  // f_icon
      font_small(),      // f_btn
  };
  return l;
}

}  // namespace cities
}  // namespace ui
