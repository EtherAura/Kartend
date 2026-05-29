#ifndef COLLECTIONPICKERDIALOG_H
#define COLLECTIONPICKERDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>

#include "collection/collectionconfig.h"
QT_BEGIN_NAMESPACE
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

/// Modal multi-select picker for collection indices.
/// Shows the collection list as a checkable QTreeWidget that
/// mirrors the parent/child hierarchy. Cascading is bidirectional: toggling a
/// parent flips all descendants, and a parent shows the partial state when
/// its descendants disagree (Qt::ItemIsAutoTristate).
///
/// Indices in @p excludedIndices are hidden from the tree. Two callers exist:
/// the Apply-to-Selected path passes the source collection (pushing onto
/// itself is a no-op); the Linked-Parents picker passes the source plus all
/// of its descendants (a collection can't link to itself or any node already
/// reachable through its primary subtree). Children of an excluded
/// collection get reparented to the excluded node's nearest non-excluded
/// ancestor so the visible hierarchy stays connected.
class CollectionPickerDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionPickerDialog)
public:
  CollectionPickerDialog(const QList<CollectionConfig> &collections,
                         const QList<int> &excludedIndices, const QList<int> &initialChecked = {},
                         QWidget *parent = nullptr);

  [[nodiscard]] QList<int> selectedIndices() const;

private slots:
  void onItemChanged(QTreeWidgetItem *item, int column);
  void onSelectAllClicked();
  void onSelectNoneClicked();

private:
  void setupUi();
  void populateTree(const QList<int> &initialChecked);
  QTreeWidgetItem *createItem(int collectionIndex, QTreeWidgetItem *parentItem);
  /// Walks @p idx's parent chain, skipping any ancestor in @p excluded.
  /// Returns -1 once the walk reaches a root or runs off the end.
  static int displayedParent(int idx, const QSet<int> &excluded,
                             const QList<CollectionConfig> &cols);

  QList<CollectionConfig> m_collections;
  QSet<int> m_excludedIndices;
  QHash<int, QTreeWidgetItem *> m_indexToItem;
  QHash<QTreeWidgetItem *, int> m_itemToIndex;
  QTreeWidget *m_tree = nullptr;
  bool m_blockItemChanged = false;
};

#endif
