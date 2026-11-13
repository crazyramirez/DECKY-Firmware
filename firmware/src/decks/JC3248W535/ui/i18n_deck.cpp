// Guition JC3248W535 (3.5" portrait): texts that differ from the shared table (src/core/ui/i18n.cpp), e.g. shorter labels
// for this panel or hints that name this board's buttons.
#include "ui/i18n.h"

namespace ui {

const char *deck_tr(Str id, bool en) {
  switch (id) {
    case Str::PairManual:
      return en ? "Enter IP manually" : "Introducir IP manualmente";
    case Str::TabsHint:
      return en ? "The eye shows or hides a tab. The first visible one opens at start-up." : "El ojo muestra u oculta la pestaña. La primera visible se abre al encender.";
    case Str::ConfirmDeepSleep:
      return en ? "Deep sleep? The deck disconnects from the PC and uses minimal power. Touch the screen (or press BOOT) to wake it; it takes a few seconds." : "¿Reposo profundo? El deck se desconecta del PC y gasta lo mínimo. Toca la pantalla (o pulsa BOOT) para despertarlo; tarda unos segundos.";
    case Str::WxPartly:  // fits beside the temperature (Today, Now card)
      return en ? "Partly cloudy" : "Nubes y claros";
    case Str::CryBtcDom:  // narrow block on the Crypto tab
      return en ? "BTC dom." : "Dom. BTC";
    default:
      return nullptr;
  }
}

}  // namespace ui
