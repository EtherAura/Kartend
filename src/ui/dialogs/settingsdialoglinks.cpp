// Alias-parent ("Linked Parents") controls for SettingsDialog (Kartend-gzmk).
// Sibling translation unit hosting the load/save/picker logic for
// CollectionConfig::additionalParentNames so the link feature lives in one
// place — modeled on settingsdialoglaunchers.cpp.
#include "collectionpickerdialog.h"
#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QDialog>
#include <QPushButton>

#include "collectionutils.h"

void SettingsDialog::loadLinkedParentsToUI(const CollectionConfig &config) {
  m_workingAdditionalParentNames = config.additionalParentNames;
  updateLinkedParentsButtonLabel();
}

void SettingsDialog::clearLinkedParentsUI() {
  m_workingAdditionalParentNames.clear();
  updateLinkedParentsButtonLabel();
}

void SettingsDialog::updateLinkedParentsButtonLabel() {
  if (!ui->editLinkedParentsButton) {
    return;
  }
  const int count = m_workingAdditionalParentNames.size();
  if (count == 0) {
    ui->editLinkedParentsButton->setText(tr("Linked Parents..."));
  } else {
    ui->editLinkedParentsButton->setText(tr("Linked Parents (%1)...").arg(count));
  }
}

auto SettingsDialog::checkLinkedParentsChanges() const -> bool {
  // Order matters — the cache renders linked children in the user's
  // declared order, so swapping two link names is a real change.
  return m_workingAdditionalParentNames != originalCollection.additionalParentNames;
}

void SettingsDialog::onEditLinkedParents() {
  if (!CollectionUtils::isValidIndex(currentCollectionIndex, &collections)) {
    return;
  }
  // Build excluded set: self plus every descendant. The cache resolves
  // unknown names to no-ops at runtime, but disallowing cycles at the UI
  // level keeps the user from constructing a config where a collection
  // ends up below itself in the merged tree.
  QList<int> excluded;
  excluded.append(currentCollectionIndex);
  excluded.append(CollectionUtils::collectDescendantIndices(currentCollectionIndex, collections));

  // Resolve the current additionalParentNames back to indices for the
  // initial-checked set. Names that no longer match any collection are
  // dropped silently — same forgiveness the cache applies.
  QList<int> initialChecked;
  initialChecked.reserve(m_workingAdditionalParentNames.size());
  for (const QString &name : m_workingAdditionalParentNames) {
    for (int i = 0; i < collections.size(); ++i) {
      if (collections[i].name == name) {
        initialChecked.append(i);
        break;
      }
    }
  }

  CollectionPickerDialog picker(collections, excluded, initialChecked, this);
  picker.setWindowTitle(tr("Linked Parents"));
  if (picker.exec() != QDialog::Accepted) {
    return;
  }

  QStringList newNames;
  const QList<int> selected = picker.selectedIndices();
  newNames.reserve(selected.size());
  for (int idx : selected) {
    if (CollectionUtils::isValidIndex(idx, &collections)) {
      newNames.append(collections[idx].name);
    }
  }

  if (newNames == m_workingAdditionalParentNames) {
    return;
  }
  m_workingAdditionalParentNames = newNames;
  updateLinkedParentsButtonLabel();
  m_collectionSaved = false;
  updateSaveButtonStyle();
}
