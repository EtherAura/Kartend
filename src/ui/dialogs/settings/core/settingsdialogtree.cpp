// Kartend-uodi: residual thin delegators + the parent-combo populator
// after splitting the original tree TU. The heavier flows live in
// sibling TUs:
//   * settingsdialogtreesync.cpp       — name change + selection sync
//   * settingsdialogtreemutation.cpp   — add / duplicate / copy / propagate
//   * settingsdialogtreedragdrop.cpp   — context menu + drag-drop reparent
//
// What stays here is the per-call code that's too small to warrant its
// own TU (mostly one-line forwards to TreeManager plus the combo-box
// populator that doesn't fit any of the heavier categories).
#include <QComboBox>
#include <QSignalBlocker>

#include "collectionutils.h"
#include "settingsdialog.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"

void SettingsDialog::updateCollectionTreeWidget() {
  if (!m_treeManager) {
    return;
  }
  m_treeManager->rebuild();
  // Enable delete button when a collection is selected.
  updateDeleteButtonState();
}

void SettingsDialog::expandPathToCollection(int index) {
  if (m_treeManager) {
    m_treeManager->expandPathTo(index);
  }
}

void SettingsDialog::updateParentCollectionComboBox(int currentIndex) {
  if (!ui->configurationPanel->parentCollectionComboBox()) {
    return;
  }

  QSignalBlocker blocker(ui->configurationPanel->parentCollectionComboBox());
  ui->configurationPanel->parentCollectionComboBox()->clear();
  ui->configurationPanel->parentCollectionComboBox()->addItem("None");
  m_parentCollectionMapping.clear();
  m_parentCollectionMapping.append(-1);

  for (int i = 0; i < collections.size(); ++i) {
    if (i == currentIndex) {
      continue;
    }
    if (wouldCreateCircularReference(currentIndex, i)) {
      continue;
    }
    ui->configurationPanel->parentCollectionComboBox()->addItem(collections[i].name);
    m_parentCollectionMapping.append(i);
  }

  int desiredParentIndex = (currentIndex >= 0 && currentIndex < collections.size())
                               ? collections[currentIndex].parentCollectionIndex
                               : -1;
  int targetDropdownIndex = m_parentCollectionMapping.indexOf(desiredParentIndex);
  if (targetDropdownIndex < 0) {
    targetDropdownIndex = 0;
  }
  ui->configurationPanel->parentCollectionComboBox()->setCurrentIndex(targetDropdownIndex);
}

auto SettingsDialog::wouldCreateCircularReference(int childIndex, int potentialParentIndex) const
    -> bool {
  return CollectionUtils::wouldCreateCircularReference(childIndex, potentialParentIndex,
                                                       collections);
}
