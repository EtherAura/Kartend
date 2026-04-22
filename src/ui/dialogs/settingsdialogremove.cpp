// Sibling translation unit for SettingsDialog: collection removal cluster.
// Extracted from settingsdialogtree.cpp during LOC-reduction refactor.
#include <QInputDialog>
#include <QList>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <functional>

#include "mainwindow.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

// Validates preconditions for collection removal
auto SettingsDialog::validateRemovalPreconditions() -> bool {
  if ((!currentTreeItem) || !itemToCollectionIndex.contains(currentTreeItem)) {
    return false;
  }
  int index = itemToCollectionIndex[currentTreeItem];
  if (!CollectionUtils::isValidIndex(index, collections)) {
    return false;
  }
  // Allow removing any collection, including the last one
  return true;
}

// Captures currently expanded tree states before removal
auto SettingsDialog::captureExpandedStates() -> QList<int> {
  QList<int> expandedBefore;
  for (auto it = collectionIndexToItem.begin();
       it != collectionIndexToItem.end(); ++it) {
    if ((it.value()) && it.value()->isExpanded()) {
      expandedBefore.append(it.key());
    }
  }
  return expandedBefore;
}

// Performs the actual collection removal from data structures
auto SettingsDialog::performCollectionRemoval(int index) -> void {
  collections.removeAt(index);
  if (index >= 0 && index < m_workingCollections.size()) {
    m_workingCollections.removeAt(index);
  }
}

// Updates parent references after collection removal
auto SettingsDialog::updateParentReferences(int removedIndex) -> void {
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex > removedIndex) {
      collections[i].parentCollectionIndex -= 1;
      m_workingCollections[i].parentCollectionIndex =
          collections[i].parentCollectionIndex;
    } else if (collections[i].parentCollectionIndex == removedIndex) {
      collections[i].parentCollectionIndex = -1;
      m_workingCollections[i].parentCollectionIndex = -1;
      collections[i].isSubcollection = false;
      m_workingCollections[i].isSubcollection = false;
    }
  }
}

// Rebuilds parent indices after multiple removals to ensure consistency
auto SettingsDialog::rebuildParentIndices() -> void {
  // Parent indices should already be set correctly after removals
  // Just sync working collections with main collections
  for (int i = 0; i < collections.size() && i < m_workingCollections.size();
       ++i) {
    m_workingCollections[i].parentCollectionIndex =
        collections[i].parentCollectionIndex;
    m_workingCollections[i].isSubcollection = collections[i].isSubcollection;
  }
}

// Restores expanded states after tree rebuild, adjusting for removed index
auto SettingsDialog::restoreExpandedStates(const QList<int> &expandedBefore,
                                           int removedIndex) -> void {
  for (int expIdx : expandedBefore) {
    if (expIdx == removedIndex) {
      continue;
    }
    int adjustedIdx = (expIdx > removedIndex) ? expIdx - 1 : expIdx;
    if (collectionIndexToItem.contains(adjustedIdx) &&
        (collectionIndexToItem[adjustedIdx])) {
      collectionIndexToItem[adjustedIdx]->setExpanded(true);
    }
  }
}

// Selects appropriate target collection after removal
auto SettingsDialog::selectTargetAfterRemoval(int parentIdx, int removedIndex)
    -> void {
  if (parentIdx >= removedIndex) {
    parentIdx -= 1;
  }
  int targetIndex =
      (parentIdx >= 0 && parentIdx < collections.size()) ? parentIdx : 0;
  currentCollectionIndex = (collections.isEmpty())
                               ? -1
                               : qBound(0, targetIndex, collections.size() - 1);

  if (currentCollectionIndex >= 0 &&
      collectionIndexToItem.contains(currentCollectionIndex)) {
    collectionTreeWidget->setCurrentItem(
        collectionIndexToItem[currentCollectionIndex]);
    collectionIndexToItem[currentCollectionIndex]->setSelected(true);
    expandPathToCollection(currentCollectionIndex);
    loadCollectionToUI(currentCollectionIndex);
    originalCollection = m_workingCollections[currentCollectionIndex];
  } else {
    originalCollection = CollectionConfig();
  }
}

// Extracts all UI field values into a collection config
void SettingsDialog::removeCollection() {
  if (!validateRemovalPreconditions()) {
    return;
  }

  int index = itemToCollectionIndex[currentTreeItem];
  int parentIdx = collections[index].parentCollectionIndex;

  // Check if this is a root collection with descendants
  QList<int> descendants =
      CollectionUtils::collectDescendantIndices(index, collections);
  bool isRootCollection = (parentIdx == -1);
  bool hasDescendants = !descendants.isEmpty();

  QString message;
  if (isRootCollection && hasDescendants) {
    message = QString("Remove \"%1\" and all %2 nested collection(s)?\n\n"
                      "This action cannot be undone.")
                  .arg(collections[index].name)
                  .arg(descendants.size());
  } else if (hasDescendants) {
    message = QString("Remove \"%1\" and all %2 nested collection(s)?")
                  .arg(collections[index].name)
                  .arg(descendants.size());
  } else {
    message = QString("Remove \"%1\"?").arg(collections[index].name);
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Remove Collection", message, QMessageBox::Yes | QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }

  QList<int> expandedBefore = captureExpandedStates();

  // Remove descendants first (in reverse order to maintain indices)
  // Sort descendants in descending order so removal doesn't affect other
  // indices
  std::sort(descendants.begin(), descendants.end(), std::greater<int>());
  for (int descIndex : descendants) {
    performCollectionRemoval(descIndex);
  }

  // Recalculate index after descendant removals
  // Count how many descendants were before the target index
  int adjustedIndex = index;
  for (int descIndex : descendants) {
    if (descIndex < index) {
      adjustedIndex--;
    }
  }

  performCollectionRemoval(adjustedIndex);

  // Update parent references - need to rebuild since multiple removals happened
  for (int i = 0; i < collections.size(); ++i) {
    // Find the original parent and update if needed
    int origParent = collections[i].parentCollectionIndex;
    if (origParent >= 0) {
      // Check if parent was removed or shifted
      if (origParent == index || descendants.contains(origParent)) {
        // Parent was removed - orphan this collection to root
        collections[i].parentCollectionIndex = -1;
        m_workingCollections[i].parentCollectionIndex = -1;
        collections[i].isSubcollection = false;
        m_workingCollections[i].isSubcollection = false;
      }
    }
  }
  // Rebuild parent indices to be consistent
  rebuildParentIndices();

  // Persist the removal immediately
  emit collectionSaved(collections);

  updateCollectionTreeWidget();

  // If all collections were removed, prompt for a new one
  if (collections.isEmpty()) {
    clearCollectionUI();
    currentTreeItem = nullptr;
    currentCollectionIndex = -1;
    ensureRootCollectionExists();
    updateCollectionTreeWidget();
    if (!collections.isEmpty()) {
      currentCollectionIndex = 0;
      if (collectionIndexToItem.contains(0)) {
        currentTreeItem = collectionIndexToItem[0];
        collectionTreeWidget->setCurrentItem(currentTreeItem);
        currentTreeItem->setSelected(true);
      }
      loadCollectionToUI(0);
      originalCollection = m_workingCollections[0];
      emit collectionSaved(collections);
    }
  } else {
    restoreExpandedStates(expandedBefore, index);
    selectTargetAfterRemoval(parentIdx, adjustedIndex);
  }

  m_collectionSaved = true;
  updateSaveButtonStyle();
  updateDeleteButtonState();
}

// Saves current collection UI edits (including name) into working and live
// collections
