#ifndef TREEMANAGER_H
#define TREEMANAGER_H

#include "collection/collectionconfig.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>

class CollectionTreeWidget;
class QTreeWidgetItem;
class SettingsTreeHost;

/// Kartend-ook62: the SettingsDialog tree controller. Owns the
/// CollectionTreeWidget's wiring (edit triggers, context-menu policy,
/// drag-drop cycle-check + item→index resolution) and the tree-population
/// state (the three index maps + the build/refresh/walk operations). It
/// subscribes to the widget's raw signals and re-emits them as the four
/// higher-level gesture signals below.
///
/// The reactions to those gestures (selection switch, rename commit,
/// context-menu actions, drag-drop reparent) stay as SettingsDialog slots
/// connected to these signals — by design, because each carries irreducible
/// dialog-state side effects (resolveUnsavedChanges, loadCollectionToUI,
/// dirty-flag/Save-button updates, collection add/duplicate/copy). Moving
/// those into the controller would re-couple it to ~15 dialog members, so the
/// boundary is "controller owns the widget + signalling; dialog owns the
/// stateful reactions".
///
/// Lifetime: owned by SettingsDialog (std::unique_ptr); constructed with no
/// QObject parent so the unique_ptr is the sole owner. The widget and
/// collection lists are non-owning references — assumed to outlive it.
class TreeManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TreeManager)
public:
  TreeManager(CollectionTreeWidget *widget, QList<CollectionConfig> *collections,
              QList<CollectionConfig> *workingCollections, QObject *parent = nullptr);

  /// Kartend-mnymg: borrow the host dialog's selection-state members by
  /// pointer (mirrors how the collection lists are borrowed) so the migrated
  /// gesture handlers read/write the same state the dialog's ~70 other call
  /// sites use, without relocating storage. @p host receives the dialog-side
  /// side effects (load/save/dirty/mutation).
  void setSelectionState(int *currentCollectionIndex, QTreeWidgetItem **currentTreeItem,
                         CollectionConfig *originalCollection, bool *collectionSaved);
  void setHost(SettingsTreeHost *host) { m_host = host; }

  /// Install the widget's edit/context-menu policy, the drag-drop cycle-check
  /// + item→index callbacks (resolved from this controller's own state), and
  /// connect its raw signals to this controller's own gesture-handler slots.
  /// Call once after the widget + selection-state + host are set.
  void attachWidget();

  /// True when making @p potentialParentIndex the parent of @p childIndex would
  /// form a cycle in the collection hierarchy. Used by the drag-drop cycle
  /// guard and the parent-combo populator.
  [[nodiscard]] bool wouldCreateCircularReference(int childIndex, int potentialParentIndex) const;

signals:
  /// Emitted after a drag-drop reparent has resynced parent linkage; the host
  /// connects this to re-emit SettingsDialog::collectionSaved so the change
  /// survives a Cancel of the outer dialog.
  void collectionsReordered();

private slots:
  // Kartend-mnymg: the tree gesture handlers, migrated off SettingsDialog. They
  // operate on the borrowed selection state + collection lists and route
  // dialog-side effects through m_host.
  void onWidgetSelectionChanged();
  void onWidgetItemChanged(QTreeWidgetItem *item, int column);
  void onWidgetContextMenuRequested(const QPoint &pos);
  void onWidgetRearranged();

public:
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

  // Kartend-mnymg: borrowed (non-owning) selection state + the host callback
  // surface. Null until setSelectionState/setHost run; the gesture handlers
  // guard on them.
  SettingsTreeHost *m_host = nullptr;
  int *m_currentCollectionIndex = nullptr;
  QTreeWidgetItem **m_currentTreeItem = nullptr;
  CollectionConfig *m_originalCollection = nullptr;
  bool *m_collectionSaved = nullptr;

  QHash<QTreeWidgetItem *, int> m_itemToIndex;
  QHash<int, QTreeWidgetItem *> m_indexToItem;
  QHash<int, QList<QTreeWidgetItem *>> m_indexToLinkedItems;

  /// True while rebuild() is clearing/repopulating the widget. The clear
  /// fires itemSelectionChanged, and onWidgetSelectionChanged's deselect
  /// gate must not run for that programmatic clear: the mutation flows
  /// (add / duplicate / remove) manage save state themselves around the
  /// rebuild, and the index maps still point at just-deleted items.
  bool m_rebuilding = false;
};

#endif // TREEMANAGER_H
