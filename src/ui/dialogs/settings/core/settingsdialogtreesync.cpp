// Kartend-mnymg: onTreeItemSelectionChanged + onTreeItemChanged migrated into
// the TreeManager controller (it owns the tree gesture handlers now). What
// remains here is the propagateCollectionNameChange forwarder, which stays on
// SettingsDialog because it's the CollectionRemoverHost override the removal
// pipeline calls; it delegates to the controller's propagateNameChange.
#include "settingsdialog.h"
#include "treemanager.h"

void SettingsDialog::propagateCollectionNameChange(const QString &oldName, const QString &newName) {
  if (m_treeManager) {
    m_treeManager->propagateNameChange(oldName, newName);
  }
}
