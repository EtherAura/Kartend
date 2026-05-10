// Sibling translation unit for SettingsDialog: collection removal.
// The seven-step pipeline + the orchestrator that strings them together
// live on CollectionRemover; this TU forwards the public slot the
// dialog's button connect uses and implements the small set of
// CollectionRemoverHost selection/expansion adapters that don't already
// have a same-named SettingsDialog method.
#include "collectionremover.h"
#include "collectiontreewidget.h"
#include "settingsdialog.h"

#include <QTreeWidgetItem>

void SettingsDialog::removeCollection() {
  if (m_collectionRemover) {
    m_collectionRemover->run();
  }
}

// ── CollectionRemoverHost: selection adapters ────────────────────────────

int SettingsDialog::selectedCollectionIndex() const {
  if (!currentTreeItem || !itemToCollectionIndex.contains(currentTreeItem)) {
    return -1;
  }
  return itemToCollectionIndex.value(currentTreeItem);
}

bool SettingsDialog::hasSelection() const {
  if (!currentTreeItem || !itemToCollectionIndex.contains(currentTreeItem)) {
    return false;
  }
  const int index = itemToCollectionIndex.value(currentTreeItem);
  return CollectionUtils::isValidIndex(index, &collections);
}

void SettingsDialog::selectCollection(int index) {
  if (index < 0) {
    currentTreeItem = nullptr;
    currentCollectionIndex = -1;
    return;
  }
  if (!collectionIndexToItem.contains(index)) {
    currentCollectionIndex = index;
    return;
  }
  QTreeWidgetItem *item = collectionIndexToItem.value(index);
  currentTreeItem = item;
  currentCollectionIndex = index;
  if (collectionTreeWidget && item) {
    collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
  }
}

void SettingsDialog::clearSelection() {
  currentTreeItem = nullptr;
  currentCollectionIndex = -1;
}

// ── CollectionRemoverHost: expansion adapters ────────────────────────────

QList<int> SettingsDialog::expandedCollectionIndices() const {
  QList<int> result;
  for (auto it = collectionIndexToItem.begin(); it != collectionIndexToItem.end(); ++it) {
    if (it.value() && it.value()->isExpanded()) {
      result.append(it.key());
    }
  }
  return result;
}

void SettingsDialog::expandCollectionAtIndex(int index) {
  QTreeWidgetItem *item = collectionIndexToItem.value(index, nullptr);
  if (item) {
    item->setExpanded(true);
  }
}
