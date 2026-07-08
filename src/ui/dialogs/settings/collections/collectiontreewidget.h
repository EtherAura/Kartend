#ifndef COLLECTIONTREEWIDGET_H
#define COLLECTIONTREEWIDGET_H

#include <functional>

#include <QTreeWidget>

QT_BEGIN_NAMESPACE
class QDropEvent;
class QTreeWidgetItem;
QT_END_NAMESPACE

/// Promoted QTreeWidget used by SettingsDialog for the collection tree
/// Adds drag-and-drop reparenting with a host-supplied cycle
/// check so the dialog can keep its existing
/// SettingsDialog::wouldCreateCircularReference() invariant. The widget itself
/// owns no model state — after a successful drop it emits treeRearranged() so
/// the host can walk the post-drop tree and resync
/// CollectionConfig::parentCollectionIndex / isSubcollection.
class CollectionTreeWidget : public QTreeWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionTreeWidget)
public:
  using CycleCheck = std::function<bool(int childIndex, int potentialParentIndex)>;
  using ItemToIndex = std::function<int(const QTreeWidgetItem *)>;

  explicit CollectionTreeWidget(QWidget *parent = nullptr);

  void setCycleCheck(CycleCheck check) { m_cycleCheck = std::move(check); }
  void setItemToIndex(ItemToIndex resolver) { m_itemToIndex = std::move(resolver); }

  /// True while the base-class drop handler is relocating items. An internal
  /// move takes the dragged rows out of the tree first, which transiently
  /// empties the selection — TreeManager's deselect gate must not treat that
  /// as a user deselect (item pointers are mid-move and a prompt-triggered
  /// save could rebuild the tree under the drop).
  [[nodiscard]] bool dropInProgress() const { return m_dropInProgress; }

signals:
  /// Emitted after a drag-and-drop reparenting completes successfully. The
  /// host should re-walk the tree and update parentCollectionIndex on each
  /// CollectionConfig.
  void treeRearranged();

protected:
  void dropEvent(QDropEvent *event) override;

private:
  CycleCheck m_cycleCheck;
  ItemToIndex m_itemToIndex;
  bool m_dropInProgress = false;
};

#endif
