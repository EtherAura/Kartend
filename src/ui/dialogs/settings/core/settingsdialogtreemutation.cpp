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
#include "pathutils.h"
#include "settingsdialog.h"
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
    const CollectionConfig &parent = m_workingCollections[parentIdx];
    newCollection.parentCollectionIndex = parentIdx;
    newCollection.isSubcollection = true;
    newCollection.gridLayout.gridWidth = parent.gridLayout.gridWidth;
    newCollection.gridLayout.horizontalGridHeight = parent.gridLayout.horizontalGridHeight;
    newCollection.gridLayout.gridWidthSidebarHidden = parent.gridLayout.gridWidthSidebarHidden;
    newCollection.gridLayout.horizontalGridHeightSidebarHidden =
        parent.gridLayout.horizontalGridHeightSidebarHidden;
    newCollection.gridLayout.horizontalSpacing = parent.gridLayout.horizontalSpacing;
    newCollection.gridLayout.verticalSpacing = parent.gridLayout.verticalSpacing;
    newCollection.gridLayout.itemWidth = parent.gridLayout.itemWidth;
    newCollection.gridLayout.itemHeight = parent.gridLayout.itemHeight;
    newCollection.gridLayout.fontSize = parent.gridLayout.fontSize;
    newCollection.gridLayout.hideHorizontalScrollbar = parent.gridLayout.hideHorizontalScrollbar;
    newCollection.gridLayout.hideVerticalScrollbar = parent.gridLayout.hideVerticalScrollbar;
    newCollection.sidebar.sidebarMode = parent.sidebar.sidebarMode;
    newCollection.viewType = parent.viewType;
    newCollection.showAllSubcollectionItems = parent.showAllSubcollectionItems;
    newCollection.horizontalAlignment = parent.horizontalAlignment;
    newCollection.hideTitles = parent.hideTitles;
    newCollection.hideSubcollectionTitles = parent.hideSubcollectionTitles;
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

namespace {

// copy a per-category subset from @p src onto @p dst. Each flag in @p
// categories enables one logical group of fields. Categories not in the
// mask leave @p dst's existing values untouched. Identity, paths, launcher
// list, and scan-affecting flags are never copied regardless of the mask —
// those still require an explicit per-collection edit.
//
// Kartend-fybhy: the per-field copies below are deliberately an explicit,
// opt-in allowlist — NOT a whole-leaf `dst.background = src.background` /
// `dst.listView = src.listView` assignment. Today the Colors block happens to
// cover every CollectionBackground field and the ListView block every
// ListViewOptions field, so collapsing them would be field-equivalent *right
// now*. It is intentionally left expanded so that a future leaf field which
// should NOT propagate (e.g. a runtime-only member, as FolderBrowsingOptions::
// currentSubfolder already is) does not start propagating silently the moment
// it is added. The flip side — a propagatable field added to a leaf but
// forgotten here — is the drift this issue tracks; when migrating propagation
// onto a per-leaf "propagatable" descriptor, do it leaf-by-leaf rather than by
// swapping these in for raw whole-leaf assignment.
void copyAppearanceAndLayoutFields(const CollectionConfig &src, CollectionConfig &dst,
                                   ApplySettingsDialog::FieldCategories categories) {
  if (categories.testFlag(ApplySettingsDialog::GridLayout)) {
    dst.gridLayout.gridWidth = src.gridLayout.gridWidth;
    dst.gridLayout.horizontalGridHeight = src.gridLayout.horizontalGridHeight;
    dst.gridLayout.gridWidthSidebarHidden = src.gridLayout.gridWidthSidebarHidden;
    dst.gridLayout.horizontalGridHeightSidebarHidden =
        src.gridLayout.horizontalGridHeightSidebarHidden;
    dst.gridLayout.horizontalSpacing = src.gridLayout.horizontalSpacing;
    dst.gridLayout.verticalSpacing = src.gridLayout.verticalSpacing;
    dst.gridLayout.itemWidth = src.gridLayout.itemWidth;
    dst.gridLayout.itemHeight = src.gridLayout.itemHeight;
    dst.gridLayout.cornerRadius = src.gridLayout.cornerRadius;
    dst.horizontalAlignment = src.horizontalAlignment;
    dst.viewType = src.viewType;
  }
  if (categories.testFlag(ApplySettingsDialog::ItemText)) {
    dst.gridLayout.fontSize = src.gridLayout.fontSize;
    dst.customFontFamily = src.customFontFamily;
  }
  if (categories.testFlag(ApplySettingsDialog::Visibility)) {
    dst.hideTitles = src.hideTitles;
    dst.hideSubcollectionTitles = src.hideSubcollectionTitles;
    dst.gridLayout.hideHorizontalScrollbar = src.gridLayout.hideHorizontalScrollbar;
    dst.gridLayout.hideVerticalScrollbar = src.gridLayout.hideVerticalScrollbar;
    dst.sidebar.sidebarMode = src.sidebar.sidebarMode;
  }
  if (categories.testFlag(ApplySettingsDialog::Colors)) {
    dst.background.backgroundType = src.background.backgroundType;
    dst.background.backgroundColor = src.background.backgroundColor;
    dst.background.backgroundImage = src.background.backgroundImage;
    dst.background.backgroundVideo = src.background.backgroundVideo;
    dst.background.primaryColor = src.background.primaryColor;
    dst.background.tileColor = src.background.tileColor;
    dst.background.selectionColor = src.background.selectionColor;
    // / qbp3 / y25g / eq8r: header logo + vignette + parallax
    // + backdrop blur ride along with the Colors category since they're
    // presented in the same dialog area and users intuitively expect
    // "apply theme" to cover them too.
    dst.background.headerLogoImage = src.background.headerLogoImage;
    dst.background.headerLogoPosition = src.background.headerLogoPosition;
    dst.background.vignetteEnabled = src.background.vignetteEnabled;
    dst.background.vignetteIntensity = src.background.vignetteIntensity;
    dst.background.wallpaperParallax = src.background.wallpaperParallax;
    dst.background.parallaxStrength = src.background.parallaxStrength;
    dst.background.toolbarBackdropBlur = src.background.toolbarBackdropBlur;
    dst.background.backdropBlurRadius = src.background.backdropBlurRadius;
  }
  if (categories.testFlag(ApplySettingsDialog::ListView)) {
    dst.listView.listFontSize = src.listView.listFontSize;
    dst.listView.listRowHeight = src.listView.listRowHeight;
    dst.listView.listRowColor = src.listView.listRowColor;
    dst.listView.listAltRowColor = src.listView.listAltRowColor;
  }
  dst.clampValues();
}

} // namespace

int SettingsDialog::applyCategoriesToIndices(const QList<int> &targetIndices,
                                             ApplySettingsDialog::FieldCategories categories,
                                             int sourceIndex) {
  if (categories == ApplySettingsDialog::None) {
    return 0;
  }
  if (!CollectionUtils::isValidIndex(sourceIndex, collections)) {
    return 0;
  }
  const CollectionConfig source = collections[sourceIndex];
  int applied = 0;
  for (int idx : targetIndices) {
    if (idx < 0 || idx >= collections.size()) {
      continue;
    }
    if (idx == sourceIndex) {
      continue;
    }
    copyAppearanceAndLayoutFields(source, collections[idx], categories);
    if (idx < m_workingCollections.size()) {
      copyAppearanceAndLayoutFields(source, m_workingCollections[idx], categories);
    }
    ++applied;
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
  QList<int> parentMapping;
  parentCombo->addItem(tr("None"));
  parentMapping.append(-1);
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].isPlaylist) {
      // Playlists can't be parents — they're not real persisted collections.
      continue;
    }
    parentCombo->addItem(collections[i].name);
    parentMapping.append(i);
  }
  int defaultMapIndex = parentMapping.indexOf(source.parentCollectionIndex);
  if (defaultMapIndex < 0) {
    defaultMapIndex = 0;
  }
  parentCombo->setCurrentIndex(defaultMapIndex);
  layout->addRow(tr("Parent collection:"), parentCombo);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  layout->addRow(buttonBox);
  QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  const QString name = nameEdit->text().trimmed();
  if (name.isEmpty()) {
    QMessageBox::warning(this, tr("Duplicate Collection"), tr("Collection name cannot be empty."));
    return;
  }
  auto nameValidation = PathUtils::validateCollectionNameForSubstitution(name);
  if (nameValidation.isError()) {
    QMessageBox::warning(
        this, tr("Invalid Collection Name"),
        tr("Collection names cannot contain '/', '\\\\', or '..' — those would inject a "
           "traversal segment into the launcher path when '%%collection%%' is substituted."));
    return;
  }
  for (const auto &existing : collections) {
    if (existing.name == name) {
      QMessageBox::warning(
          this, tr("Duplicate Collection"),
          tr("A collection named \"%1\" already exists. Pick a different name.").arg(name));
      return;
    }
  }

  const int parentIdx = parentMapping.value(parentCombo->currentIndex(), -1);

  // Full copy. Override only the fields that must differ on a fresh
  // collection: name + parent linkage + runtime state.
  CollectionConfig copy = source;
  copy.name = name;
  copy.parentCollectionIndex = parentIdx;
  copy.isSubcollection = (parentIdx >= 0);
  copy.folderBrowsing.currentSubfolder.clear();
  copy.isPlaylist = false;
  copy.playlistId.clear();
  copy.playlistReservedKind.clear();
  copy.clampValues();

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
