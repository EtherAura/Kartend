#ifndef COLLECTIONPICKERDIALOG_H
#define COLLECTIONPICKERDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>

#include "collectionutils.h"

QT_BEGIN_NAMESPACE
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

/// Modal multi-select picker for collection indices (Kartend-f5i9). Shows the
/// collection list as a checkable QTreeWidget that mirrors the parent/child
/// hierarchy. Cascading is bidirectional: toggling a parent flips all
/// descendants, and a parent shows the partial state when its descendants
/// disagree (Qt::ItemIsAutoTristate).
///
/// The @p excludeIndex is hidden from the tree — typically the source
/// collection that's pushing settings out, since pushing onto itself is a
/// no-op. Children of the excluded collection are reparented to the excluded
/// collection's parent so the visible hierarchy stays connected.
class CollectionPickerDialog : public QDialog {
  Q_OBJECT
public:
  CollectionPickerDialog(const QList<CollectionConfig> &collections, int excludeIndex,
                         const QList<int> &initialChecked = {}, QWidget *parent = nullptr);

  [[nodiscard]] QList<int> selectedIndices() const;

private slots:
  void onItemChanged(QTreeWidgetItem *item, int column);
  void onSelectAllClicked();
  void onSelectNoneClicked();

private:
  void setupUi();
  void populateTree(const QList<int> &initialChecked);
  QTreeWidgetItem *createItem(int collectionIndex, QTreeWidgetItem *parentItem);
  /// Walks @p idx's parent chain, skipping @p excluded if encountered. Returns
  /// -1 once the walk reaches a root or runs off the end.
  static int displayedParent(int idx, int excluded, const QList<CollectionConfig> &cols);

  QList<CollectionConfig> m_collections;
  int m_excludeIndex;
  QHash<int, QTreeWidgetItem *> m_indexToItem;
  QHash<QTreeWidgetItem *, int> m_itemToIndex;
  QTreeWidget *m_tree = nullptr;
  bool m_blockItemChanged = false;
};

#endif
