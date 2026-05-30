#include "scraperesultselectionmodel.h"

#include "scraperesultdialog.h"

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/collectioncontext.h"
#include "idatabasemanager.h"

#include <functional>
#include <limits>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

ScrapeResultSelectionModel::ScrapeResultSelectionModel(ScrapeResultDialog *dlg)
    : QObject(dlg), m_dlg(dlg) {}

void ScrapeResultSelectionModel::populateCollectionTree() {
  m_dlg->m_collectionTree->clear();
  m_treeItemToCollectionIndex.clear();
  if (!m_dlg->m_scraperCtx.collections) return;
  const auto &cols = *m_dlg->m_scraperCtx.collections;

  // Build a parent-aware QTreeWidget mirroring the collection
  // hierarchy via CollectionConfig::parentCollectionIndex (root rows
  // are pi == -1). Multi-pass placement: keep iterating until every
  // collection has been parented, since the source list isn't sorted
  // topologically. Bounded by depth; orphans (out-of-range parent)
  // get re-rooted as a defensive last pass.
  QHash<int, QTreeWidgetItem *> itemByIndex;
  QSignalBlocker b(m_dlg->m_collectionTree);
  int remaining = cols.size();
  while (remaining > 0) {
    bool progress = false;
    for (int i = 0; i < cols.size(); ++i) {
      if (itemByIndex.contains(i)) continue;
      const int pi = cols[i].parentCollectionIndex;
      QTreeWidgetItem *item = nullptr;
      if (pi < 0) {
        item = new QTreeWidgetItem(m_dlg->m_collectionTree);
      } else if (itemByIndex.contains(pi)) {
        item = new QTreeWidgetItem(itemByIndex.value(pi));
      } else {
        continue; // parent not placed yet — try again next pass
      }
      item->setText(0, cols[i].name);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(0, Qt::Unchecked);
      itemByIndex.insert(i, item);
      m_treeItemToCollectionIndex.insert(item, i);
      --remaining;
      progress = true;
    }
    if (!progress) break; // safety: orphan / cycle — bail to the rescue loop.
  }
  for (int i = 0; i < cols.size(); ++i) {
    if (itemByIndex.contains(i)) continue;
    auto *item = new QTreeWidgetItem(m_dlg->m_collectionTree);
    item->setText(0, cols[i].name);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, Qt::Unchecked);
    m_treeItemToCollectionIndex.insert(item, i);
  }
  m_dlg->m_collectionTree->expandAll();
}

void ScrapeResultSelectionModel::onCollectionTreeCurrentChanged(QTreeWidgetItem *current,
                                                                QTreeWidgetItem *) {
  if (!current) {
    m_dlg->m_itemsHeaderLabel->setText(tr("Select a collection to see its items."));
    m_dlg->m_unifiedItemsList->clear();
    return;
  }
  const int idx = m_treeItemToCollectionIndex.value(current, -1);
  if (idx < 0) return;
  rebuildItemsList(idx);
}

void ScrapeResultSelectionModel::applyCollectionCheckState(int collectionIndex, bool checked) {
  if (collectionIndex < 0) return;
  if (checked) {
    // Newly-checked collection: default the inclusion set to "every
    // item we know about". The items list rebuild ticks each row.
    if (m_itemsCacheByCollection.contains(collectionIndex)) {
      m_itemSelectionByCollection[collectionIndex] =
          m_itemsCacheByCollection.value(collectionIndex);
    } else {
      // Empty entry signals "include all" until the DB lookup lands;
      // the rebuildItemsList call kicks the DB fetch which populates
      // both caches once paths arrive. Without that fetch
      // m_itemSelectionByCollection[idx] would stay empty and the
      // Scrape button would silently no-op for this collection.
      m_itemSelectionByCollection.insert(collectionIndex, QStringList());
      rebuildItemsList(collectionIndex);
    }
  } else {
    m_itemSelectionByCollection.remove(collectionIndex);
  }
}

void ScrapeResultSelectionModel::onCollectionCheckChanged(QTreeWidgetItem *item, int column) {
  if (column != 0) return;
  const int idx = m_treeItemToCollectionIndex.value(item, -1);
  if (idx < 0) return;
  const bool checked = item->checkState(0) == Qt::Checked;
  applyCollectionCheckState(idx, checked);

  // Cascade the new state through the whole subtree: checking a parent
  // collection selects its subcollections too, unchecking clears them.
  // Tree signals are blocked while the child check states are written
  // so this doesn't re-enter once per child — the per-collection
  // bookkeeping is applied directly instead.
  QSet<int> affected{idx};
  {
    QSignalBlocker blocker(m_dlg->m_collectionTree);
    std::function<void(QTreeWidgetItem *)> cascade = [&](QTreeWidgetItem *parent) {
      for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem *child = parent->child(i);
        child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        const int childIdx = m_treeItemToCollectionIndex.value(child, -1);
        if (childIdx >= 0) {
          applyCollectionCheckState(childIdx, checked);
          affected.insert(childIdx);
        }
        cascade(child);
      }
    };
    cascade(item);
  }

  // Refresh the items list if the collection currently on screen is
  // one the cascade just touched.
  const auto *cur = m_dlg->m_collectionTree->currentItem();
  const int curIdx =
      cur ? m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1) : -1;
  if (curIdx >= 0 && affected.contains(curIdx)) {
    rebuildItemsList(curIdx);
  }
}

void ScrapeResultSelectionModel::rebuildItemsList(int collectionIndex) {
  if (!m_dlg->m_scraperCtx.collections || collectionIndex < 0 ||
      collectionIndex >= m_dlg->m_scraperCtx.collections->size()) {
    return;
  }
  const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[collectionIndex];
  m_dlg->m_itemsHeaderLabel->setText(tr("Items in '%1'").arg(cfg.name));

  // Fetch from DB on first display per session; cache for subsequent
  // tree clicks. Fetch is async — populate cache from the response.
  if (!m_itemsCacheByCollection.contains(collectionIndex)) {
    m_dlg->m_unifiedItemsList->clear();
    auto *placeholder = new QListWidgetItem(tr("Loading items…"), m_dlg->m_unifiedItemsList);
    placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsEnabled);
    // Kartend-m02z: read DB through ctx instead of cached pointer.
    auto *db = m_dlg->m_scraperCtx.ctx ? m_dlg->m_scraperCtx.ctx->databaseManager() : nullptr;
    if (!db || !m_dlg->m_scraperCtx.collections) return;
    CollectionContext context;
    context.config = cfg;
    context.currentIndex = collectionIndex;
    QPointer<ScrapeResultDialog> guard(m_dlg);
    auto *connHolder = new QObject(this);
    QObject::connect(
        db, &IDatabaseManager::itemsRangeLoaded, connHolder,
        [guard, connHolder, collectionIndex](
            int /*offset*/, const QStringList &filePaths, const QHash<QString, QString> &,
            const QHash<QString, QString> &, const QHash<QString, QString> &,
            const QHash<QString, int> &fileToCollectionIndex, int requestedCollectionIndex) {
          // itemsRangeLoaded is a shared signal: when a parent collection is
          // cascade-checked the dialog has one fetchItemsRange in flight per
          // collection, and every connected handler sees every emission.
          // Consume ONLY the result for the collection this fetch asked for —
          // otherwise this collection's cache gets populated from another
          // collection's items (the whole-parent-group over-count bug).
          if (requestedCollectionIndex != collectionIndex) return;
          connHolder->deleteLater();
          if (guard.isNull()) return;
          guard->m_selectionModel->m_itemsCacheByCollection[collectionIndex] = filePaths;
          // Retain each item's owning-collection index so a
          // scrape of a shell parent routes per item rather
          // than dumping everything on the parent.
          guard->m_selectionModel->m_itemOwnerByCollection[collectionIndex] = fileToCollectionIndex;
          // If the collection was checked before items landed,
          // populate the inclusion set with the full list now.
          if (guard->m_selectionModel->m_itemSelectionByCollection.contains(collectionIndex) &&
              guard->m_selectionModel->m_itemSelectionByCollection.value(collectionIndex)
                  .isEmpty()) {
            guard->m_selectionModel->m_itemSelectionByCollection[collectionIndex] = filePaths;
          }
          // Only re-render if the user is still viewing this collection.
          const auto *cur = guard->m_collectionTree->currentItem();
          const int curIdx = cur ? guard->m_selectionModel->m_treeItemToCollectionIndex.value(
                                       const_cast<QTreeWidgetItem *>(cur), -1)
                                 : -1;
          if (curIdx == collectionIndex) {
            guard->m_selectionModel->rebuildItemsList(collectionIndex);
          }
        });
    db->fetchItemsRange(context, *m_dlg->m_scraperCtx.collections, 0,
                        std::numeric_limits<int>::max(), QString());
    return;
  }

  // Cache hit — render synchronously.
  const QStringList &paths = m_itemsCacheByCollection.value(collectionIndex);
  const auto *treeRow = [&]() -> QTreeWidgetItem * {
    for (auto it = m_treeItemToCollectionIndex.constBegin();
         it != m_treeItemToCollectionIndex.constEnd(); ++it) {
      if (it.value() == collectionIndex) return it.key();
    }
    return nullptr;
  }();
  const bool collectionChecked = treeRow && treeRow->checkState(0) == Qt::Checked;
  const QStringList &included = m_itemSelectionByCollection.value(collectionIndex);
  const QSet<QString> includedSet(included.begin(), included.end());

  QSignalBlocker b(m_dlg->m_unifiedItemsList);
  m_dlg->m_unifiedItemsList->clear();
  for (const QString &path : paths) {
    auto *row = new QListWidgetItem(QFileInfo(path).fileName(), m_dlg->m_unifiedItemsList);
    row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
    row->setData(Qt::UserRole, path);
    if (!collectionChecked) {
      row->setCheckState(Qt::Unchecked);
      row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
    } else {
      row->setCheckState(includedSet.contains(path) ? Qt::Checked : Qt::Unchecked);
    }
  }
}

void ScrapeResultSelectionModel::onItemCheckChanged(QListWidgetItem *item) {
  if (!item) return;
  const auto *cur = m_dlg->m_collectionTree->currentItem();
  if (!cur) return;
  const int idx = m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1);
  if (idx < 0) return;
  const QString path = item->data(Qt::UserRole).toString();
  if (path.isEmpty()) return;
  QStringList included = m_itemSelectionByCollection.value(idx);
  if (item->checkState() == Qt::Checked) {
    if (!included.contains(path)) included.append(path);
  } else {
    included.removeAll(path);
  }
  m_itemSelectionByCollection[idx] = included;
}

void ScrapeResultSelectionModel::setAllItemsChecked(bool checked) {
  const auto *cur = m_dlg->m_collectionTree->currentItem();
  if (!cur) return;
  const int idx = m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1);
  if (idx < 0) return;
  // The items list rows are only enabled when the collection itself
  // is checked; respect that gating here so disabled rows stay off.
  if (cur->checkState(0) != Qt::Checked && checked) return;
  QStringList included;
  QSignalBlocker b(m_dlg->m_unifiedItemsList);
  for (int i = 0; i < m_dlg->m_unifiedItemsList->count(); ++i) {
    auto *row = m_dlg->m_unifiedItemsList->item(i);
    if (!(row->flags() & Qt::ItemIsEnabled)) continue;
    row->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    if (checked) {
      const QString path = row->data(Qt::UserRole).toString();
      if (!path.isEmpty()) included.append(path);
    }
  }
  m_itemSelectionByCollection[idx] = included;
}

void ScrapeResultSelectionModel::preCheckSingleItem(int preCollectionIndex,
                                                    const QString &preItemPath) {
  // Right-click flow: pre-check exactly the requested collection +
  // its single item, leaving every other collection in the unchecked
  // default state.
  if (preCollectionIndex < 0 || !m_dlg->m_scraperCtx.collections ||
      preCollectionIndex >= m_dlg->m_scraperCtx.collections->size()) {
    return;
  }
  for (auto it = m_treeItemToCollectionIndex.constBegin();
       it != m_treeItemToCollectionIndex.constEnd(); ++it) {
    if (it.value() == preCollectionIndex) {
      QSignalBlocker b(m_dlg->m_collectionTree);
      it.key()->setCheckState(0, Qt::Checked);
      m_dlg->m_collectionTree->setCurrentItem(it.key());
      if (!preItemPath.isEmpty()) {
        // Seed the inclusion list with the single requested path so
        // the items-list rebuild ticks only that row.
        m_itemSelectionByCollection[preCollectionIndex] = {preItemPath};
        // Pre-populate the items cache too so we don't need a DB
        // round-trip to display the row immediately.
        if (!m_itemsCacheByCollection.contains(preCollectionIndex)) {
          m_itemsCacheByCollection[preCollectionIndex] = {preItemPath};
        }
      }
      rebuildItemsList(preCollectionIndex);
      break;
    }
  }
}

int ScrapeResultSelectionModel::collectionIndexForRow(const QTreeWidgetItem *row) const {
  return m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(row), -1);
}

int ScrapeResultSelectionModel::totalCheckedItemCount() const {
  int total = 0;
  for (auto it = m_itemSelectionByCollection.constBegin();
       it != m_itemSelectionByCollection.constEnd(); ++it) {
    total += it.value().size();
  }
  return total;
}
