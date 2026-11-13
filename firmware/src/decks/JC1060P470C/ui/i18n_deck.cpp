// Guition JC1060P470C (7"): texts that differ from the shared table (src/core/ui/i18n.cpp), e.g. shorter labels
// for this panel or hints that name this board's buttons.
#include "ui/i18n.h"

namespace ui {

const char *deck_tr(Str id, bool en) {
  switch (id) {
    // none: this deck uses the shared texts
    default:
      return nullptr;
  }
}

}  // namespace ui
