// Sibling translation unit for SettingsDialog.
// Extracted from settingsdialogform.cpp during LOC-reduction refactor.
// These remain SettingsDialog members; this is a translation-unit split.
#include <algorithm>
#include <functional>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPixmapCache>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "appearanceeffectspanel.h"
#include "appearancelayoutpanel.h"
#include "appearancelistpanel.h"
#include "appearancetitlespanel.h"
#include "appearancetoolbarpanel.h"
#include "artworktabpanel.h"
#include "configurationpanel.h"
#include "extensionutils.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "sidebarpanel.h"
#include "subfolderspanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"

auto SettingsDialog::extractUIFieldValues() -> CollectionConfig {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_workingCollections.size()) {
    return {};
  }

  // Allow panels to write directly into the working row via the model.
  // saveCollectionFromUI snapshots first and rolls back if validation
  // fails downstream.
  ui->launcherPanel->save();
  ui->configurationPanel->save();
  ui->artworkPanel->save();
  ui->subfoldersPanel->save();
  ui->appearanceLayoutPanel->save();
  ui->appearanceTitlesPanel->save();
  ui->sidebarPanel->save();

  // Background / palette / list-row colors / vignette owned by
  // AppearanceColorsPanel.
  ui->appearanceColorsPanel->save();

  // List mode settings (font size + row height owned by AppearanceListPanel).
  ui->appearanceListPanel->save();

  // header logo
  ui->appearanceToolbarPanel->save();

  // Effects (parallax + backdrop blur) handled by AppearanceEffectsPanel.
  ui->appearanceEffectsPanel->save();

  // Special handling for tree name, additionalLaunchers, additionalParentNames,
  // and defaultLauncherIndex — these stay on the dialog and write directly
  // into the working row.
  auto &row = m_workingCollections[currentCollectionIndex];
  if (auto *item = m_treeManager ? m_treeManager->itemAt(currentCollectionIndex) : nullptr) {
    QString treeName = item->text(0);
    if (!treeName.isEmpty()) {
      row.name = treeName;
    }
  }
  row.launcher.additionalLaunchers = m_workingAdditionalLaunchers;
  row.additionalParentNames = m_workingAdditionalParentNames;
  if (ui->launcherPanel->defaultLauncherComboBox()->count() > 0) {
    row.launcher.defaultLauncherIndex =
        ui->launcherPanel->defaultLauncherComboBox()->currentIndex();
  }
  return row;
}

// Updates parent collection settings from UI
auto SettingsDialog::updateParentCollectionFromUI(CollectionConfig &collection, int index) -> void {
  if (ui->configurationPanel->parentCollectionComboBox()) {
    int dropdownIndex = ui->configurationPanel->parentCollectionComboBox()->currentIndex();
    if (dropdownIndex >= 0 && dropdownIndex < m_parentCollectionMapping.size()) {
      int newParentIndex = m_parentCollectionMapping[dropdownIndex];
      if (newParentIndex >= 0 && newParentIndex < m_workingCollections.size() &&
          newParentIndex != index) {
        collection.parentCollectionIndex = newParentIndex;
        collection.isSubcollection = true;
      } else {
        collection.parentCollectionIndex = -1;
        collection.isSubcollection = false;
      }
    } else {
      collection.parentCollectionIndex = -1;
      collection.isSubcollection = false;
    }
  }
}

// Kartend audit 2026-07: the per-collection check*Changes helper family that
// lived here (checkBasicFieldChanges + the TreeName/ParentCollection/Color/
// ListMode buckets and the permanently-false Extension/Dimension/Background
// stubs) was deleted along with every panel's hasChanges() — the dirty check
// in hasUnsavedChanges() (settingsdialogform.cpp) now runs the same
// extraction pipeline Save uses and whole-struct-compares the result against
// originalCollection, so a field can no longer be persisted-but-untracked.

auto SettingsDialog::checkGeneralSettingsChanges() const -> bool {
  // Whole-struct compare against the baseline snapshot. Every settings panel
  // writes straight into m_generalSettings (the dialog's live model) and save
  // persists it wholesale (mwSettings = m_generalSettings, Kartend-d27fg), so
  // the dirty-check must mirror that whole struct — anything that differs WILL
  // be persisted on save and therefore IS a change. GeneralSettings::operator==
  // covers every settings sub-struct (excluding the runtime-only
  // lastSelectedItems) and stays field-complete automatically as new fields are
  // added, replacing the ~210-line hand-enumerated compare that went stale
  // whenever a deferred field was missed (Kartend-6oqat; e.g. the
  // retroarchConfigPath miss, Kartend-hvdow). normalizedForSave() trims the
  // free-text path/title fields on both sides so a whitespace-only edit doesn't
  // register as dirty (matching how those fields are persisted).
  return m_generalSettings.normalizedForSave() != m_originalGeneralSettings.normalizedForSave();
}
