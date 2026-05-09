// Multi-step collection-removal pipeline lifted out of
// settingsdialogremove.cpp. Reaches into SettingsDialog's collection list,
// tree-index maps, and several private helper methods via the friend
// declaration in settingsdialog.h.
#include "collectionremover.h"

#include "collectionutils.h"
#include "collectiontreewidget.h"
#include "mainwindow.h"
#include "settingsdialog.h"

#include <algorithm>
#include <functional>
#include <QMessageBox>
#include <QTreeWidgetItem>

CollectionRemover::CollectionRemover(SettingsDialog *host) : QObject(host), m_host(host) {}

bool CollectionRemover::validatePreconditions() {
  if (!m_host || !m_host->currentTreeItem ||
      !m_host->itemToCollectionIndex.contains(m_host->currentTreeItem)) {
    return false;
  }
  const int index = m_host->itemToCollectionIndex[m_host->currentTreeItem];
  if (!CollectionUtils::isValidIndex(index, m_host->collections)) {
    return false;
  }
  // Allow removing any collection, including the last one.
  return true;
}

QList<int> CollectionRemover::captureExpandedStates() {
  QList<int> expandedBefore;
  if (!m_host) {
    return expandedBefore;
  }
  for (auto it = m_host->collectionIndexToItem.begin();
       it != m_host->collectionIndexToItem.end(); ++it) {
    if (it.value() && it.value()->isExpanded()) {
      expandedBefore.append(it.key());
    }
  }
  return expandedBefore;
}

void CollectionRemover::performRemovalAt(int index) {
  if (!m_host) {
    return;
  }
  m_host->collections.removeAt(index);
  if (index >= 0 && index < m_host->m_workingCollections.size()) {
    m_host->m_workingCollections.removeAt(index);
  }
}

void CollectionRemover::rebuildParentIndices() {
  if (!m_host) {
    return;
  }
  // Parent indices should already be set correctly after removals — just
  // sync the working copy with the live collections.
  for (int i = 0; i < m_host->collections.size() && i < m_host->m_workingCollections.size(); ++i) {
    m_host->m_workingCollections[i].parentCollectionIndex =
        m_host->collections[i].parentCollectionIndex;
    m_host->m_workingCollections[i].isSubcollection = m_host->collections[i].isSubcollection;
  }
}

void CollectionRemover::restoreExpandedStates(const QList<int> &expandedBefore, int removedIndex) {
  if (!m_host) {
    return;
  }
  for (int expIdx : expandedBefore) {
    if (expIdx == removedIndex) {
      continue;
    }
    const int adjustedIdx = (expIdx > removedIndex) ? expIdx - 1 : expIdx;
    if (m_host->collectionIndexToItem.contains(adjustedIdx) &&
        m_host->collectionIndexToItem[adjustedIdx]) {
      m_host->collectionIndexToItem[adjustedIdx]->setExpanded(true);
    }
  }
}

void CollectionRemover::selectTargetAfter(int parentIdx, int removedIndex) {
  if (!m_host) {
    return;
  }
  if (parentIdx >= removedIndex) {
    parentIdx -= 1;
  }
  const int targetIndex =
      (parentIdx >= 0 && parentIdx < m_host->collections.size()) ? parentIdx : 0;
  m_host->currentCollectionIndex =
      m_host->collections.isEmpty() ? -1 : qBound(0, targetIndex, m_host->collections.size() - 1);

  if (m_host->currentCollectionIndex >= 0 &&
      m_host->collectionIndexToItem.contains(m_host->currentCollectionIndex)) {
    QTreeWidgetItem *item = m_host->collectionIndexToItem[m_host->currentCollectionIndex];
    m_host->collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
    m_host->expandPathToCollection(m_host->currentCollectionIndex);
    m_host->loadCollectionToUI(m_host->currentCollectionIndex);
    m_host->originalCollection = m_host->m_workingCollections[m_host->currentCollectionIndex];
  } else {
    m_host->originalCollection = CollectionConfig();
  }
}

void CollectionRemover::run() {
  if (!validatePreconditions()) {
    return;
  }

  const int index = m_host->itemToCollectionIndex[m_host->currentTreeItem];
  const int parentIdx = m_host->collections[index].parentCollectionIndex;

  // Check if this is a root collection with descendants — affects the
  // confirmation prompt copy.
  QList<int> descendants = CollectionUtils::collectDescendantIndices(index, m_host->collections);
  const bool isRootCollection = (parentIdx == -1);
  const bool hasDescendants = !descendants.isEmpty();

  QString message;
  if (isRootCollection && hasDescendants) {
    message = QString("Remove \"%1\" and all %2 nested collection(s)?\n\n"
                      "This action cannot be undone.")
                  .arg(m_host->collections[index].name)
                  .arg(descendants.size());
  } else if (hasDescendants) {
    message = QString("Remove \"%1\" and all %2 nested collection(s)?")
                  .arg(m_host->collections[index].name)
                  .arg(descendants.size());
  } else {
    message = QString("Remove \"%1\"?").arg(m_host->collections[index].name);
  }

  const QMessageBox::StandardButton reply = QMessageBox::question(
      m_host, "Remove Collection", message, QMessageBox::Yes | QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }

  const QList<int> expandedBefore = captureExpandedStates();

  // Capture every name about to disappear so we can scrub them out of
  // other collections' additionalParentNames *before* the index list
  // shifts under us. Empty newName tells propagateCollectionNameChange to
  // delete the entry.
  QStringList namesAboutToVanish;
  namesAboutToVanish.reserve(descendants.size() + 1);
  namesAboutToVanish.append(m_host->collections[index].name);
  for (int descIndex : descendants) {
    namesAboutToVanish.append(m_host->collections[descIndex].name);
  }

  // Remove descendants first (in reverse order to maintain indices).
  std::sort(descendants.begin(), descendants.end(), std::greater<int>());
  for (int descIndex : descendants) {
    performRemovalAt(descIndex);
  }

  // Recalculate the target's index after descendant removals.
  int adjustedIndex = index;
  for (int descIndex : descendants) {
    if (descIndex < index) {
      adjustedIndex--;
    }
  }
  performRemovalAt(adjustedIndex);

  // Update parent references — multiple removals happened, so any
  // collection whose parent was removed needs to be orphaned to root.
  for (int i = 0; i < m_host->collections.size(); ++i) {
    const int origParent = m_host->collections[i].parentCollectionIndex;
    if (origParent >= 0) {
      if (origParent == index || descendants.contains(origParent)) {
        m_host->collections[i].parentCollectionIndex = -1;
        m_host->m_workingCollections[i].parentCollectionIndex = -1;
        m_host->collections[i].isSubcollection = false;
        m_host->m_workingCollections[i].isSubcollection = false;
      }
    }
  }
  rebuildParentIndices();

  // Scrub any link references to the removed names so the cache doesn't
  // keep silently dropping them on every rebuild.
  for (const QString &removed : namesAboutToVanish) {
    m_host->propagateCollectionNameChange(removed, QString{});
  }

  // Persist the removal immediately.
  emit m_host->collectionSaved(m_host->collections);

  m_host->updateCollectionTreeWidget();

  // If all collections were removed, prompt for a new one.
  if (m_host->collections.isEmpty()) {
    m_host->clearCollectionUI();
    m_host->currentTreeItem = nullptr;
    m_host->currentCollectionIndex = -1;
    m_host->ensureRootCollectionExists();
    m_host->updateCollectionTreeWidget();
    if (!m_host->collections.isEmpty()) {
      m_host->currentCollectionIndex = 0;
      if (m_host->collectionIndexToItem.contains(0)) {
        m_host->currentTreeItem = m_host->collectionIndexToItem[0];
        m_host->collectionTreeWidget->setCurrentItem(m_host->currentTreeItem);
        m_host->currentTreeItem->setSelected(true);
      }
      m_host->loadCollectionToUI(0);
      m_host->originalCollection = m_host->m_workingCollections[0];
      emit m_host->collectionSaved(m_host->collections);
    }
  } else {
    restoreExpandedStates(expandedBefore, index);
    selectTargetAfter(parentIdx, adjustedIndex);
  }

  m_host->m_collectionSaved = true;
  m_host->updateSaveButtonStyle();
  m_host->updateDeleteButtonState();
}
