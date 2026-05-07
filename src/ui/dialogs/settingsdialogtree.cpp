// Sibling translation unit for SettingsDialog: tree population, add/remove,
// parent rebuild, circular-reference checks.
#include <algorithm>
#include <functional>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "applysettingsdialog.h"
#include "collectiontreewidget.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

void SettingsDialog::updateCollectionTreeWidget() {
  if (!collectionTreeWidget) {
    return;
  }
  collectionTreeWidget->clear();
  itemToCollectionIndex.clear();
  collectionIndexToItem.clear();
  collectionIndexToLinkedItems.clear();
  populateTreeWidget();
  populateLinkedAppearances();
  // Enable delete button when a collection is selected
  updateDeleteButtonState();
}

void SettingsDialog::expandPathToCollection(int collectionIndex) {
  if (!CollectionUtils::isValidIndex(collectionIndex, &collections)) {
    return;
  }
  if (!collectionIndexToItem.contains(collectionIndex)) {
    return;
  }

  QList<int> pathIndices;
  int currentIndex = collectionIndex;
  while (CollectionUtils::isValidIndex(currentIndex, &collections)) {
    pathIndices.prepend(currentIndex);
    const CollectionConfig &config = collections[currentIndex];
    currentIndex = config.parentCollectionIndex;
  }
  for (int i = 0; i < pathIndices.size() - 1; ++i) {
    int index = pathIndices[i];
    if (collectionIndexToItem.contains(index)) {
      QTreeWidgetItem *item = collectionIndexToItem[index];
      if (item) {
        item->setExpanded(true);
      }
    }
  }
}

void SettingsDialog::populateTreeWidget() {
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == -1) {
      createTreeItem(i);
    }
  }

  bool foundSubcollection = true;
  int maxIterations = collections.size();
  int iteration = 0;

  while (foundSubcollection && iteration < maxIterations) {
    foundSubcollection = false;
    iteration++;

    for (int i = 0; i < collections.size(); ++i) {
      if (collectionIndexToItem.contains(i)) {
        continue;
      }
      int parentIndex = collections[i].parentCollectionIndex;
      if (parentIndex >= 0 && parentIndex < collections.size()) {
        if (collectionIndexToItem.contains(parentIndex)) {
          QTreeWidgetItem *parentItem = collectionIndexToItem[parentIndex];
          createTreeItem(i, parentItem);
          foundSubcollection = true;
        }
      }
    }
  }

  for (int i = 0; i < collections.size(); ++i) {
    if (!collectionIndexToItem.contains(i)) {
      collections[i].parentCollectionIndex = -1;
      collections[i].isSubcollection = false;
      createTreeItem(i);
    }
  }
}

auto SettingsDialog::createTreeItem(int collectionIndex, QTreeWidgetItem *parent)
    -> QTreeWidgetItem * {
  QTreeWidgetItem *item =
      (parent) ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(collectionTreeWidget);
  item->setText(0, collections[collectionIndex].name);
  item->setFlags(item->flags() | Qt::ItemIsEditable);
  // Kartend-gzmk: Qt::UserRole flag distinguishes the canonical row (false)
  // from linked-appearance mirrors (true). Most slots branch on this flag.
  item->setData(0, Qt::UserRole, false);
  itemToCollectionIndex[item] = collectionIndex;
  collectionIndexToItem[collectionIndex] = item;
  return item;
}

void SettingsDialog::populateLinkedAppearances() {
  // Build a name → index map once so each link resolves in O(1).
  QHash<QString, int> nameToIndex;
  nameToIndex.reserve(collections.size());
  for (int i = 0; i < collections.size(); ++i) {
    nameToIndex.insert(collections[i].name, i);
  }

  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &c = collections[i];
    if (c.additionalParentNames.isEmpty()) {
      continue;
    }
    // Track parents already mirrored so a stutter in the name list (the
    // user listed the same parent twice) doesn't render two identical
    // appearances.
    QSet<int> alreadyMirrored;
    for (const QString &parentName : c.additionalParentNames) {
      const int parentIdx = nameToIndex.value(parentName, -1);
      if (parentIdx < 0 || parentIdx == i) {
        continue;
      }
      if (parentIdx == c.parentCollectionIndex) {
        continue;
      }
      if (alreadyMirrored.contains(parentIdx)) {
        continue;
      }
      QTreeWidgetItem *parentItem = collectionIndexToItem.value(parentIdx);
      if (!parentItem) {
        continue;
      }
      auto *linkedItem = new QTreeWidgetItem(parentItem);
      linkedItem->setText(0, c.name);
      // Italic font marks the alias visually; tooltip points to the
      // canonical home so the user always knows where the row "lives".
      QFont font = linkedItem->font(0);
      font.setItalic(true);
      linkedItem->setFont(0, font);
      const QString primaryName =
          (c.parentCollectionIndex >= 0 && c.parentCollectionIndex < collections.size())
              ? collections[c.parentCollectionIndex].name
              : tr("(root)");
      linkedItem->setToolTip(0, tr("Linked appearance — primary parent: %1").arg(primaryName));
      // Strip edit/drag/drop — linked items are read-only mirrors.
      Qt::ItemFlags flags = linkedItem->flags();
      flags &= ~(Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
      linkedItem->setFlags(flags);
      linkedItem->setData(0, Qt::UserRole, true);

      itemToCollectionIndex[linkedItem] = i;
      collectionIndexToLinkedItems[i].append(linkedItem);
      alreadyMirrored.insert(parentIdx);
    }
  }
}

void SettingsDialog::propagateCollectionNameChange(const QString &oldName, const QString &newName) {
  if (oldName == newName) {
    return;
  }
  for (int i = 0; i < collections.size(); ++i) {
    QStringList &names = collections[i].additionalParentNames;
    if (!names.contains(oldName)) {
      continue;
    }
    if (newName.isEmpty()) {
      names.removeAll(oldName);
    } else {
      for (QString &n : names) {
        if (n == oldName) {
          n = newName;
        }
      }
    }
    if (i < m_workingCollections.size()) {
      m_workingCollections[i].additionalParentNames = names;
    }
  }
}

// Handles selection changes; supports deselection state
void SettingsDialog::onTreeItemSelectionChanged() {
  QList<QTreeWidgetItem *> selectedItems = collectionTreeWidget->selectedItems();
  if (selectedItems.isEmpty()) {
    currentTreeItem = nullptr;
    currentCollectionIndex = -1;
    updateDeleteButtonState();
    return;
  }

  QTreeWidgetItem *item = selectedItems.first();
  if (!itemToCollectionIndex.contains(item)) {
    return;
  }

  int newIndex = itemToCollectionIndex[item];
  if (newIndex == currentCollectionIndex) {
    return;
  }

  const int previousIndex = currentCollectionIndex;
  if (previousIndex >= 0 && previousIndex < m_workingCollections.size() &&
      !resolveUnsavedChanges(tr("switching collections"), true)) {
    if (collectionIndexToItem.contains(previousIndex)) {
      QSignalBlocker blocker(collectionTreeWidget);
      if (auto *previousItem = collectionIndexToItem[previousIndex]) {
        collectionTreeWidget->setCurrentItem(previousItem);
        previousItem->setSelected(true);
      }
    }
    return;
  }

  if (collectionIndexToItem.contains(newIndex)) {
    item = collectionIndexToItem[newIndex];
  }

  if (collectionTreeWidget && item) {
    QSignalBlocker blocker(collectionTreeWidget);
    collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
  }

  currentCollectionIndex = newIndex;
  currentTreeItem = item;
  if (newIndex >= 0 && newIndex < m_workingCollections.size()) {
    originalCollection = m_workingCollections[newIndex];
  } else {
    originalCollection = CollectionConfig();
  }
  loadCollectionToUI(newIndex);
  m_collectionSaved = true;
  updateSaveButtonStyle();
  updateDeleteButtonState();
}

void SettingsDialog::onTreeItemChanged(QTreeWidgetItem *item, int column) {
  if (column != 0 || !itemToCollectionIndex.contains(item)) {
    return;
  }
  // Kartend-gzmk: rename only fires for the canonical row. Linked mirrors
  // are flagged read-only via ItemIsEditable, but defensively skip if a
  // signal sneaks in.
  if (item->data(0, Qt::UserRole).toBool()) {
    return;
  }
  int collectionIndex = itemToCollectionIndex[item];
  if (!CollectionUtils::isValidIndex(collectionIndex, &collections) ||
      !CollectionUtils::isValidIndex(collectionIndex, &m_workingCollections)) {
    return;
  }

  QString newName = item->text(0);
  const QString oldName = collections[collectionIndex].name;

  if (newName != oldName) {
    collections[collectionIndex].name = newName;
    m_workingCollections[collectionIndex].name = newName;

    // Kartend-gzmk: rename in-place propagates to linked mirrors so they
    // don't drift, and rewrites references in other collections'
    // additionalParentNames so links survive the rename.
    for (QTreeWidgetItem *linked : collectionIndexToLinkedItems.value(collectionIndex)) {
      if (linked) {
        linked->setText(0, newName);
      }
    }
    propagateCollectionNameChange(oldName, newName);

    bool revertedToOriginal =
        (collectionIndex == currentCollectionIndex && newName == originalCollection.name);
    m_collectionSaved = revertedToOriginal && !hasUnsavedChanges();
    if (!revertedToOriginal) {
      m_collectionSaved = false;
    }

    updateSaveButtonStyle();
  }
}

void SettingsDialog::addCollection() {
  bool parseOk;
  QString name = QInputDialog::getText(this, "Add Collection",
                                       "Enter collection name:", QLineEdit::Normal, "", &parseOk);
  if (!parseOk || name.isEmpty()) {
    return;
  }

  if (currentCollectionIndex >= 0 && currentCollectionIndex < collections.size()) {
    saveCollectionFromUI(currentCollectionIndex);
  }

  CollectionConfig newCollection;
  newCollection.name = name;
  newCollection.launcherPath = "";
  newCollection.corePath = "";
  newCollection.launchParameters = "";
  newCollection.mediaDirectory = "";
  newCollection.artworkDirectory = "";
  newCollection.videoDirectory = "";
  newCollection.manualDirectory = "";
  newCollection.extensions = QStringList();
  newCollection.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
  newCollection.sidebarVisible = false;
  newCollection.parentCollectionIndex = -1;
  newCollection.isSubcollection = false;
  newCollection.showAllSubcollectionItems = false;
  newCollection.horizontalAlignment = HorizontalAlignment::Center;
  newCollection.fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
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
    newCollection.gridWidth = parent.gridWidth;
    newCollection.horizontalGridHeight = parent.horizontalGridHeight;
    newCollection.gridWidthSidebarHidden = parent.gridWidthSidebarHidden;
    newCollection.horizontalGridHeightSidebarHidden = parent.horizontalGridHeightSidebarHidden;
    newCollection.horizontalSpacing = parent.horizontalSpacing;
    newCollection.verticalSpacing = parent.verticalSpacing;
    newCollection.itemWidth = parent.itemWidth;
    newCollection.itemHeight = parent.itemHeight;
    newCollection.fontSize = parent.fontSize;
    newCollection.hideHorizontalScrollbar = parent.hideHorizontalScrollbar;
    newCollection.hideVerticalScrollbar = parent.hideVerticalScrollbar;
    newCollection.sidebarMode = parent.sidebarMode;
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

  if (collectionIndexToItem.contains(newIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[newIndex];
    if (item) {
      collectionTreeWidget->setCurrentItem(item);
      item->setSelected(true);
    }
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
    bool ok = false;
    QString name =
        QInputDialog::getText(this, tr("Create Collection"),
                              tr("No collections found. Enter a name for your first collection:"),
                              QLineEdit::Normal, "", &ok);

    if (!ok) {
      // User cancelled - they must create a collection to use settings
      QMessageBox::warning(this, tr("Collection Required"),
                           tr("A collection is required to configure settings. "
                              "Please enter a collection name."));
      continue;
    }

    if (name.trimmed().isEmpty()) {
      QMessageBox::warning(this, tr("Invalid Name"),
                           tr("Collection name cannot be empty. Please enter a valid name."));
      continue;
    }

    // Create the new root collection
    CollectionConfig newCollection;
    newCollection.name = name.trimmed();
    newCollection.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
    newCollection.parentCollectionIndex = -1;
    newCollection.isSubcollection = false;

    collections.append(newCollection);
    m_workingCollections.append(newCollection);
    currentCollectionIndex = collections.size() - 1;

    m_collectionSaved = false;
    break;
  }
}

void SettingsDialog::updateParentCollectionComboBox(int currentIndex) {
  if (!ui->parentCollectionComboBox) {
    return;
  }

  QSignalBlocker blocker(ui->parentCollectionComboBox);
  ui->parentCollectionComboBox->clear();
  ui->parentCollectionComboBox->addItem("None");
  m_parentCollectionMapping.clear();
  m_parentCollectionMapping.append(-1);

  for (int i = 0; i < collections.size(); ++i) {
    if (i == currentIndex) {
      continue;
    }
    if (wouldCreateCircularReference(currentIndex, i)) {
      continue;
    }
    ui->parentCollectionComboBox->addItem(collections[i].name);
    m_parentCollectionMapping.append(i);
  }

  int desiredParentIndex = (currentIndex >= 0 && currentIndex < collections.size())
                               ? collections[currentIndex].parentCollectionIndex
                               : -1;
  int targetDropdownIndex = m_parentCollectionMapping.indexOf(desiredParentIndex);
  if (targetDropdownIndex < 0) {
    targetDropdownIndex = 0;
  }
  ui->parentCollectionComboBox->setCurrentIndex(targetDropdownIndex);
}

auto SettingsDialog::wouldCreateCircularReference(int childIndex, int potentialParentIndex) const
    -> bool {
  if (childIndex < 0 || childIndex >= collections.size() || potentialParentIndex < 0 ||
      potentialParentIndex >= collections.size()) {
    return true;
  }
  if (potentialParentIndex == childIndex) {
    return true;
  }

  int currentParent = collections[potentialParentIndex].parentCollectionIndex;
  std::set<int> visited;
  while (currentParent >= 0 && currentParent < collections.size()) {
    if (visited.contains(currentParent)) {
      return true;
    }
    visited.insert(currentParent);
    if (currentParent == childIndex) {
      return true;
    }
    currentParent = collections[currentParent].parentCollectionIndex;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Kartend-63o: propagate appearance/layout settings to other collections
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
//     manualDirectory,
//     extensions, launcher/core/launch params, extract* flags).
//   * Scan-affecting flags (includeContent/ArtworkSubfolders,
//     show*SubcollectionItems, hideSubfolderTitles, showHiddenFolders,
//     showAllSubfolderItems) — propagating these would silently trigger
//     rescans on unrelated collections, which the user almost certainly
//     doesn't want from a "copy my look to everything" action.

namespace {

// Kartend-iyk: copy a per-category subset from @p src onto @p dst. Each flag
// in @p categories enables one logical group of fields. Categories not in
// the mask leave @p dst's existing values untouched. Identity, paths,
// launcher list, and scan-affecting flags are never copied regardless of
// the mask — those still require an explicit per-collection edit.
void copyAppearanceAndLayoutFields(const CollectionConfig &src, CollectionConfig &dst,
                                   ApplySettingsDialog::FieldCategories categories) {
  if (categories.testFlag(ApplySettingsDialog::GridLayout)) {
    dst.gridWidth = src.gridWidth;
    dst.horizontalGridHeight = src.horizontalGridHeight;
    dst.gridWidthSidebarHidden = src.gridWidthSidebarHidden;
    dst.horizontalGridHeightSidebarHidden = src.horizontalGridHeightSidebarHidden;
    dst.horizontalSpacing = src.horizontalSpacing;
    dst.verticalSpacing = src.verticalSpacing;
    dst.itemWidth = src.itemWidth;
    dst.itemHeight = src.itemHeight;
    dst.cornerRadius = src.cornerRadius;
    dst.horizontalAlignment = src.horizontalAlignment;
    dst.viewType = src.viewType;
  }
  if (categories.testFlag(ApplySettingsDialog::ItemText)) {
    dst.fontSize = src.fontSize;
    dst.customFontFamily = src.customFontFamily;
  }
  if (categories.testFlag(ApplySettingsDialog::Visibility)) {
    dst.hideTitles = src.hideTitles;
    dst.hideSubcollectionTitles = src.hideSubcollectionTitles;
    dst.hideHorizontalScrollbar = src.hideHorizontalScrollbar;
    dst.hideVerticalScrollbar = src.hideVerticalScrollbar;
    dst.sidebarMode = src.sidebarMode;
  }
  if (categories.testFlag(ApplySettingsDialog::Colors)) {
    dst.backgroundType = src.backgroundType;
    dst.backgroundColor = src.backgroundColor;
    dst.backgroundImage = src.backgroundImage;
    dst.backgroundVideo = src.backgroundVideo;
    dst.primaryColor = src.primaryColor;
    dst.tileColor = src.tileColor;
    dst.selectionColor = src.selectionColor;
    // Kartend-guo5 / qbp3 / y25g / eq8r: header logo + vignette + parallax
    // + backdrop blur ride along with the Colors category since they're
    // presented in the same dialog area and users intuitively expect
    // "apply theme" to cover them too.
    dst.headerLogoImage = src.headerLogoImage;
    dst.headerLogoPosition = src.headerLogoPosition;
    dst.vignetteEnabled = src.vignetteEnabled;
    dst.vignetteIntensity = src.vignetteIntensity;
    dst.wallpaperParallax = src.wallpaperParallax;
    dst.parallaxStrength = src.parallaxStrength;
    dst.toolbarBackdropBlur = src.toolbarBackdropBlur;
    dst.backdropBlurRadius = src.backdropBlurRadius;
  }
  if (categories.testFlag(ApplySettingsDialog::ListView)) {
    dst.listFontSize = src.listFontSize;
    dst.listRowHeight = src.listRowHeight;
    dst.listRowColor = src.listRowColor;
    dst.listAltRowColor = src.listAltRowColor;
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
  // Kartend-f5i9: full copy of the currently-selected collection. The user
  // chose 1a (full copy: paths/launchers/everything except name) and 2c (ask
  // for parent at duplicate time) during scoping. Runtime-only state is
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
  copy.currentSubfolder.clear();
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
  if (collectionIndexToItem.contains(newIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[newIndex];
    if (item) {
      collectionTreeWidget->setCurrentItem(item);
      item->setSelected(true);
    }
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
  // Kartend-enq: silent propagation used by the Settings Mode auto-apply
  // path. Skips the per-category dialog because the mode itself is the
  // user's opt-in — the silent path always copies the full curated subset.
  // Caller is responsible for emitting collectionSaved() and refreshing
  // the tree once.
  return applyCategoriesToIndices(targetIndices, ApplySettingsDialog::All, currentCollectionIndex);
}

void SettingsDialog::copySettingsFromOtherCollection() {
  // Kartend-iyk: pull-from-source. The user picks a source collection and
  // a category mask via ApplySettingsDialog, then we overwrite the
  // currently-edited collection's selected fields. Unlike the push paths
  // we don't auto-emit collectionSaved — instead we mark the form dirty so
  // the user can review (and undo via Cancel) before committing.
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
  // review before saving. Don't emit collectionSaved — the user has not yet
  // committed the change; saving from the toolbar (or accepting the dialog)
  // is what flushes to disk.
  loadCollectionToUI(currentCollectionIndex);
  checkForChanges();
  updateSaveButtonStyle();

  QMessageBox::information(this, tr("Copy Settings"),
                           tr("Copied settings from \"%1\" onto \"%2\". Review the form and "
                              "click Save to commit, or Cancel the dialog to discard.")
                               .arg(collections[sourceIdx].name)
                               .arg(targetName));
}

// ─────────────────────────────────────────────────────────────────────────────
// Kartend-j613: tree context menu + drag-drop reparenting resync.
// ─────────────────────────────────────────────────────────────────────────────

void SettingsDialog::setSubtreeExpanded(QTreeWidgetItem *item, bool expanded) {
  if (!item) {
    return;
  }
  item->setExpanded(expanded);
  for (int i = 0; i < item->childCount(); ++i) {
    setSubtreeExpanded(item->child(i), expanded);
  }
}

void SettingsDialog::onTreeContextMenuRequested(const QPoint &pos) {
  if (!collectionTreeWidget) {
    return;
  }
  QTreeWidgetItem *target = collectionTreeWidget->itemAt(pos);

  QMenu menu(collectionTreeWidget);
  // Kartend-gzmk: linked-mirror rows are read-only viewports; per-row
  // actions (rename, duplicate, delete) belong on the canonical row.
  const bool isLinked = target && target->data(0, Qt::UserRole).toBool();
  // Per-row actions only make sense when the click hit a non-linked item.
  if (target && !isLinked) {
    QAction *renameAction = menu.addAction(tr("Rename"));
    QAction *duplicateAction = menu.addAction(tr("Duplicate..."));
    QAction *deleteAction = menu.addAction(tr("Delete"));
    menu.addSeparator();
    QAction *copyFromAction = menu.addAction(tr("Copy Settings From..."));
    menu.addSeparator();
    QAction *expandSubAction = menu.addAction(tr("Expand subtree"));
    QAction *collapseSubAction = menu.addAction(tr("Collapse subtree"));
    // Subtree expand/collapse is only meaningful when the row has children.
    const bool hasChildren = target->childCount() > 0;
    expandSubAction->setEnabled(hasChildren);
    collapseSubAction->setEnabled(hasChildren);
    menu.addSeparator();
    QAction *expandAllAction = menu.addAction(tr("Expand all"));
    QAction *collapseAllAction = menu.addAction(tr("Collapse all"));

    // Selecting the row first ensures the form/save state is consistent
    // with the action — matches what clicking a row before pressing the
    // toolbar button would do. If the user has unsaved changes and cancels
    // the resulting prompt (handled by onTreeItemSelectionChanged), the
    // selection reverts and we abort the menu action so we never operate
    // on the wrong collection.
    auto switchToTarget = [this, target]() -> bool {
      if (currentTreeItem == target) {
        return true;
      }
      collectionTreeWidget->setCurrentItem(target);
      return currentTreeItem == target;
    };
    connect(renameAction, &QAction::triggered, this, [this, target, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      collectionTreeWidget->editItem(target, 0);
    });
    connect(duplicateAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      duplicateCollection();
    });
    connect(deleteAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      removeCollection();
    });
    connect(copyFromAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      copySettingsFromOtherCollection();
    });
    connect(expandSubAction, &QAction::triggered, this,
            [this, target]() { setSubtreeExpanded(target, true); });
    connect(collapseSubAction, &QAction::triggered, this,
            [this, target]() { setSubtreeExpanded(target, false); });
    connect(expandAllAction, &QAction::triggered, collectionTreeWidget, &QTreeWidget::expandAll);
    connect(collapseAllAction, &QAction::triggered, collectionTreeWidget,
            &QTreeWidget::collapseAll);
  } else {
    // Empty area: only the all-tree actions apply.
    QAction *expandAllAction = menu.addAction(tr("Expand all"));
    QAction *collapseAllAction = menu.addAction(tr("Collapse all"));
    connect(expandAllAction, &QAction::triggered, collectionTreeWidget, &QTreeWidget::expandAll);
    connect(collapseAllAction, &QAction::triggered, collectionTreeWidget,
            &QTreeWidget::collapseAll);
  }

  menu.exec(collectionTreeWidget->viewport()->mapToGlobal(pos));
}

void SettingsDialog::onTreeRearranged() {
  // Walk the post-drop tree and resync parentCollectionIndex /
  // isSubcollection on every collection. This is the only place where
  // parent linkage gets rewritten by user input outside the parent combo.
  // Kartend-gzmk: linked mirrors aren't draggable, but a drop can still
  // shuffle them along with their primary parent — so we skip linked
  // items when resolving the canonical primary parent for each collection
  // and recurse into them so their children (if any — currently none) are
  // still visited correctly.
  std::function<void(QTreeWidgetItem *, int)> walk = [&](QTreeWidgetItem *item, int parentIdx) {
    if (!item) {
      return;
    }
    const bool isLinked = item->data(0, Qt::UserRole).toBool();
    int idx = itemToCollectionIndex.value(item, -1);
    if (!isLinked && CollectionUtils::isValidIndex(idx, &collections)) {
      collections[idx].parentCollectionIndex = parentIdx;
      collections[idx].isSubcollection = (parentIdx >= 0);
      if (idx < m_workingCollections.size()) {
        m_workingCollections[idx].parentCollectionIndex = parentIdx;
        m_workingCollections[idx].isSubcollection = (parentIdx >= 0);
      }
    }
    // Children of a linked mirror inherit the same parent semantics from
    // the mirror's position (i.e. their primary parent is whoever the
    // mirror is under), since the mirror itself is just a viewport.
    const int childParentIdx = isLinked ? parentIdx : idx;
    for (int i = 0; i < item->childCount(); ++i) {
      walk(item->child(i), childParentIdx);
    }
  };
  for (int i = 0; i < collectionTreeWidget->topLevelItemCount(); ++i) {
    walk(collectionTreeWidget->topLevelItem(i), -1);
  }

  // Persist immediately so the rearranged tree survives a Cancel of the
  // outer dialog, matching addCollection() / duplicateCollection().
  emit collectionSaved(collections);
  m_collectionSaved = true;
  updateSaveButtonStyle();
}
