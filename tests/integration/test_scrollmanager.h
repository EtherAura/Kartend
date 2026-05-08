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
};

#endif // TEST_SCROLLMANAGER_H
