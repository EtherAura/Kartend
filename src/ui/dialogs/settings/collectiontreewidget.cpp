#include "collectiontreewidget.h"

#include <QDropEvent>
#include <QTreeWidgetItem>

CollectionTreeWidget::CollectionTreeWidget(QWidget *parent) : QTreeWidget(parent) {
  setDragEnabled(true);
  setAcceptDrops(true);
  setDragDropMode(QAbstractItemView::InternalMove);
  setDefaultDropAction(Qt::MoveAction);
}

void CollectionTreeWidget::dropEvent(QDropEvent *event) {
  // Reject the drop entirely if any selected source item would create a
  // circular reference at its new home. The default QTreeWidget behaviour
  // would happily attach a parent under one of its own descendants and
  // corrupt parentCollectionIndex on the next resync.
  if (m_cycleCheck && m_itemToIndex) {
    const QTreeWidgetItem *target = itemAt(event->position().toPoint());
    const int targetIndex = target ? m_itemToIndex(target) : -1;
    if (targetIndex >= 0) {
      for (QTreeWidgetItem *dragged : selectedItems()) {
        const int childIndex = m_itemToIndex(dragged);
        if (childIndex < 0) {
          continue;
        }
        if (m_cycleCheck(childIndex, targetIndex)) {
          event->ignore();
          return;
        }
      }
    }
  }
  QTreeWidget::dropEvent(event);
  if (event->isAccepted()) {
    emit treeRearranged();
  }
}
