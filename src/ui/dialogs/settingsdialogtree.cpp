// Sibling translation unit for SettingsDialog: tree population, add/remove,
// parent rebuild, circular-reference checks.
#include <algorithm>
#include <functional>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "collectionpickerdialog.h"
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
  populateTreeWidget();
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
  itemToCollectionIndex[item] = collectionIndex;
  collectionIndexToItem[collectionIndex] = item;
  return item;
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

// Copy the appearance/layout subset from @p src onto @p dst. Leaves all
// non-listed fields on @p dst untouched so identity and scan settings
// survive.
void copyAppearanceAndLayoutFields(const CollectionConfig &src, CollectionConfig &dst) {
  // Grid/layout
  dst.gridWidth = src.gridWidth;
  dst.horizontalSpacing = src.horizontalSpacing;
  dst.verticalSpacing = src.verticalSpacing;
  dst.itemWidth = src.itemWidth;
  dst.itemHeight = src.itemHeight;
  dst.fontSize = src.fontSize;
  dst.cornerRadius = src.cornerRadius;
  dst.horizontalAlignment = src.horizontalAlignment;
  dst.viewType = src.viewType;

  // Visibility/chrome
  dst.hideTitles = src.hideTitles;
  dst.hideSubcollectionTitles = src.hideSubcollectionTitles;
  dst.hideHorizontalScrollbar = src.hideHorizontalScrollbar;
  dst.hideVerticalScrollbar = src.hideVerticalScrollbar;
  dst.sidebarMode = src.sidebarMode;

  // Colors/background
  dst.backgroundType = src.backgroundType;
  dst.backgroundColor = src.backgroundColor;
  dst.backgroundImage = src.backgroundImage;
  dst.primaryColor = src.primaryColor;
  dst.tileColor = src.tileColor;
  dst.selectionColor = src.selectionColor;

  // List-mode styling
  dst.listFontSize = src.listFontSize;
  dst.listRowHeight = src.listRowHeight;
  dst.listRowColor = src.listRowColor;
  dst.listAltRowColor = src.listAltRowColor;

  // Text appearance
  dst.customFontFamily = src.customFontFamily;

  dst.clampValues();
}

} // namespace

void SettingsDialog::applyCurrentSettingsToIndices(const QList<int> &targetIndices,
                                                   const QString &scopeLabel) {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= collections.size()) {
    QMessageBox::information(this, tr("Apply Settings"),
                             tr("Select a collection first, then use Apply to "
                                "copy its appearance and layout to %1.")
                                 .arg(scopeLabel));
    return;
  }
  if (targetIndices.isEmpty()) {
    QMessageBox::information(
        this, tr("Apply Settings"),
        tr("There are no collections in the scope \"%1\" to apply to.").arg(scopeLabel));
    return;
  }

  // Snapshot the form first so we copy what the user currently sees, not the
  // last-saved state. saveCollectionFromUI updates m_workingCollections and
  // collections in place.
  saveCollectionFromUI(currentCollectionIndex);

  const QString sourceName = collections[currentCollectionIndex].name;
  const QMessageBox::StandardButton reply =
      QMessageBox::question(this, tr("Apply Settings"),
                            tr("Copy the appearance and layout settings from \"%1\" to %2 (%3 "
                               "collection(s))?\n\n"
                               "This overwrites grid/spacing, item dimensions, fonts, colors, "
                               "background, list-mode styling, view type, alignment, and "
                               "scrollbar/sidebar visibility. Paths, extensions, and scan-related "
                               "flags are left alone.")
                                .arg(sourceName)
                                .arg(scopeLabel)
                                .arg(targetIndices.size()),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }

  const CollectionConfig source = collections[currentCollectionIndex];
  int applied = 0;
  for (int idx : targetIndices) {
    if (idx < 0 || idx >= collections.size()) {
      continue;
    }
    if (idx == currentCollectionIndex) {
      continue;
    }
    copyAppearanceAndLayoutFields(source, collections[idx]);
    if (idx < m_workingCollections.size()) {
      copyAppearanceAndLayoutFields(source, m_workingCollections[idx]);
    }
    ++applied;
  }

  if (applied == 0) {
    return;
  }

  // Persist immediately so the user's click has a durable effect even if
  // they later close the dialog with Cancel (matching how addCollection()
  // emits collectionSaved).
  emit collectionSaved(collections);

  // Refresh the tree + reload current UI so any visible differences show
  // up in the selected collection's form if the user jumps between nodes.
  updateCollectionTreeWidget();
  if (currentCollectionIndex >= 0 && currentCollectionIndex < collections.size()) {
    loadCollectionToUI(currentCollectionIndex);
    originalCollection = m_workingCollections[currentCollectionIndex];
  }
  m_collectionSaved = true;
  updateSaveButtonStyle();

  QMessageBox::information(
      this, tr("Apply Settings"),
      tr("Copied settings from \"%1\" to %2 collection(s).").arg(sourceName).arg(applied));
}

void SettingsDialog::applyCurrentSettingsToAllCollections() {
  QList<int> targets;
  targets.reserve(collections.size());
  for (int i = 0; i < collections.size(); ++i) {
    if (i != currentCollectionIndex) {
      targets.append(i);
    }
  }
  applyCurrentSettingsToIndices(targets, tr("all other collections"));
}

void SettingsDialog::applyCurrentSettingsToSubcollections() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= collections.size()) {
    QMessageBox::information(this, tr("Apply Settings"),
                             tr("Select a collection first to apply its "
                                "settings to its subcollections."));
    return;
  }
  const QList<int> descendants =
      CollectionUtils::collectDescendantIndices(currentCollectionIndex, collections);
  applyCurrentSettingsToIndices(descendants, tr("its subcollections"));
}

void SettingsDialog::applyCurrentSettingsToSelected() {
  // Kartend-f5i9: arbitrary-target multi-select picker. Complements the
  // existing all/subcollections paths (Kartend-63o) for users who want to
  // push appearance to a hand-picked subset.
  if (currentCollectionIndex < 0 || currentCollectionIndex >= collections.size()) {
    QMessageBox::information(this, tr("Apply Settings"),
                             tr("Select a source collection first, then use Apply to "
                                "copy its appearance and layout to chosen targets."));
    return;
  }
  if (collections.size() <= 1) {
    QMessageBox::information(this, tr("Apply Settings"),
                             tr("There are no other collections to apply settings to."));
    return;
  }

  CollectionPickerDialog picker(collections, currentCollectionIndex, {}, this);
  if (picker.exec() != QDialog::Accepted) {
    return;
  }
  const QList<int> targets = picker.selectedIndices();
  if (targets.isEmpty()) {
    QMessageBox::information(this, tr("Apply Settings"),
                             tr("No target collections were selected."));
    return;
  }
  applyCurrentSettingsToIndices(targets, tr("%n selected collection(s)", "", targets.size()));
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
  // currently sees, not the last-saved state. This mirrors how
  // applyCurrentSettingsToIndices works.
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
  // path. Mirrors the field subset of applyCurrentSettingsToIndices() but
  // skips the confirmation/summary dialogs because the user already opted
  // in by selecting a non-`Current` Settings Mode. Caller is responsible
  // for emitting collectionSaved() and refreshing the tree once.
  if (currentCollectionIndex < 0 || currentCollectionIndex >= collections.size()) {
    return 0;
  }
  const CollectionConfig source = collections[currentCollectionIndex];
  int applied = 0;
  for (int idx : targetIndices) {
    if (idx < 0 || idx >= collections.size()) {
      continue;
    }
    if (idx == currentCollectionIndex) {
      continue;
    }
    copyAppearanceAndLayoutFields(source, collections[idx]);
    if (idx < m_workingCollections.size()) {
      copyAppearanceAndLayoutFields(source, m_workingCollections[idx]);
    }
    ++applied;
  }
  return applied;
}
