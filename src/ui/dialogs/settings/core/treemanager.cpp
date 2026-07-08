// Tree-population state and operations lifted out of settingsdialogtree.cpp.
// Non-owning references to the dialog's CollectionTreeWidget and the two
// collection lists; assumes those outlive the manager. Slots that wire
// tree gestures back into dialog UI stay on SettingsDialog because they
// have non-tree side effects.
#include "treemanager.h"
#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h" // CollectionUtils::wouldCreateCircularReference
#include "collection/validationhelpers.h"
#include "settingsdialogtreehelpers.h"
#include "settingstreehost.h"

#include "collectiontreewidget.h"

#include <functional>

#include <QAbstractItemView>
#include <QAction>
#include <QFont>
#include <QMenu>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace {
const QList<QTreeWidgetItem *> kEmptyLinkedItems;
} // namespace

TreeManager::TreeManager(CollectionTreeWidget *widget, QList<CollectionConfig> *collections,
                         QList<CollectionConfig> *workingCollections, QObject *parent)
    : QObject(parent), m_widget(widget), m_collections(collections),
      m_workingCollections(workingCollections) {}

void TreeManager::attachWidget() {
  if (!m_widget) {
    return;
  }
  m_widget->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
  // right-click context menu surfaces Rename/Duplicate/Delete + expand/collapse
  // helpers where the pointer already is.
  m_widget->setContextMenuPolicy(Qt::CustomContextMenu);

  // Drag-drop reparenting: the promoted widget delegates cycle validation +
  // item→index resolution to this controller's own state and emits
  // treeRearranged() on success.
  m_widget->setCycleCheck([this](int childIndex, int parentIndex) {
    return wouldCreateCircularReference(childIndex, parentIndex);
  });
  m_widget->setItemToIndex(
      [this](const QTreeWidgetItem *item) { return indexOf(const_cast<QTreeWidgetItem *>(item)); });

  // Kartend-mnymg: the controller now owns the gesture handlers, so connect the
  // widget's raw signals straight to its own slots.
  connect(m_widget, &QTreeWidget::itemSelectionChanged, this,
          &TreeManager::onWidgetSelectionChanged);
  connect(m_widget, &QTreeWidget::itemChanged, this, &TreeManager::onWidgetItemChanged);
  connect(m_widget, &QWidget::customContextMenuRequested, this,
          &TreeManager::onWidgetContextMenuRequested);
  connect(m_widget, &CollectionTreeWidget::treeRearranged, this, &TreeManager::onWidgetRearranged);
}

void TreeManager::setSelectionState(int *currentCollectionIndex, QTreeWidgetItem **currentTreeItem,
                                    CollectionConfig *originalCollection, bool *collectionSaved) {
  m_currentCollectionIndex = currentCollectionIndex;
  m_currentTreeItem = currentTreeItem;
  m_originalCollection = originalCollection;
  m_collectionSaved = collectionSaved;
}

bool TreeManager::wouldCreateCircularReference(int childIndex, int potentialParentIndex) const {
  if (!m_collections) {
    return false;
  }
  return CollectionUtils::wouldCreateCircularReference(childIndex, potentialParentIndex,
                                                       *m_collections);
}

void TreeManager::rebuild() {
  if (!m_widget) {
    return;
  }
  // clear() fires itemSelectionChanged when rows were selected; flag the
  // programmatic clear so the deselect gate in onWidgetSelectionChanged
  // stays out of the way (see m_rebuilding).
  m_rebuilding = true;
  m_widget->clear();
  m_rebuilding = false;
  m_itemToIndex.clear();
  m_indexToItem.clear();
  m_indexToLinkedItems.clear();
  populate();
  populateLinkedAppearances();
}

void TreeManager::expandPathTo(int collectionIndex) {
  if (!m_collections || !CollectionUtils::isValidIndex(collectionIndex, m_collections)) {
    return;
  }
  if (!m_indexToItem.contains(collectionIndex)) {
    return;
  }

  QList<int> pathIndices;
  int currentIndex = collectionIndex;
  while (CollectionUtils::isValidIndex(currentIndex, m_collections)) {
    pathIndices.prepend(currentIndex);
    const CollectionConfig &config = (*m_collections)[currentIndex];
    currentIndex = config.parentCollectionIndex;
  }
  for (int i = 0; i < pathIndices.size() - 1; ++i) {
    int index = pathIndices[i];
    if (QTreeWidgetItem *item = m_indexToItem.value(index)) {
      item->setExpanded(true);
    }
  }
}

void TreeManager::propagateNameChange(const QString &oldName, const QString &newName) {
  if (!m_collections) {
    return;
  }
  SettingsTreeHelpers::propagateNameChange(*m_collections, m_workingCollections, oldName, newName);
}

void TreeManager::setSubtreeExpanded(QTreeWidgetItem *item, bool expanded) {
  if (!item) {
    return;
  }
  item->setExpanded(expanded);
  for (int i = 0; i < item->childCount(); ++i) {
    setSubtreeExpanded(item->child(i), expanded);
  }
}

const QList<QTreeWidgetItem *> &TreeManager::linkedItemsFor(int index) const {
  auto it = m_indexToLinkedItems.constFind(index);
  return (it != m_indexToLinkedItems.constEnd()) ? it.value() : kEmptyLinkedItems;
}

QTreeWidgetItem *TreeManager::itemAt(int collectionIndex) const {
  return m_indexToItem.value(collectionIndex, nullptr);
}

int TreeManager::indexOf(QTreeWidgetItem *item) const {
  return m_itemToIndex.value(item, -1);
}

bool TreeManager::contains(QTreeWidgetItem *item) const {
  return m_itemToIndex.contains(item);
}

QList<QTreeWidgetItem *> TreeManager::linkedItems(int index) const {
  return m_indexToLinkedItems.value(index);
}

void TreeManager::populate() {
  if (!m_collections) {
    return;
  }
  auto &live = *m_collections;
  for (int i = 0; i < live.size(); ++i) {
    if (live[i].parentCollectionIndex == -1) {
      createItem(i);
    }
  }

  bool foundSubcollection = true;
  int maxIterations = live.size();
  int iteration = 0;

  while (foundSubcollection && iteration < maxIterations) {
    foundSubcollection = false;
    iteration++;

    for (int i = 0; i < live.size(); ++i) {
      if (m_indexToItem.contains(i)) {
        continue;
      }
      int parentIndex = live[i].parentCollectionIndex;
      if (parentIndex >= 0 && parentIndex < live.size()) {
        if (m_indexToItem.contains(parentIndex)) {
          QTreeWidgetItem *parentItem = m_indexToItem.value(parentIndex);
          createItem(i, parentItem);
          foundSubcollection = true;
        }
      }
    }
  }

  // Any collection not yet placed has a stale parent — orphan to root and
  // create a top-level row so the tree always covers every collection.
  for (int i = 0; i < live.size(); ++i) {
    if (!m_indexToItem.contains(i)) {
      live[i].parentCollectionIndex = -1;
      live[i].isSubcollection = false;
      createItem(i);
    }
  }
}

QTreeWidgetItem *TreeManager::createItem(int collectionIndex, QTreeWidgetItem *parent) {
  if (!m_collections) {
    return nullptr;
  }
  QTreeWidgetItem *item = (parent) ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_widget);
  item->setText(0, (*m_collections)[collectionIndex].name);
  item->setFlags(item->flags() | Qt::ItemIsEditable);
  // Qt::UserRole flag distinguishes the canonical row (false) from
  // linked-appearance mirrors (true). Most slots branch on this flag.
  item->setData(0, Qt::UserRole, false);
  m_itemToIndex[item] = collectionIndex;
  m_indexToItem[collectionIndex] = item;
  return item;
}

void TreeManager::populateLinkedAppearances() {
  if (!m_collections) {
    return;
  }
  auto &live = *m_collections;
  // Build a name → index map once so each link resolves in O(1).
  QHash<QString, int> nameToIndex;
  nameToIndex.reserve(live.size());
  for (int i = 0; i < live.size(); ++i) {
    nameToIndex.insert(live[i].name, i);
  }

  for (int i = 0; i < live.size(); ++i) {
    const CollectionConfig &c = live[i];
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
      QTreeWidgetItem *parentItem = m_indexToItem.value(parentIdx);
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
          (c.parentCollectionIndex >= 0 && c.parentCollectionIndex < live.size())
              ? live[c.parentCollectionIndex].name
              : QObject::tr("(root)");
      linkedItem->setToolTip(
          0, QObject::tr("Linked appearance — primary parent: %1").arg(primaryName));
      // Strip edit/drag/drop — linked items are read-only mirrors.
      Qt::ItemFlags flags = linkedItem->flags();
      flags &= ~(Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
      linkedItem->setFlags(flags);
      linkedItem->setData(0, Qt::UserRole, true);

      m_itemToIndex[linkedItem] = i;
      m_indexToLinkedItems[i].append(linkedItem);
      alreadyMirrored.insert(parentIdx);
    }
  }
}

// ── Tree gesture handlers (Kartend-mnymg: migrated from SettingsDialog) ───────
// Bodies are the former SettingsDialog::onTree* slots, transformed to operate on
// the borrowed selection state (m_currentCollectionIndex / m_currentTreeItem /
// m_originalCollection / m_collectionSaved) + collection lists, routing
// dialog-side effects through m_host. Behavior is preserved verbatim.

void TreeManager::onWidgetSelectionChanged() {
  if (!m_widget || !m_host || !m_currentCollectionIndex || !m_currentTreeItem ||
      !m_originalCollection || !m_collectionSaved || !m_workingCollections) {
    return;
  }
  QList<QTreeWidgetItem *> selectedItems = m_widget->selectedItems();
  if (selectedItems.isEmpty()) {
    // A blank-area click clears the selection. Run the same unsaved-changes
    // gate a collection→collection switch uses: once the index drops to -1,
    // hasUnsavedChanges() short-circuits false and the pending form edits
    // become silently unrecoverable (saveCollectionFromUI is the only path
    // into m_workingCollections).
    const int previousIndex = *m_currentCollectionIndex;
    // Skip the gate for programmatic clears: a rebuild() or an in-flight
    // drag-drop move empties the selection transiently, manages its own save
    // state, and may hold item pointers a prompt-triggered save would delete.
    if (!m_rebuilding && !m_widget->dropInProgress() && previousIndex >= 0 &&
        previousIndex < m_workingCollections->size()) {
      if (!m_host->resolveUnsavedChanges(tr("deselecting"), true)) {
        // Cancel: restore the previous row with the widget's signals blocked
        // so the programmatic re-selection can't re-enter this handler and
        // re-prompt — same shape as the switch guard below.
        if (auto *previousItem = itemAt(previousIndex)) {
          QSignalBlocker blocker(m_widget);
          m_widget->setCurrentItem(previousItem);
          previousItem->setSelected(true);
        }
        return;
      }
      // Save rebuilds the tree and re-selects the saved row (refreshTree in
      // handleSaveCollection). The user asked to deselect, so clear that
      // restored selection — blocked, or the clear re-enters this handler.
      if (!m_widget->selectedItems().isEmpty()) {
        QSignalBlocker blocker(m_widget);
        m_widget->clearSelection();
        m_widget->setCurrentItem(nullptr);
      }
    }
    *m_currentTreeItem = nullptr;
    *m_currentCollectionIndex = -1;
    m_host->updateDeleteButtonState();
    m_host->updateContextHeader();
    return;
  }

  QTreeWidgetItem *item = selectedItems.first();
  if (!contains(item)) {
    return;
  }

  int newIndex = indexOf(item);
  if (newIndex == *m_currentCollectionIndex) {
    return;
  }

  const int previousIndex = *m_currentCollectionIndex;
  if (previousIndex >= 0 && previousIndex < m_workingCollections->size() &&
      !m_host->resolveUnsavedChanges(tr("switching collections"), true)) {
    if (auto *previousItem = itemAt(previousIndex)) {
      QSignalBlocker blocker(m_widget);
      m_widget->setCurrentItem(previousItem);
      previousItem->setSelected(true);
    }
    return;
  }

  if (auto *primary = itemAt(newIndex)) {
    item = primary;
  }

  if (m_widget && item) {
    QSignalBlocker blocker(m_widget);
    m_widget->setCurrentItem(item);
    item->setSelected(true);
  }

  *m_currentCollectionIndex = newIndex;
  *m_currentTreeItem = item;
  if (newIndex >= 0 && newIndex < m_workingCollections->size()) {
    *m_originalCollection = (*m_workingCollections)[newIndex];
  } else {
    *m_originalCollection = CollectionConfig();
  }
  m_host->loadCollectionToUI(newIndex);
  *m_collectionSaved = true;
  m_host->updateSaveButtonStyle();
  m_host->updateDeleteButtonState();
}

void TreeManager::onWidgetItemChanged(QTreeWidgetItem *item, int column) {
  if (column != 0 || !contains(item) || !m_collections || !m_workingCollections || !m_host ||
      !m_currentCollectionIndex || !m_originalCollection || !m_collectionSaved) {
    return;
  }
  // rename only fires for the canonical row. Linked mirrors are flagged
  // read-only via ItemIsEditable, but defensively skip if a signal sneaks in.
  if (item->data(0, Qt::UserRole).toBool()) {
    return;
  }
  int collectionIndex = indexOf(item);
  if (!CollectionUtils::isValidIndex(collectionIndex, m_collections) ||
      !CollectionUtils::isValidIndex(collectionIndex, m_workingCollections)) {
    return;
  }

  QString newName = item->text(0);
  const QString oldName = (*m_collections)[collectionIndex].name;

  // Reject a blank / whitespace-only rename; revert the tree text with signals
  // blocked so this handler doesn't re-enter.
  if (newName.trimmed().isEmpty()) {
    QSignalBlocker blocker(item->treeWidget());
    item->setText(0, oldName);
    return;
  }

  if (newName != oldName) {
    (*m_collections)[collectionIndex].name = newName;
    (*m_workingCollections)[collectionIndex].name = newName;

    for (QTreeWidgetItem *linked : linkedItemsFor(collectionIndex)) {
      if (linked) {
        linked->setText(0, newName);
      }
    }
    propagateNameChange(oldName, newName);

    bool revertedToOriginal =
        (collectionIndex == *m_currentCollectionIndex && newName == m_originalCollection->name);
    *m_collectionSaved = revertedToOriginal && !m_host->hasUnsavedChanges();
    if (!revertedToOriginal) {
      *m_collectionSaved = false;
    }

    m_host->updateSaveButtonStyle();
  }
}

void TreeManager::onWidgetContextMenuRequested(const QPoint &pos) {
  if (!m_widget || !m_host || !m_currentTreeItem) {
    return;
  }
  QTreeWidgetItem *target = m_widget->itemAt(pos);

  QMenu menu(m_widget);
  const bool isLinked = target && target->data(0, Qt::UserRole).toBool();
  if (target && !isLinked) {
    QAction *renameAction = menu.addAction(tr("Rename"));
    QAction *duplicateAction = menu.addAction(tr("Duplicate..."));
    QAction *deleteAction = menu.addAction(tr("Delete"));
    menu.addSeparator();
    QAction *copyFromAction = menu.addAction(tr("Copy Settings From..."));
    menu.addSeparator();
    QAction *expandSubAction = menu.addAction(tr("Expand subtree"));
    QAction *collapseSubAction = menu.addAction(tr("Collapse subtree"));
    const bool hasChildren = target->childCount() > 0;
    expandSubAction->setEnabled(hasChildren);
    collapseSubAction->setEnabled(hasChildren);
    menu.addSeparator();
    QAction *expandAllAction = menu.addAction(tr("Expand all"));
    QAction *collapseAllAction = menu.addAction(tr("Collapse all"));

    // Select the row first so the form/save state matches the action; if the
    // user cancels the resulting unsaved-changes prompt the selection reverts
    // and we abort so we never operate on the wrong collection.
    auto switchToTarget = [this, target]() -> bool {
      if (*m_currentTreeItem == target) {
        return true;
      }
      m_widget->setCurrentItem(target);
      return *m_currentTreeItem == target;
    };
    connect(renameAction, &QAction::triggered, this, [this, target, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      m_widget->editItem(target, 0);
    });
    connect(duplicateAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      m_host->duplicateCollection();
    });
    connect(deleteAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      m_host->removeCollection();
    });
    connect(copyFromAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      m_host->copySettingsFromOtherCollection();
    });
    connect(expandSubAction, &QAction::triggered, this,
            [this, target]() { setSubtreeExpanded(target, true); });
    connect(collapseSubAction, &QAction::triggered, this,
            [this, target]() { setSubtreeExpanded(target, false); });
    connect(expandAllAction, &QAction::triggered, m_widget, &QTreeWidget::expandAll);
    connect(collapseAllAction, &QAction::triggered, m_widget, &QTreeWidget::collapseAll);
  } else {
    QAction *expandAllAction = menu.addAction(tr("Expand all"));
    QAction *collapseAllAction = menu.addAction(tr("Collapse all"));
    connect(expandAllAction, &QAction::triggered, m_widget, &QTreeWidget::expandAll);
    connect(collapseAllAction, &QAction::triggered, m_widget, &QTreeWidget::collapseAll);
  }

  menu.exec(m_widget->viewport()->mapToGlobal(pos));
}

void TreeManager::onWidgetRearranged() {
  if (!m_widget || !m_collections || !m_workingCollections || !m_collectionSaved || !m_host) {
    return;
  }
  // Walk the post-drop tree and resync parentCollectionIndex / isSubcollection
  // on every collection. Linked mirrors aren't draggable but can shuffle along
  // with their primary parent, so the walk (extracted for unit coverage) skips
  // them when resolving the canonical parent and recurses through them so
  // their children are still visited.
  SettingsTreeHelpers::resyncParentIndicesFromTree(
      m_widget,
      [this](const QTreeWidgetItem *item) { return indexOf(const_cast<QTreeWidgetItem *>(item)); },
      *m_collections, *m_workingCollections);

  // Persist immediately so the rearranged tree survives a Cancel of the outer
  // dialog; the host re-emits SettingsDialog::collectionSaved.
  emit collectionsReordered();
  *m_collectionSaved = true;
  m_host->updateSaveButtonStyle();
}
