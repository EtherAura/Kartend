// Kartend-uodi step: drag-drop + context-menu slot bodies extracted from
// settingsdialogtree.cpp. Covers the two emission points where the user
// reshapes the tree by direct manipulation: dropping a row to reparent it
// (onTreeRearranged) and the context menu over a row (onTreeContextMenuRequested).
// Selection-driven flows (onTreeItemSelectionChanged, onTreeItemChanged)
// live in settingsdialogtreesync.cpp; add/duplicate/copy lives in
// settingsdialogtreemutation.cpp.
#include <functional>
#include <QAction>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "collection/validationhelpers.h"
#include "collectiontreewidget.h"
#include "settingsdialog.h"
#include "treemanager.h"

void SettingsDialog::onTreeContextMenuRequested(const QPoint &pos) {
  if (!collectionTreeWidget) {
    return;
  }
  QTreeWidgetItem *target = collectionTreeWidget->itemAt(pos);

  QMenu menu(collectionTreeWidget);
  // linked-mirror rows are read-only viewports; per-row actions (rename,
  // duplicate, delete) belong on the canonical row.
  const bool isLinked = target && target->data(0, Qt::UserRole).toBool();
  // Per-row actions only make sense when the click hit a non-linked item.
  if (target && !isLinked) {
    QAction *renameAction = menu.addAction(tr("Rename"));
    QAction *duplicateAction = menu.addAction(tr("Duplicate..."));
    QAction *deleteAction = menu.addAction(tr("Delete"));
    menu.addSeparator();
    QAction *copyFromAction = menu.addAction(tr("Copy Settings From..."));
    menu.addSeparator();
    QAction *expandSubAction = menu.addAction(tr("Expand subtree"));
    QAction *collapseSubAction = menu.addAction(tr("Collapse subtree"));
    // Subtree expand/collapse is only meaningful when the row has children.
    const bool hasChildren = target->childCount() > 0;
    expandSubAction->setEnabled(hasChildren);
    collapseSubAction->setEnabled(hasChildren);
    menu.addSeparator();
    QAction *expandAllAction = menu.addAction(tr("Expand all"));
    QAction *collapseAllAction = menu.addAction(tr("Collapse all"));

    // Selecting the row first ensures the form/save state is consistent
    // with the action — matches what clicking a row before pressing the
    // toolbar button would do. If the user has unsaved changes and cancels
    // the resulting prompt (handled by onTreeItemSelectionChanged), the
    // selection reverts and we abort the menu action so we never operate
    // on the wrong collection.
    auto switchToTarget = [this, target]() -> bool {
      if (currentTreeItem == target) {
        return true;
      }
      collectionTreeWidget->setCurrentItem(target);
      return currentTreeItem == target;
    };
    connect(renameAction, &QAction::triggered, this, [this, target, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      collectionTreeWidget->editItem(target, 0);
    });
    connect(duplicateAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      duplicateCollection();
    });
    connect(deleteAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      removeCollection();
    });
    connect(copyFromAction, &QAction::triggered, this, [this, switchToTarget]() {
      if (!switchToTarget()) {
        return;
      }
      copySettingsFromOtherCollection();
    });
    connect(expandSubAction, &QAction::triggered, this, [this, target]() {
      if (m_treeManager) m_treeManager->setSubtreeExpanded(target, true);
    });
    connect(collapseSubAction, &QAction::triggered, this, [this, target]() {
      if (m_treeManager) m_treeManager->setSubtreeExpanded(target, false);
    });
    connect(expandAllAction, &QAction::triggered, collectionTreeWidget, &QTreeWidget::expandAll);
    connect(collapseAllAction, &QAction::triggered, collectionTreeWidget,
            &QTreeWidget::collapseAll);
  } else {
    // Empty area: only the all-tree actions apply.
    QAction *expandAllAction = menu.addAction(tr("Expand all"));
    QAction *collapseAllAction = menu.addAction(tr("Collapse all"));
    connect(expandAllAction, &QAction::triggered, collectionTreeWidget, &QTreeWidget::expandAll);
    connect(collapseAllAction, &QAction::triggered, collectionTreeWidget,
            &QTreeWidget::collapseAll);
  }

  menu.exec(collectionTreeWidget->viewport()->mapToGlobal(pos));
}

void SettingsDialog::onTreeRearranged() {
  // Walk the post-drop tree and resync parentCollectionIndex /
  // isSubcollection on every collection. This is the only place where
  // parent linkage gets rewritten by user input outside the parent combo.
  // linked mirrors aren't draggable, but a drop can still shuffle them
  // along with their primary parent — so we skip linked items when
  // resolving the canonical primary parent for each collection and
  // recurse into them so their children (if any — currently none) are
  // still visited correctly.
  std::function<void(QTreeWidgetItem *, int)> walk = [&](QTreeWidgetItem *item, int parentIdx) {
    if (!item) {
      return;
    }
    const bool isLinked = item->data(0, Qt::UserRole).toBool();
    int idx = m_treeManager ? m_treeManager->indexOf(item) : -1;
    if (!isLinked && CollectionUtils::isValidIndex(idx, &collections)) {
      collections[idx].parentCollectionIndex = parentIdx;
      collections[idx].isSubcollection = (parentIdx >= 0);
      if (idx < m_workingCollections.size()) {
        m_workingCollections[idx].parentCollectionIndex = parentIdx;
        m_workingCollections[idx].isSubcollection = (parentIdx >= 0);
      }
    }
    // Children of a linked mirror inherit the same parent semantics from
    // the mirror's position (i.e. their primary parent is whoever the
    // mirror is under), since the mirror itself is just a viewport.
    const int childParentIdx = isLinked ? parentIdx : idx;
    for (int i = 0; i < item->childCount(); ++i) {
      walk(item->child(i), childParentIdx);
    }
  };
  for (int i = 0; i < collectionTreeWidget->topLevelItemCount(); ++i) {
    walk(collectionTreeWidget->topLevelItem(i), -1);
  }

  // Persist immediately so the rearranged tree survives a Cancel of the
  // outer dialog, matching addCollection() / duplicateCollection().
  emit collectionSaved(collections);
  m_collectionSaved = true;
  updateSaveButtonStyle();
}
