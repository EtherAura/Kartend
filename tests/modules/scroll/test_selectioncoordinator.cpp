// Unit tests for SelectionCoordinator (Kartend-93vuk) — the scroll module's
// selection-state + movement-analysis helper extracted from ScrollManager.
// Exercised widget-free: analyzeMovement() is pure, setSelectedIndex() updates
// state + emits, rectForIndex() uses injected position/metrics callbacks.
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "selectioncoordinator.h"
#include "selectionoverlaymanager.h"

class TestSelectionCoordinator : public QObject {
  Q_OBJECT
private slots:
  void analyzeMovement_invalidInputsReturnNeutral();
  void analyzeMovement_singleStepSameRowIsHorizontal();
  void analyzeMovement_rowWrapStepIsVertical();
  void analyzeMovement_exactRowJumpIsVertical();
  void analyzeMovement_multiRowJumpIsVertical();
  void setSelectedIndex_emitsAndTracksDirection();
  void setSelectedIndex_sameIndexDoesNotEmit();
  void rectForIndex_outOfRangeOrNoCallbacksIsEmpty();
  void rectForIndex_usesInjectedPositionAndMetrics();
  void reset_clearsAllState();
};

void TestSelectionCoordinator::analyzeMovement_invalidInputsReturnNeutral() {
  SelectionCoordinator c;
  for (auto m : {c.analyzeMovement(-1, 0, 4), c.analyzeMovement(0, -1, 4), c.analyzeMovement(1, 0, 0)}) {
    QVERIFY(!m.isHorizontal);
    QCOMPARE(m.direction, 0);
  }
}

void TestSelectionCoordinator::analyzeMovement_singleStepSameRowIsHorizontal() {
  SelectionCoordinator c;
  const auto m = c.analyzeMovement(2, 1, 4); // row 0 → row 0
  QVERIFY(m.isHorizontal);
  QCOMPARE(m.direction, 1);
}

void TestSelectionCoordinator::analyzeMovement_rowWrapStepIsVertical() {
  SelectionCoordinator c;
  // 3 → 4 with itemsPerRow=4 is a single step but crosses row 0 → row 1.
  const auto m = c.analyzeMovement(4, 3, 4);
  QVERIFY(!m.isHorizontal);
  QCOMPARE(m.direction, 1);
}

void TestSelectionCoordinator::analyzeMovement_exactRowJumpIsVertical() {
  SelectionCoordinator c;
  const auto m = c.analyzeMovement(4, 0, 4); // straight down one row
  QVERIFY(!m.isHorizontal);
  QCOMPARE(m.direction, 1);
}

void TestSelectionCoordinator::analyzeMovement_multiRowJumpIsVertical() {
  SelectionCoordinator c;
  const auto m = c.analyzeMovement(0, 9, 4); // |delta| 9 > itemsPerRow → vertical
  QVERIFY(!m.isHorizontal);
  QCOMPARE(m.direction, -1);
}

void TestSelectionCoordinator::setSelectedIndex_emitsAndTracksDirection() {
  SelectionCoordinator c;
  QSignalSpy spy(&c, &SelectionCoordinator::selectionChanged);

  c.setSelectedIndex(5); // from -1: prev<0 → direction 0
  QCOMPARE(c.selectedIndex(), 5);
  QCOMPARE(c.selectionDirection(), 0);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), 5);
  QCOMPARE(spy.at(0).at(1).toInt(), -1);

  c.setSelectedIndex(8); // 5 → 8: forward
  QCOMPARE(c.selectionDirection(), 1);
  c.setSelectedIndex(2); // 8 → 2: backward
  QCOMPARE(c.selectionDirection(), -1);
  QCOMPARE(spy.count(), 3);
}

void TestSelectionCoordinator::setSelectedIndex_sameIndexDoesNotEmit() {
  SelectionCoordinator c;
  c.setSelectedIndex(4);
  QSignalSpy spy(&c, &SelectionCoordinator::selectionChanged);
  c.setSelectedIndex(4); // no change → no emit
  QCOMPARE(spy.count(), 0);
}

void TestSelectionCoordinator::rectForIndex_outOfRangeOrNoCallbacksIsEmpty() {
  SelectionCoordinator c;
  // No callbacks installed yet → empty even for an in-range index.
  QVERIFY(c.rectForIndex(0, 5).isNull());
  c.setPositionCallback([](int) { return QPoint(0, 0); });
  c.setMetricsCallback([]() { return std::make_pair(100, 80); });
  // Out of range stays empty.
  QVERIFY(c.rectForIndex(-1, 5).isNull());
  QVERIFY(c.rectForIndex(5, 5).isNull());
}

void TestSelectionCoordinator::rectForIndex_usesInjectedPositionAndMetrics() {
  SelectionCoordinator c;
  const QPoint pos(40, 60);
  c.setPositionCallback([pos](int) { return pos; });
  c.setMetricsCallback([]() { return std::make_pair(100, 80); });
  const QRect got = c.rectForIndex(2, 5);
  // Must equal what the overlay helper produces for the injected pos/metrics.
  QCOMPARE(got, SelectionOverlayManager::overlayRectForPosition(pos, 100, 80));
  QVERIFY(!got.isNull());
}

void TestSelectionCoordinator::reset_clearsAllState() {
  SelectionCoordinator c;
  c.setSelectedIndex(3);
  c.setCommittedIndex(3);
  c.setSelectedRow(1);
  c.reset();
  QCOMPARE(c.selectedIndex(), -1);
  QCOMPARE(c.committedIndex(), -1);
  QCOMPARE(c.selectedRow(), -1);
  QCOMPARE(c.selectionDirection(), 0);
}

QTEST_MAIN(TestSelectionCoordinator)
#include "test_selectioncoordinator.moc"
