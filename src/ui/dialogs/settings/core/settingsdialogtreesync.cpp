// Kartend-uodi step: name-change + selection-sync slot bodies extracted from
// settingsdialogtree.cpp. Owns onTreeItemSelectionChanged + onTreeItemChanged
// and the thin propagateCollectionNameChange forwarder. Other tree
// concerns live in settingsdialogtree.cpp (delegators / combo),
// settingsdialogtreemutation.cpp (add/duplicate/copy), and
// settingsdialogtreedragdrop.cpp (drag-drop + context menu).
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "collection/collectionconfig.h"
#include "collection/helpers.h"
#include "collectiontreewidget.h"
#include "settingsdialog.h"
#include "treemanager.h"

void SettingsDialog::propagateCollectionNameChange(const QString &oldName, const QString &newName) {
  if (m_treeManager) {
    m_treeManager->propagateNameChange(oldName, newName);
  }
}

// Handles selection changes; supports deselection state
void SettingsDialog::onTreeItemSelectionChanged() {
  QList<QTreeWidgetItem *> selectedItems = collectionTreeWidget->selectedItems();
  if (selectedItems.isEmpty()) {
    currentTreeItem = nullptr;
    currentCollectionIndex = -1;
    updateDeleteButtonState();
    updateContextHeader();
    return;
  }

  QTreeWidgetItem *item = selectedItems.first();
  if (!m_treeManager || !m_treeManager->contains(item)) {
    return;
  }

  int newIndex = m_treeManager->indexOf(item);
  if (newIndex == currentCollectionIndex) {
    return;
  }

  const int previousIndex = currentCollectionIndex;
  if (previousIndex >= 0 && previousIndex < m_workingCollections.size() &&
      !resolveUnsavedChanges(tr("switching collections"), true)) {
    if (auto *previousItem = m_treeManager->itemAt(previousIndex)) {
      QSignalBlocker blocker(collectionTreeWidget);
      collectionTreeWidget->setCurrentItem(previousItem);
      previousItem->setSelected(true);
    }
    return;
  }

  if (auto *primary = m_treeManager->itemAt(newIndex)) {
    item = primary;
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
  if (column != 0 || !m_treeManager || !m_treeManager->contains(item)) {
    return;
  }
  // rename only fires for the canonical row. Linked mirrors are flagged
  // read-only via ItemIsEditable, but defensively skip if a signal sneaks in.
  if (item->data(0, Qt::UserRole).toBool()) {
    return;
  }
  int collectionIndex = m_treeManager->indexOf(item);
  if (!CollectionUtils::isValidIndex(collectionIndex, &collections) ||
      !CollectionUtils::isValidIndex(collectionIndex, &m_workingCollections)) {
    return;
  }

  QString newName = item->text(0);
  const QString oldName = collections[collectionIndex].name;

  // Reject a blank / whitespace-only rename: a nameless collection has no
  // valid hierarchical section to persist under and resurfaces as a ghost
  // row. Revert the tree text to the prior name; block signals on the
  // revert so this handler doesn't re-enter.
  if (newName.trimmed().isEmpty()) {
    QSignalBlocker blocker(item->treeWidget());
    item->setText(0, oldName);
    return;
  }

  if (newName != oldName) {
    collections[collectionIndex].name = newName;
    m_workingCollections[collectionIndex].name = newName;

    // rename in-place propagates to linked mirrors so they don't drift,
    // and rewrites references in other collections' additionalParentNames
    // so links survive the rename.
    for (QTreeWidgetItem *linked : m_treeManager->linkedItemsFor(collectionIndex)) {
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
