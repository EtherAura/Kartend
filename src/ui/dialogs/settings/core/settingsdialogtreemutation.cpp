// Kartend-uodi step: add/duplicate/copy mutation paths extracted from
// settingsdialogtree.cpp. Each entry point either appends a new
// CollectionConfig (addCollection, ensureRootCollectionExists,
// duplicateCollection) or overwrites a curated subset of fields onto an
// existing one (applyCategoriesToIndices,
// propagateAppearanceToIndicesSilently, copySettingsFromOtherCollection).
//
// All entry points emit collectionSaved() on success and refresh the tree
// + form state so the user sees the new collection / values immediately.
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>

#include "applysettingsdialog.h"
#include "collection/collectionconfig.h"
#include "collection/validationhelpers.h"
#include "collectiontreewidget.h"
#include "createcollectiondialog.h"
#include "errordialog.h"
#include "retroarchicons.h"
#include "retroarchutils.h"
#include "settingsdialog.h"
#include "settingsdialogtreehelpers.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants/grid.h"
#include "uiconstants/item.h"

void SettingsDialog::addCollection() {
  CreateCollectionDialog dialog(this);
  dialog.setRetroarchConfigOverride(m_generalSettings.launchers.retroarchConfigPath);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  // OK is gated on a non-empty name, so an accepted dialog always
  // carries a usable collection name.
  const QString name = dialog.collectionName();

  if (currentCollectionIndex >= 0 && currentCollectionIndex < collections.size()) {
    saveCollectionFromUI(currentCollectionIndex);
  }

  // Flush global general settings too (Kartend-c427s). The title-tint fields
  // (saturation / lightness / base color) are consumed off the appearance-colors
  // widget into m_generalSettings as the user edits, but saveCollectionFromUI
  // only writes the per-collection row — so without this the global edit would be
  // silently dropped on add/duplicate/copy. Mirrors handleSaveCollection's flush;
  // disk failures are surfaced but don't abort the add.
  if (auto result = saveGeneralSettingsFromUI(); result.isError()) {
    ErrorDialog::showError(this, result.error());
  }
  m_originalGeneralSettings = m_generalSettings;

  CollectionConfig newCollection;
  newCollection.name = name;
  newCollection.type = dialog.collectionType();
  newCollection.scraperOverrides.scraperProviderId = dialog.scraperProviderId();
  newCollection.scraperOverrides.screenscraperSystemId = dialog.screenscraperSystemId();
  newCollection.systemIcon = dialog.systemIcon();
  newCollection.launcher.launcherPath = dialog.launcherPath();
  newCollection.launcher.corePath = dialog.corePath();
  newCollection.launcher.launchParameters = "";
  newCollection.mediaDirectory = dialog.contentPath();
  newCollection.artworkDirectory = dialog.artworkDirectory();
  newCollection.videoDirectory = "";
  newCollection.manualDirectory = "";
  newCollection.extensions = QStringList();
  newCollection.gridLayout.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
  newCollection.sidebar.sidebarVisible = false;
  newCollection.parentCollectionIndex = -1;
  newCollection.isSubcollection = false;
  newCollection.showAllSubcollectionItems = false;
  newCollection.horizontalAlignment = HorizontalAlignment::Center;
  newCollection.gridLayout.fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  newCollection.hideTitles = false;
  newCollection.hideSubcollectionTitles = false;

  int parentIdx =
      (currentCollectionIndex >= 0 && currentCollectionIndex < m_workingCollections.size())
          ? currentCollectionIndex
          : -1;
  if (parentIdx >= 0) {
    SettingsTreeHelpers::applySubcollectionDefaults(newCollection, m_workingCollections[parentIdx],
                                                    parentIdx);
  }

  collections.append(newCollection);
  m_workingCollections.append(newCollection);
  int newIndex = collections.size() - 1;
  currentCollectionIndex = newIndex;

  updateCollectionTreeWidget();
  expandPathToCollection(newIndex);

  if (auto *item = m_treeManager ? m_treeManager->itemAt(newIndex) : nullptr) {
    collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
  }

  loadCollectionToUI(newIndex);
  originalCollection = m_workingCollections[newIndex];
  m_collectionSaved = true;
  updateSaveButtonStyle();

  // Persist the new collection immediately to disk
  emit collectionSaved(collections);
}

// Ensures at least one root collection exists, prompting user to create one if
// needed
void SettingsDialog::ensureRootCollectionExists() {
  // Check if any root collection exists
  bool hasRootCollection = false;
  for (const auto &collection : collections) {
    if (collection.parentCollectionIndex == -1) {
      hasRootCollection = true;
      break;
    }
  }

  if (hasRootCollection) {
    return;
  }

  // No root collection exists - prompt user to create one
  while (true) {
    CreateCollectionDialog dialog(this);
    dialog.setRetroarchConfigOverride(m_generalSettings.launchers.retroarchConfigPath);
    dialog.setWindowTitle(tr("Create Collection"));
    dialog.setIntroText(tr("No collections found. Set up your first collection to continue."));

    if (dialog.exec() != QDialog::Accepted) {
      // The user can't cancel their way out — a collection is required
      // to configure settings. Explain why, then re-prompt.
      QMessageBox::warning(this, tr("Collection Required"),
                           tr("A collection is required to configure settings. "
                              "Please create one to continue."));
      continue;
    }

    // OK is gated on a non-empty name, so no empty-name re-check is needed.
    CollectionConfig newCollection;
    newCollection.name = dialog.collectionName();
    newCollection.type = dialog.collectionType();
    newCollection.scraperOverrides.scraperProviderId = dialog.scraperProviderId();
    newCollection.scraperOverrides.screenscraperSystemId = dialog.screenscraperSystemId();
    newCollection.systemIcon = dialog.systemIcon();
    newCollection.mediaDirectory = dialog.contentPath();
    newCollection.artworkDirectory = dialog.artworkDirectory();
    newCollection.launcher.launcherPath = dialog.launcherPath();
    newCollection.launcher.corePath = dialog.corePath();
    newCollection.gridLayout.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
    newCollection.parentCollectionIndex = -1;
    newCollection.isSubcollection = false;

    collections.append(newCollection);
    m_workingCollections.append(newCollection);
    currentCollectionIndex = collections.size() - 1;

    m_collectionSaved = false;
    break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// propagate appearance/layout settings to other collections
// ─────────────────────────────────────────────────────────────────────────────
//
// These helpers copy a curated subset of the currently-edited collection's
// configuration onto a target set of collections. The copied fields are
// strictly the "how it looks and lays out" knobs — grid/item dimensions,
// spacing, fonts, colors, background, list-mode styling, view type,
// alignment, scrollbar/sidebar visibility. We deliberately do NOT touch:
//   * Identity fields (name, parentCollectionIndex, isSubcollection,
//     collectionIcon).
//   * Path/content fields (mediaDirectory, artworkDirectory, videoDirectory,
//     manualDirectory, extensions, launcher/core/launch params, extract*).
//   * Scan-affecting flags (includeContent/ArtworkSubfolders,
//     show*SubcollectionItems, hideSubfolderTitles, showHiddenFolders,
//     showAllSubfolderItems) — propagating these would silently trigger
//     rescans on unrelated collections.
//
// The per-category field copy itself lives in
// SettingsTreeHelpers::copyAppearanceAndLayoutFields (settingsdialogtreehelpers.cpp)
// so the allowlist is unit-testable; the deliberate field-by-field (never
// whole-leaf) copying rationale is documented there.

int SettingsDialog::applyCategoriesToIndices(const QList<int> &targetIndices,
                                             ApplySettingsDialog::FieldCategories categories,
                                             int sourceIndex) {
  const int applied = SettingsTreeHelpers::applyCategoriesToLists(
      collections, m_workingCollections, targetIndices, categories, sourceIndex);
  // Kartend-1kkk2: the Sidebars category carries how the system glyph LOOKS
  // but not which machine it names, so each target now resolves its own from
  // its own name. Without this, applying a root's settings down the tree
  // switched the glyph on for every subcollection and left them all blank —
  // which is exactly how it was reported.
  if (applied > 0 && categories.testFlag(ApplySettingsDialog::Sidebars) &&
      CollectionUtils::isValidIndex(sourceIndex, collections)) {
    const SystemIconSettings &sourceIcon = collections[sourceIndex].systemIcon;
    if (sourceIcon.enabled) {
      // Enumerated ONCE for the whole apply, against the pack the propagated
      // settings will actually resolve through — a per-target walk of the
      // assets tree would be a directory scan per collection.
      const QString assetsDir =
          RetroArchUtils::resolveAssetsDirectory(m_generalSettings.launchers.retroarchConfigPath);
      const QString pack = RetroArchIcons::resolvePack(sourceIcon.subject, sourceIcon.packOverride,
                                                       RetroArchIcons::discoverPacks(assetsDir));
      SettingsTreeHelpers::resolveSystemIconIdentities(
          collections, &m_workingCollections, targetIndices,
          RetroArchIcons::discoverSystems(assetsDir, pack));
    }
  }
  return applied;
}

void SettingsDialog::duplicateCollection() {
  // full copy of the currently-selected collection. The user chose 1a
  // (full copy: paths/launchers/everything except name) and 2c (ask for
  // parent at duplicate time) during scoping. Runtime-only state is
  // stripped because it never belongs in a fresh copy.
  if (!CollectionUtils::isValidIndex(currentCollectionIndex, &collections)) {
    QMessageBox::information(this, tr("Duplicate Collection"),
                             tr("Select a collection first to duplicate it."));
    return;
  }

  if (collections[currentCollectionIndex].isPlaylist) {
    QMessageBox::information(
        this, tr("Duplicate Collection"),
        tr("Playlists are virtual collections and cannot be duplicated here. Use the "
           "playlist controls to create a new playlist."));
    return;
  }

  // Snapshot the live form first so the duplicate captures what the user
  // currently sees, not the last-saved state.
  saveCollectionFromUI(currentCollectionIndex);

  // Flush global general settings too (Kartend-c427s) — saveCollectionFromUI
  // writes only the per-collection row, so an in-memory title-tint edit consumed
  // off the appearance-colors widget would otherwise be dropped here.
  if (auto result = saveGeneralSettingsFromUI(); result.isError()) {
    ErrorDialog::showError(this, result.error());
  }
  m_originalGeneralSettings = m_generalSettings;

  const CollectionConfig source = collections[currentCollectionIndex];

  QDialog dlg(this);
  dlg.setWindowTitle(tr("Duplicate Collection"));
  auto *layout = new QFormLayout(&dlg);

  auto *nameEdit = new QLineEdit(&dlg);
  nameEdit->setText(source.name + tr(" copy"));
  nameEdit->selectAll();
  layout->addRow(tr("Name:"), nameEdit);

  auto *parentCombo = new QComboBox(&dlg);
  const SettingsTreeHelpers::ParentComboModel comboModel =
      SettingsTreeHelpers::buildDuplicateParentModel(collections, source.parentCollectionIndex,
                                                     tr("None"));
  parentCombo->addItems(comboModel.labels);
  const QList<int> parentMapping = comboModel.mapping;
  parentCombo->setCurrentIndex(comboModel.selectedRow);
  layout->addRow(tr("Parent collection:"), parentCombo);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  layout->addRow(buttonBox);
  QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  const QString name = nameEdit->text().trimmed();
  switch (SettingsTreeHelpers::validateDuplicateName(name, collections)) {
  case SettingsTreeHelpers::DuplicateNameError::Empty:
    QMessageBox::warning(this, tr("Duplicate Collection"), tr("Collection name cannot be empty."));
    return;
  case SettingsTreeHelpers::DuplicateNameError::Unsafe:
    QMessageBox::warning(
        this, tr("Invalid Collection Name"),
        tr("Collection names cannot contain '/', '\\\\', or '..' — those would inject a "
           "traversal segment into the launcher path when '%%collection%%' is substituted."));
    return;
  case SettingsTreeHelpers::DuplicateNameError::Duplicate:
    QMessageBox::warning(
        this, tr("Duplicate Collection"),
        tr("A collection named \"%1\" already exists. Pick a different name.").arg(name));
    return;
  case SettingsTreeHelpers::DuplicateNameError::Ok:
    break;
  }

  const int parentIdx = parentMapping.value(parentCombo->currentIndex(), -1);

  const CollectionConfig copy = SettingsTreeHelpers::makeDuplicateConfig(source, name, parentIdx);

  collections.append(copy);
  m_workingCollections.append(copy);
  const int newIndex = collections.size() - 1;
  currentCollectionIndex = newIndex;

  updateCollectionTreeWidget();
  expandPathToCollection(newIndex);
  if (auto *item = m_treeManager ? m_treeManager->itemAt(newIndex) : nullptr) {
    collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
  }

  loadCollectionToUI(newIndex);
  originalCollection = m_workingCollections[newIndex];
  m_collectionSaved = true;
  updateSaveButtonStyle();

  // Persist immediately so the duplicate survives a Cancel of the outer
  // dialog, matching addCollection()'s behaviour.
  emit collectionSaved(collections);
}

int SettingsDialog::propagateAppearanceToIndicesSilently(const QList<int> &targetIndices) {
  // silent propagation used by the Settings Mode auto-apply path. Skips
  // the per-category dialog because the mode itself is the user's opt-in
  // — the silent path always copies the full curated subset. Caller is
  // responsible for emitting collectionSaved() and refreshing the tree.
  return applyCategoriesToIndices(targetIndices, ApplySettingsDialog::All, currentCollectionIndex);
}

void SettingsDialog::copySettingsFromOtherCollection() {
  // pull-from-source. The user picks a source collection and a category
  // mask via ApplySettingsDialog, then we overwrite the currently-edited
  // collection's selected fields. Unlike the push paths we don't auto-emit
  // collectionSaved — instead we mark the form dirty so the user can
  // review (and undo via Cancel) before committing.
  if (!CollectionUtils::isValidIndex(currentCollectionIndex, collections)) {
    QMessageBox::information(this, tr("Copy Settings"),
                             tr("Select a target collection first, then use Copy Settings "
                                "From... to pull values from another collection."));
    return;
  }
  if (collections[currentCollectionIndex].isPlaylist) {
    QMessageBox::information(this, tr("Copy Settings"),
                             tr("Playlists are virtual collections and have no editable "
                                "appearance settings to overwrite."));
    return;
  }

  // Snapshot the form first so an in-progress edit isn't silently discarded
  // when we reload the UI after the copy.
  saveCollectionFromUI(currentCollectionIndex);

  // Flush global general settings too (Kartend-c427s) — saveCollectionFromUI
  // writes only the per-collection row, so an in-memory title-tint edit consumed
  // off the appearance-colors widget would otherwise be dropped here.
  if (auto result = saveGeneralSettingsFromUI(); result.isError()) {
    ErrorDialog::showError(this, result.error());
  }
  m_originalGeneralSettings = m_generalSettings;

  // Count eligible source collections (everything except current + playlists)
  // before opening the dialog so we can give a useful empty-state message.
  int eligible = 0;
  for (int i = 0; i < collections.size(); ++i) {
    if (i == currentCollectionIndex || collections[i].isPlaylist) {
      continue;
    }
    ++eligible;
  }
  if (eligible == 0) {
    QMessageBox::information(this, tr("Copy Settings"),
                             tr("There are no other collections to copy from."));
    return;
  }

  const QString targetName = collections[currentCollectionIndex].name;
  ApplySettingsDialog dialog(ApplySettingsDialog::Mode::Pull, collections, currentCollectionIndex,
                             ApplySettingsDialog::All, this);
  dialog.setHeaderText(tr("Pick a collection to copy settings from onto \"%1\". Tick the "
                          "categories to overwrite — paths, extensions, launchers and "
                          "scan-related flags are never copied.")
                           .arg(targetName));
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const int sourceIdx = dialog.selectedSourceIndex();
  if (!CollectionUtils::isValidIndex(sourceIdx, collections)) {
    return;
  }
  const ApplySettingsDialog::FieldCategories categories = dialog.selectedCategories();
  if (categories == ApplySettingsDialog::None) {
    QMessageBox::information(this, tr("Copy Settings"),
                             tr("No categories were selected — nothing was copied."));
    return;
  }

  // Single-target apply with the picked source. applyCategoriesToIndices
  // skips when target == source so passing the current index is safe even
  // though the dialog already excludes it.
  const int applied = applyCategoriesToIndices({currentCollectionIndex}, categories, sourceIdx);
  if (applied == 0) {
    return;
  }

  // Reload the form so the user immediately sees the copied values and can
  // review before saving. Don't emit collectionSaved — the user has not
  // yet committed the change; saving from the toolbar (or accepting the
  // dialog) is what flushes to disk.
  loadCollectionToUI(currentCollectionIndex);
  checkForChanges();
  updateSaveButtonStyle();

  QMessageBox::information(this, tr("Copy Settings"),
                           tr("Copied settings from \"%1\" onto \"%2\". Review the form and "
                              "click Save to commit, or Cancel the dialog to discard.")
                               .arg(collections[sourceIdx].name)
                               .arg(targetName));
}
