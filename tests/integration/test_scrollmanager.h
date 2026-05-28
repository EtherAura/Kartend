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
  void getFilePaths_isEmptyOnFreshFixture();
  void willNeedVerticalScrollbar_returnsBoolWithoutCrash();

  // Kartend-2hzy pilot: gridLayoutChanged → ScrollManager::onGridLayoutChanged
  // end-to-end re-read. Two cases: signal targeted at the active collection
  // must apply the new layout cluster to m_context; signal targeted at a
  // non-active collection index must be a no-op (the next switchCollection
  // path applies it instead, so we shouldn't shadow that).
  void gridLayoutChanged_appliesToActiveCollection();
  void gridLayoutChanged_ignoresNonActiveCollection();
};

#endif // TEST_SCROLLMANAGER_H
