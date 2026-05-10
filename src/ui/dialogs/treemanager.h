#ifndef TREEMANAGER_H
#define TREEMANAGER_H

#include "collectionutils.h"
#include <QHash>
#include <QList>
#include <QString>

class CollectionTreeWidget;
class QTreeWidgetItem;

/// Owns the SettingsDialog's tree-population state — the three index maps
/// (item→collection, collection→primary item, collection→linked-mirror
/// items) plus the pure-domain operations that build, refresh, and walk
/// them. Slots that wire user gestures back into the dialog stay on
/// SettingsDialog because they have non-tree side effects (e.g.
/// loadCollectionToUI, dirty-flag updates); those slots read this manager
/// for tree state.
///
/// Lifetime: owned by SettingsDialog (std::unique_ptr). The widget and
/// collection lists are non-owning references — TreeManager assumes they
/// outlive it. Not a QObject; no signals/slots needed.
class TreeManager {
public:
  TreeManager(CollectionTreeWidget *widget, QList<CollectionConfig> *collections,
              QList<CollectionConfig> *workingCollections);

  /// Wipe and re-populate every row from the live collections list. Mirrors
  /// the previous SettingsDialog::updateCollectionTreeWidget — same call
  /// sites should hit this directly.
  void rebuild();

  /// Expand the chain of ancestor items so the row at `collectionIndex` is
  /// visible. No-op when the index has no item registered.
  void expandPathTo(int collectionIndex);

  /// Rewrite or remove `oldName` references in every collection's
  /// `additionalParentNames` list when a collection is renamed (newName
  /// non-empty) or removed (newName empty). Mutates both `collections` and
  /// `workingCollections`. Does NOT trigger a rebuild — callers decide
  /// whether they also need to refresh the tree.
  void propagateNameChange(const QString &oldName, const QString &newName);

  /// Recursively expand or collapse `item` and every descendant under it.
  /// Used by the right-click "Expand subtree" / "Collapse subtree" menu
  /// entries.
  void setSubtreeExpanded(QTreeWidgetItem *item, bool expanded);

  // ── State queries (read-only views into the index maps) ─────────────────
  [[nodiscard]] CollectionTreeWidget *widget() const { return m_widget; }
  [[nodiscard]] const QHash<QTreeWidgetItem *, int> &itemToIndex() const { return m_itemToIndex; }
  [[nodiscard]] const QHash<int, QTreeWidgetItem *> &indexToItem() const { return m_indexToItem; }
  [[nodiscard]] const QList<QTreeWidgetItem *> &linkedItemsFor(int index) const;

  /// Convenience accessors. `itemAt` returns nullptr when the index has no
  /// item. `indexOf` returns -1 when the item is unknown. `contains` mirrors
  /// `itemToIndex.contains(item)` for call-site terseness.
  [[nodiscard]] QTreeWidgetItem *itemAt(int collectionIndex) const;
  [[nodiscard]] int indexOf(QTreeWidgetItem *item) const;
  [[nodiscard]] bool contains(QTreeWidgetItem *item) const;

  /// Per-collection list of linked-appearance items (italicised mirrors
  /// hanging off additionalParentNames parents). Empty when the collection
  /// has no linked appearances.
  [[nodiscard]] QList<QTreeWidgetItem *> linkedItems(int index) const;

private:
  void populate();
  QTreeWidgetItem *createItem(int collectionIndex, QTreeWidgetItem *parent = nullptr);
  void populateLinkedAppearances();

  CollectionTreeWidget *m_widget;
  QList<CollectionConfig> *m_collections;
  QList<CollectionConfig> *m_workingCollections;

  QHash<QTreeWidgetItem *, int> m_itemToIndex;
  QHash<int, QTreeWidgetItem *> m_indexToItem;
  QHash<int, QList<QTreeWidgetItem *>> m_indexToLinkedItems;
};

#endif // TREEMANAGER_H
