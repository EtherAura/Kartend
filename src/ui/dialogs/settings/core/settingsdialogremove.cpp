// Sibling translation unit for SettingsDialog: collection removal.
// The seven-step pipeline + the orchestrator that strings them together
// live on CollectionRemover; this TU forwards the public slot the
// dialog's button connect uses and implements the CollectionRemoverHost
// selection/expansion adapters by delegating to TreeManager for the
// tree-state queries.
#include "collection/validationhelpers.h"
#include "collectionremover.h"
#include "collectiontreewidget.h"
#include "settingsdialog.h"
#include "treemanager.h"

#include <QTreeWidgetItem>

void SettingsDialog::removeCollection() {
  if (m_collectionRemover) {
    m_collectionRemover->run();
  }
}

// ── CollectionRemoverHost: selection adapters ────────────────────────────

int SettingsDialog::selectedCollectionIndex() const {
  if (!currentTreeItem || !m_treeManager || !m_treeManager->contains(currentTreeItem)) {
    return -1;
  }
  return m_treeManager->indexOf(currentTreeItem);
}

bool SettingsDialog::hasSelection() const {
  if (!currentTreeItem || !m_treeManager || !m_treeManager->contains(currentTreeItem)) {
    return false;
  }
  return CollectionUtils::isValidIndex(m_treeManager->indexOf(currentTreeItem), &collections);
}

void SettingsDialog::selectCollection(int index) {
  if (index < 0) {
    currentTreeItem = nullptr;
    currentCollectionIndex = -1;
    return;
  }
  QTreeWidgetItem *item = m_treeManager ? m_treeManager->itemAt(index) : nullptr;
  if (!item) {
    currentCollectionIndex = index;
    return;
  }
  currentTreeItem = item;
  currentCollectionIndex = index;
  if (collectionTreeWidget) {
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
  if (!m_treeManager) {
    return result;
  }
  const auto &indexToItem = m_treeManager->indexToItem();
  for (auto it = indexToItem.begin(); it != indexToItem.end(); ++it) {
    if (it.value() && it.value()->isExpanded()) {
      result.append(it.key());
    }
  }
  return result;
}

void SettingsDialog::expandCollectionAtIndex(int index) {
  if (auto *item = m_treeManager ? m_treeManager->itemAt(index) : nullptr) {
    item->setExpanded(true);
  }
}
