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

#include "settingsdialog.h"
#include "settingsdialogtreehelpers.h"
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

  // Row-model computation (self/cycle exclusion + current-parent row) lives in
  // the tested helper; the cycle check itself stays on the TreeManager
  // controller, threaded through as a callable (unset when no manager, which
  // disables cycle filtering — matching the pre-controller behaviour).
  SettingsTreeHelpers::CycleCheck cycleCheck;
  if (m_treeManager) {
    cycleCheck = [this](int childIndex, int potentialParentIndex) {
      return m_treeManager->wouldCreateCircularReference(childIndex, potentialParentIndex);
    };
  }
  const SettingsTreeHelpers::ParentComboModel model = SettingsTreeHelpers::buildParentComboModel(
      collections, currentIndex, QStringLiteral("None"), cycleCheck);

  QSignalBlocker blocker(ui->configurationPanel->parentCollectionComboBox());
  ui->configurationPanel->parentCollectionComboBox()->clear();
  ui->configurationPanel->parentCollectionComboBox()->addItems(model.labels);
  m_parentCollectionMapping = model.mapping;
  ui->configurationPanel->parentCollectionComboBox()->setCurrentIndex(model.selectedRow);
}

// Kartend-ook62: wouldCreateCircularReference moved to the TreeManager
// controller (it owns the drag-drop cycle guard + parent-combo cycle filter).
