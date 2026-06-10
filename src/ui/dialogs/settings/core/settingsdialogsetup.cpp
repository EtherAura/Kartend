// Sibling translation unit for SettingsDialog.
// Extracted from settingsdialogform.cpp during LOC-reduction refactor.
// These remain SettingsDialog members; this is a translation-unit split.
#include <algorithm>
#include <functional>
#include <limits>
#include <QAbstractItemView>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmapCache>
#include <QPushButton>
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

#include "appearancelayoutpanel.h"
#include "collection/hierarchyhelpers.h"
#include "collectiontreewidget.h"
#include "configurationpanel.h"
#include "errordialog.h"
#include "extensionutils.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "subfolderspanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants/grid.h"
#include "uiconstants/viewport.h"

void SettingsDialog::setupBasicUIConnections() {
  connect(m_saveButton, &QPushButton::clicked, this, [this]() {
    // The persistent save icon must work on every tab, including those
    // where no collection is selected (Scrapers, General, etc.). When a
    // collection is selected we use the full handleSaveCollection path
    // so per-collection appearance edits round-trip correctly; otherwise
    // we just persist the general-settings deltas.
    if (currentCollectionIndex >= 0 && currentCollectionIndex < collections.size() &&
        ui->appearanceLayoutPanel->gridWidthSpinBox()) {
      handleSaveCollection(currentCollectionIndex);
    } else {
      // No collection-side save here — only the general-settings path. Surface
      // disk-write failures so the persistent save icon doesn't lie about a
      // successful save when /config can't be written.
      if (auto result = saveGeneralSettingsFromUI(); result.isError()) {
        ErrorDialog::showError(this, result.error());
        return;
      }
      m_originalGeneralSettings = m_generalSettings;
      m_collectionSaved = true;
      updateSaveButtonStyle();
    }
  });
}

void SettingsDialog::handleSaveCollection(int editedIndex, bool refreshTree) {
  // Check if database-affecting fields changed before saving
  // These fields affect the UUID or database content and require a rescan
  QString newName = originalCollection.name;
  if (auto *item = m_treeManager ? m_treeManager->itemAt(editedIndex) : nullptr) {
    newName = item->text(0);
  }
  QString newMediaDir = ui->configurationPanel->mediaDirLineEdit()
                            ? ui->configurationPanel->mediaDirLineEdit()->text().trimmed()
                            : originalCollection.mediaDirectory;
  QString newExtensions = ui->configurationPanel->fileExtensionsLineEdit()
                              ? ui->configurationPanel->fileExtensionsLineEdit()->text().trimmed()
                              : originalCollection.extensions.join(", ");
  bool newIncludeSubfolders = ui->subfoldersPanel
                                  ? ui->subfoldersPanel->isContentSubfoldersIncluded()
                                  : originalCollection.folderBrowsing.includeContentSubfolders;

  bool databaseFieldsChanged =
      (newName != originalCollection.name) || (newMediaDir != originalCollection.mediaDirectory) ||
      (newExtensions != originalCollection.extensions.join(", ")) ||
      (newIncludeSubfolders != originalCollection.folderBrowsing.includeContentSubfolders);

  if (databaseFieldsChanged) {
    m_rescanRequired.insert(editedIndex);
  }

  saveCollectionFromUI(editedIndex);
  originalCollection = collections[editedIndex];

  // if the user selected a broader Settings Mode, propagate
  // the curated appearance/layout subset (same fields as 's
  // explicit Apply action) from the just-saved collection to the chosen
  // scope. This is silent because the mode itself is the user's opt-in.
  if (m_settingsScope != SettingsScope::Current && editedIndex >= 0 &&
      editedIndex < collections.size()) {
    QList<int> targets;
    if (m_settingsScope == SettingsScope::CurrentAndSubcollections) {
      targets = CollectionUtils::collectDescendantIndices(editedIndex, collections);
    } else { // SettingsScope::All
      targets.reserve(collections.size());
      for (int i = 0; i < collections.size(); ++i) {
        if (i != editedIndex) {
          targets.append(i);
        }
      }
    }
    propagateAppearanceToIndicesSilently(targets);
  }

  // Also save general settings (e.g., titleTintSaturation, titleTintLightness).
  // Disk failures here are reported via ErrorDialog but don't abort the save
  // flow — the per-collection save has already emitted collectionSaved and
  // adjusted in-memory state.
  if (auto result = saveGeneralSettingsFromUI(); result.isError()) {
    ErrorDialog::showError(this, result.error());
  }
  m_originalGeneralSettings = m_generalSettings;

  m_collectionSaved = true;
  updateSaveButtonStyle();
  emit collectionSaved(collections);

  if (!refreshTree) {
    return;
  }

  QSignalBlocker blocker(collectionTreeWidget);
  // Rebuild tree to reflect parent changes immediately and reselect the
  // edited collection
  updateCollectionTreeWidget();
  expandPathToCollection(editedIndex);
  if (auto *item = m_treeManager ? m_treeManager->itemAt(editedIndex) : nullptr) {
    collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
  }
  loadCollectionToUI(editedIndex);
}

void SettingsDialog::setupFormFieldConnections() {
  // Launcher data field connections (path / core / params / name / extract /
  // extension) live on LauncherTabPanel — emits changed() routed to
  // checkForChanges via the dialog constructor. Cross-cutting wiring
  // (additional-launchers list + buttons + default-launcher combo + per-
  // edit launcher-type heuristic) stays here.
  connect(ui->launcherPanel->launcherLineEdit(), &QLineEdit::textChanged, this,
          [this](const QString &text) { updateUIForLauncherType(text); });
  connect(ui->launcherPanel->launcherLineEdit(), &QLineEdit::textChanged, this,
          [this](const QString &) {
            rebuildDefaultLauncherCombo(
                ui->launcherPanel->defaultLauncherComboBox()->currentIndex());
          });
  connect(ui->launcherPanel->launcherNameLineEdit(), &QLineEdit::textChanged, this,
          [this](const QString &) {
            rebuildDefaultLauncherCombo(
                ui->launcherPanel->defaultLauncherComboBox()->currentIndex());
          });
  connect(ui->launcherPanel->addAdditionalLauncherButton(), &QPushButton::clicked, this,
          &SettingsDialog::onAddAdditionalLauncher);
  connect(ui->launcherPanel->editAdditionalLauncherButton(), &QPushButton::clicked, this,
          &SettingsDialog::onEditAdditionalLauncher);
  connect(ui->launcherPanel->removeAdditionalLauncherButton(), &QPushButton::clicked, this,
          &SettingsDialog::onRemoveAdditionalLauncher);
  connect(ui->launcherPanel->additionalLaunchersList(), &QListWidget::currentRowChanged, this,
          [this](int) { onAdditionalLauncherSelectionChanged(); });
  connect(ui->launcherPanel->additionalLaunchersList(), &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem *) { onEditAdditionalLauncher(); });
  connect(ui->launcherPanel->defaultLauncherComboBox(),
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { checkForChanges(); });
  // Configuration-tab data field connections (mediaDir, type, extensions,
  // expandMode, showAllSubcollectionItems) live on ConfigurationPanel.
  // Asset-directory + placeholder-artwork edits live on ArtworkTabPanel.
  // Subfolders edits live on SubfoldersPanel.
  // The mediaDir edit additionally feeds onContentDirectoryChanged for the
  // launcher-type heuristic — wire that here against the panel's accessor.
  connect(ui->configurationPanel->mediaDirLineEdit(), &QLineEdit::textChanged, this,
          &SettingsDialog::onContentDirectoryChanged);
  // gridWidthSpinBox dirty-checks through the dialog (its live-preview
  // handler was removed with the dead gridWidthChanged signal,
  // Kartend-vy1xs) — wire that against the panel's accessor. Other layout
  // fields' change connections live on AppearanceLayoutPanel.
  connect(ui->appearanceLayoutPanel->gridWidthSpinBox(),
          QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::checkForChanges);
  // showAllSubcollectionItemsCheckBox connection lives on ConfigurationPanel.
  if (ui->configurationPanel->parentCollectionComboBox()) {
    connect(ui->configurationPanel->parentCollectionComboBox(),
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  // Sidebar / Details Pane fields are wired internally by SidebarPanel; the
  // dialog observes the panel's changed() signal in the constructor.
  // hideTitles + hideSubcollectionTitles connections live on
  // AppearanceTitlesPanel.
  // hideMissingArtwork / itemWidth / itemHeight / fontSize / cornerRadius
  // connections all live on AppearanceLayoutPanel + AppearanceTitlesPanel.
  // Color field connections (palette / list-row colors / background /
  // vignette) live on AppearanceColorsPanel.
  // List font size + row height connections live on AppearanceListPanel.
  // headerLogoEdit + headerLogoPositionComboBox connections live on
  // AppearanceToolbarPanel.
  // Parallax + backdrop blur connections live on AppearanceEffectsPanel.
}

// Kartend-vy1xs: setupSpacingConnections()/handleSpacingChanged() removed —
// they only fed the dead spacingChanged live-preview signal. Spacing dirty-
// checking lives on AppearanceLayoutPanel's changed() emission.

void SettingsDialog::setupTreeWidgetConnections() {
  if (!collectionTreeWidget || !m_treeManager) {
    return;
  }
  // Kartend-ook62/mnymg: the TreeManager controller owns the widget's wiring
  // AND the gesture handlers (selection switch, rename, context-menu, drag-drop
  // reparent). It borrows this dialog's selection-state members by pointer and
  // routes dialog-side effects through the SettingsTreeHost interface (this).
  m_treeManager->setSelectionState(&currentCollectionIndex, &currentTreeItem, &originalCollection,
                                   &m_collectionSaved);
  m_treeManager->setHost(this);
  m_treeManager->attachWidget();
  // The controller persists a drag-drop reparent immediately; relay it to the
  // dialog's collectionSaved signal (survives a Cancel of the outer dialog).
  connect(m_treeManager.get(), &TreeManager::collectionsReordered, this,
          [this]() { emit collectionSaved(collections); });
}

void SettingsDialog::setupUIConstraints() {
  if (ui->appearanceLayoutPanel->horizontalSpacingSpinBox()) {
    ui->appearanceLayoutPanel->horizontalSpacingSpinBox()->setMinimum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MIN));
    ui->appearanceLayoutPanel->horizontalSpacingSpinBox()->setMaximum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MAX));
    ui->appearanceLayoutPanel->horizontalSpacingSpinBox()->setSingleStep(1);
  }
  if (ui->appearanceLayoutPanel->verticalSpacingSpinBox()) {
    ui->appearanceLayoutPanel->verticalSpacingSpinBox()->setMinimum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MIN));
    ui->appearanceLayoutPanel->verticalSpacingSpinBox()->setMaximum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MAX));
    ui->appearanceLayoutPanel->verticalSpacingSpinBox()->setSingleStep(1);
  }
  if (ui->appearanceLayoutPanel->gridWidthSpinBox()) {
    ui->appearanceLayoutPanel->gridWidthSpinBox()->setMinimum(UIConstants::Grid::MIN_WIDTH);
    // No upper cap — 4K/8K displays exceed the legacy 40-column ceiling.
    ui->appearanceLayoutPanel->gridWidthSpinBox()->setMaximum(std::numeric_limits<int>::max());
    ui->appearanceLayoutPanel->gridWidthSpinBox()->setSingleStep(1);
  }
  // fontSizeSpinBox bounds set inside AppearanceTitlesPanel.ui.
  // cornerRadiusSpinBox bounds set inside AppearanceLayoutPanel.ui.
}
