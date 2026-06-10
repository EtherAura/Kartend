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

// Checks basic field changes against original configuration
auto SettingsDialog::checkBasicFieldChanges() const -> bool {
  const CollectionConfig &o = originalCollection;

  // Special cases: launcher dirty-check, additional-launchers list, default
  // launcher index (only valid when combobox has entries), the
  // collection-type combobox (compares trimmed currentText, not index), and
  // the spacing fields (apply spacingUiToInternal first). Sidebar / details-
  // pane fields delegate to SidebarPanel::hasChanges, which handles its own
  // image-vs-color background-value special case.
  // launcher fields (path / core / params / name / extract / extracted-ext)
  // delegate to LauncherTabPanel::hasChanges; the additional-launchers list
  // and default-launcher combo state still live on the dialog.
  const bool additionalChanged = m_workingAdditionalLaunchers != o.launcher.additionalLaunchers;
  const bool defaultLauncherChanged =
      ui->launcherPanel->defaultLauncherComboBox()->count() > 0 &&
      ui->launcherPanel->defaultLauncherComboBox()->currentIndex() !=
          o.launcher.defaultLauncherIndex;
  // type comparison is handled inside ConfigurationPanel::hasChanges below.
  const bool hSpacingChanged =
      ui->appearanceLayoutPanel->horizontalSpacingSpinBox() &&
      spacingUiToInternal(ui->appearanceLayoutPanel->horizontalSpacingSpinBox()->value()) !=
          o.gridLayout.horizontalSpacing;
  const bool vSpacingChanged =
      ui->appearanceLayoutPanel->verticalSpacingSpinBox() &&
      spacingUiToInternal(ui->appearanceLayoutPanel->verticalSpacingSpinBox()->value()) !=
          o.gridLayout.verticalSpacing;

  return additionalChanged || defaultLauncherChanged || hSpacingChanged || vSpacingChanged ||
         ui->sidebarPanel->hasChanges() || ui->configurationPanel->hasChanges() ||
         ui->artworkPanel->hasChanges() || ui->launcherPanel->hasChanges() ||

         // Layout / grid / view fields delegate to AppearanceLayoutPanel.
         ui->appearanceLayoutPanel->hasChanges() ||

         // Titles / folders.
         ui->appearanceTitlesPanel->hasChanges() || ui->subfoldersPanel->hasChanges();
}

// Extension + customArtworkTypes dirty checks live in ConfigurationPanel /
// ArtworkTabPanel respectively (covered by checkBasicFieldChanges via the
// panels' own hasChanges() methods). Kept as an empty stub so the
// hasUnsavedChanges() callsite stays unchanged for now.
auto SettingsDialog::checkExtensionChanges() const -> bool {
  return false;
}

// Checks tree name changes
auto SettingsDialog::checkTreeNameChanges() const -> bool {
  QString currentTreeName = originalCollection.name;
  if (auto *item = m_treeManager ? m_treeManager->itemAt(currentCollectionIndex) : nullptr) {
    currentTreeName = item->text(0);
  }
  return currentTreeName != originalCollection.name;
}

// Checks parent collection changes
auto SettingsDialog::checkParentCollectionChanges() const -> bool {
  int dropdownIndex = (ui->configurationPanel->parentCollectionComboBox())
                          ? ui->configurationPanel->parentCollectionComboBox()->currentIndex()
                          : -1;
  int currentParentIndex = -1;
  if (dropdownIndex >= 0 && dropdownIndex < m_parentCollectionMapping.size()) {
    currentParentIndex = m_parentCollectionMapping[dropdownIndex];
  }
  return currentParentIndex != originalCollection.parentCollectionIndex;
}

// Checks dimension changes
auto SettingsDialog::checkDimensionChanges() const -> bool {
  // Item dimensions live on AppearanceLayoutPanel; rolled into
  // checkBasicFieldChanges via the panel's own hasChanges().
  return false;
}

// Checks color field changes — palette / list-row colors / vignette / header-
// logo / parallax+blur. Background type/value are part of AppearanceColorsPanel
// too (collapsed under hasChanges).
auto SettingsDialog::checkColorChanges() const -> bool {
  return ui->appearanceColorsPanel->hasChanges() || ui->appearanceToolbarPanel->hasChanges() ||
         ui->appearanceEffectsPanel->hasChanges();
}

// Checks list mode field changes
auto SettingsDialog::checkListModeChanges() const -> bool {
  return ui->appearanceListPanel->hasChanges();
}

// Background type/value dirty check rolled into checkColorChanges via
// AppearanceColorsPanel::hasChanges. Kept as a stub so the hasUnsavedChanges()
// callsite stays unchanged.
auto SettingsDialog::checkBackgroundChanges() const -> bool {
  return false;
}

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
