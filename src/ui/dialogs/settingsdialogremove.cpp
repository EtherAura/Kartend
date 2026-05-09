// Sibling translation unit for SettingsDialog: collection removal.
// The seven-step pipeline + the orchestrator that strings them together
// now live on CollectionRemover; this TU forwards the public slot the
// dialog's button connect uses.
#include "collectionremover.h"
#include "settingsdialog.h"

void SettingsDialog::removeCollection() {
  if (m_collectionRemover) {
    m_collectionRemover->run();
  }
}
