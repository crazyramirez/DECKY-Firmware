// Guition ESP32-4848S040 (4" square): texts that differ from the shared table (src/core/ui/i18n.cpp).
#include "ui/i18n.h"

namespace ui {

const char *deck_tr(Str id, bool en) {
  switch (id) {
    case Str::SsHint:  // no BOOT button on this board (GPIO0 is a pixel line)
      return en ? "While it is on it replaces auto sleep: the screen never turns off. Touch the screen to leave it." : "Mientras está activado sustituye al reposo automático: la pantalla no se apaga. Toca la pantalla para salir.";
    case Str::TabsHint:
      return en ? "The eye shows or hides a tab. The first visible one opens at start-up." : "El ojo muestra u oculta la pestaña. La primera visible se abre al encender.";
    default:
      return nullptr;
  }
}

}  // namespace ui
