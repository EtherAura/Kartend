#include "arrowkeyscrollhelper.h"
#include <QTest>

// Direct tests for the pure centering math extracted from
// ArrowKeyScrollHelper::calculateCenterTarget into the centerTargetFor static.
// target = (margins + itemY) + itemHeight/2 - viewportHeight/2, clamped to
// [0, scrollMax] (or floored at 0 only when scrollMax < 0).
class TestArrowKeyScrollHelper : public QObject {
  Q_OBJECT
private slots:
  void centerTargetFor_centersItemInViewport();
  void centerTargetFor_clampsAtZeroAndScrollMax();
  void centerTargetFor_itemTallerThanViewport();
  void centerTargetFor_noScrollbarBoundFloorsAtZeroOnly();
};

void TestArrowKeyScrollHelper::centerTargetFor_centersItemInViewport() {
  using H = ArrowKeyScrollHelper;
  // 10 + 5000 + 50 - 300 == 4760, well within [0, 100000].
  QCOMPARE(H::centerTargetFor(5000, 600, 100, 10, 100000), 4760);
  // Zero margins: itemY + itemHeight/2 - viewportHeight/2 == 1000 + 50 - 200.
  QCOMPARE(H::centerTargetFor(1000, 400, 100, 0, 100000), 850);
}

void TestArrowKeyScrollHelper::centerTargetFor_clampsAtZeroAndScrollMax() {
  using H = ArrowKeyScrollHelper;
  // Item near the top: raw target 10 + 0 + 50 - 300 == -240 -> floored to 0.
  QCOMPARE(H::centerTargetFor(0, 600, 100, 10, 100000), 0);
  // Item past the end: raw 19760 > scrollMax -> clamped to scrollMax.
  QCOMPARE(H::centerTargetFor(20000, 600, 100, 10, 10000), 10000);
}

void TestArrowKeyScrollHelper::centerTargetFor_itemTallerThanViewport() {
  using H = ArrowKeyScrollHelper;
  // itemHeight 800 > viewport 600: still centered on the item's middle.
  // 0 + 1000 + 400 - 300 == 1100.
  QCOMPARE(H::centerTargetFor(1000, 600, 800, 0, 100000), 1100);
}

void TestArrowKeyScrollHelper::centerTargetFor_noScrollbarBoundFloorsAtZeroOnly() {
  using H = ArrowKeyScrollHelper;
  // scrollMax < 0 => no upper clamp, only the 0 floor.
  QCOMPARE(H::centerTargetFor(20000, 600, 100, 10, -1), 19760);
  QCOMPARE(H::centerTargetFor(0, 600, 100, 10, -1), 0);
}

QTEST_MAIN(TestArrowKeyScrollHelper)
#include "test_arrowkeyscrollhelper.moc"
