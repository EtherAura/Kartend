// Tree-population state and operations lifted out of settingsdialogtree.cpp.
// Non-owning references to the dialog's CollectionTreeWidget and the two
// collection lists; assumes those outlive the manager. Slots that wire
// tree gestures back into dialog UI stay on SettingsDialog because they
// have non-tree side effects.
#include "treemanager.h"

#include "collectiontreewidget.h"

#include <QFont>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace {
const QList<QTreeWidgetItem *> kEmptyLinkedItems;
} // namespace

TreeManager::TreeManager(CollectionTreeWidget *widget, QList<CollectionConfig> *collections,
                         QList<CollectionConfig> *workingCollections)
    : m_widget(widget), m_collections(collections), m_workingCollections(workingCollections) {}

void TreeManager::rebuild() {
  if (!m_widget) {
    return;
  }
  m_widget->clear();
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
  if (oldName == newName || !m_collections) {
    return;
  }
  auto &live = *m_collections;
  for (int i = 0; i < live.size(); ++i) {
    QStringList &names = live[i].additionalParentNames;
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
    if (m_workingCollections && i < m_workingCollections->size()) {
      (*m_workingCollections)[i].additionalParentNames = names;
    }
  }
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
