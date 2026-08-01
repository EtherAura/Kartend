#ifndef TEST_SCROLLMANAGER_H
#define TEST_SCROLLMANAGER_H

#include <QObject>

// Integration tests for ScrollManager. The 9-TU manager was
// previously covered only by GridLayoutCalculator/ScrollHelpers helper-TU
// tests and a nullptr check in mainwindow_smoke. These exercise the public
// state-query and toggle surface through a real MainWindow fixture so the
// virtual-scrolling pipeline is fully wired.
class TestScrollManager : public QObject {
  Q_OBJECT
private slots:
  void getTotalItems_isZeroOnFreshFixture();
  void getCurrentGridWidth_returnsPositive();
  void sidebarShrinkingActive_roundTripsThroughSetter();
  void hasPendingSelectionRestoreByPath_isFalseInitially();
  void hasPendingSelectionRestoreByPath_flipsAfterSet();
  void hasPreSearchState_isFalseBeforeSave();
  void filterChange_clearOnEmptyStateIsSafe();
  // Selection restore by path with an in-memory filter active: the DB's
  // media-only answer must land on the FILTERED visual index (or be skipped
  // when the item is filtered out), not on the store-space actual index.
  void visualIndexForPathRestore_mapsThroughActiveFilter();
  void getFilePaths_isEmptyOnFreshFixture();
  void willNeedVerticalScrollbar_returnsBoolWithoutCrash();

  // Behavior-level layout outcomes: a populated data model must produce a
  // consistent grid geometry (rows derived from the configured items-per-row,
  // container height derived from rows) and predict that the vertical
  // scrollbar will be needed; an in-memory filter must shrink that geometry,
  // and clearing it must restore the original extent.
  void setupVirtualScrolling_populatedItemsProduceScrollableLayout();
  void applyFilter_shrinksLayoutExtentAndClearRestoresIt();

  // Kartend-2hzy pilot: gridLayoutChanged → ScrollManager::onGridLayoutChanged
  // end-to-end re-read. Two cases: signal targeted at the active collection
  // must apply the new layout cluster to m_context; signal targeted at a
  // non-active collection index must be a no-op (the next switchCollection
  // path applies it instead, so we shouldn't shadow that).
  void gridLayoutChanged_appliesToActiveCollection();
  void gridLayoutChanged_ignoresNonActiveCollection();
};

#endif // TEST_SCROLLMANAGER_H
