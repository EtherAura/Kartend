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
  // Keep the startup-collection target in lockstep with the propagation the
  // alias-parent names already get: the setting stores the collection NAME.
  // The removal pipeline passes an empty newName, which resets the target to
  // the "(Default)" sentinel instead of leaving a dangling name behind.
  // (Plain renames are remapped at commit time in handleSaveCollection.)
  if (!oldName.isEmpty() && m_generalSettings.startup.startupCollection == oldName &&
      oldName != newName) {
    m_generalSettings.startup.startupCollection = newName;
  }
}
