// Tests for EventHelpers — pure helpers extracted from EventManager.

#include <QEvent>
#include <QTest>

#include "eventhelpers.h"

class TestEventHelpers : public QObject {
  Q_OBJECT

private slots:
  // isActivityEvent
  void isActivity_recognisesInputEvents();
  void isActivity_rejectsLayoutAndPaintEvents();
  void isActivity_rejectsTimerAndChildEvents();

  // shouldArmFirstClickDelay
  void arm_returnsFalseWhenLastActivityMsZero();
  void arm_returnsFalseWhenLastActivityMsNegative();
  void arm_returnsFalseWhenThresholdNonPositive();
  void arm_returnsFalseBelowThreshold();
  void arm_returnsTrueAtThreshold();
  void arm_returnsTrueAboveThreshold();
  void arm_returnsFalseOnNegativeDelta();

  // computeLogicalCenteredScrollY
  void scroll_returnsZeroWhenViewportHeightNonPositive();
  void scroll_centersFirstRowAtZeroViewportOffset();
  void scroll_centersSelectedRow();
  void scroll_addsHeaderOffset();
  void scroll_addsVerticalSpacing();
  void scroll_canReturnNegativeForTopRows();
};

// ------------------------------ isActivityEvent -----------------------------

void TestEventHelpers::isActivity_recognisesInputEvents() {
  QVERIFY(EventHelpers::isActivityEvent(QEvent::MouseMove));
  QVERIFY(EventHelpers::isActivityEvent(QEvent::MouseButtonPress));
  QVERIFY(EventHelpers::isActivityEvent(QEvent::MouseButtonRelease));
  QVERIFY(EventHelpers::isActivityEvent(QEvent::KeyPress));
  QVERIFY(EventHelpers::isActivityEvent(QEvent::KeyRelease));
  QVERIFY(EventHelpers::isActivityEvent(QEvent::Wheel));
}

void TestEventHelpers::isActivity_rejectsLayoutAndPaintEvents() {
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::Paint));
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::Resize));
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::LayoutRequest));
}

void TestEventHelpers::isActivity_rejectsTimerAndChildEvents() {
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::Timer));
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::ChildAdded));
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::Show));
  // Hover events are NOT classified as activity here — they fire constantly
  // even when the user is idle. Important regression guard: production code
  // would otherwise reset attract-mode every frame on hover-rich UIs.
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::HoverMove));
  QVERIFY(!EventHelpers::isActivityEvent(QEvent::HoverEnter));
}

// ------------------------------ shouldArmFirstClickDelay --------------------

void TestEventHelpers::arm_returnsFalseWhenLastActivityMsZero() {
  QVERIFY(!EventHelpers::shouldArmFirstClickDelay(0, 5000, 1000));
}

void TestEventHelpers::arm_returnsFalseWhenLastActivityMsNegative() {
  QVERIFY(!EventHelpers::shouldArmFirstClickDelay(-1, 5000, 1000));
}

void TestEventHelpers::arm_returnsFalseWhenThresholdNonPositive() {
  QVERIFY(!EventHelpers::shouldArmFirstClickDelay(1000, 5000, 0));
  QVERIFY(!EventHelpers::shouldArmFirstClickDelay(1000, 5000, -50));
}

void TestEventHelpers::arm_returnsFalseBelowThreshold() {
  // delta = 999, threshold = 1000 -> false
  QVERIFY(!EventHelpers::shouldArmFirstClickDelay(1000, 1999, 1000));
}

void TestEventHelpers::arm_returnsTrueAtThreshold() {
  // delta == threshold -> true (>= compare).
  QVERIFY(EventHelpers::shouldArmFirstClickDelay(1000, 2000, 1000));
}

void TestEventHelpers::arm_returnsTrueAboveThreshold() {
  QVERIFY(EventHelpers::shouldArmFirstClickDelay(1000, 9000, 1000));
}

void TestEventHelpers::arm_returnsFalseOnNegativeDelta() {
  // Clock skew (now < last). Don't accidentally arm based on a small
  // negative delta wrapping into "huge unsigned" territory.
  QVERIFY(!EventHelpers::shouldArmFirstClickDelay(2000, 1000, 1000));
}

// ------------------------------ computeLogicalCenteredScrollY ---------------

void TestEventHelpers::scroll_returnsZeroWhenViewportHeightNonPositive() {
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(
               /*selectedIndex=*/5, /*gridWidth=*/4, /*itemHeight=*/100,
               /*verticalSpacing=*/10, /*margins=*/8, /*headerOffset=*/0,
               /*viewportHeight=*/0),
           0);
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(5, 4, 100, 10, 8, 0, -200), 0);
}

void TestEventHelpers::scroll_centersFirstRowAtZeroViewportOffset() {
  // Row 0, item at margins=8, viewport 100, itemHeight 100.
  // logicalY = 8 + 0 - (100 - 100)/2 = 8.
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(0, 4, 100, 0, 8, 0, 100), 8);
}

void TestEventHelpers::scroll_centersSelectedRow() {
  // selectedIndex=4, gridWidth=4 -> row 1.
  // itemY = 8 + 1 * (100 + 0) = 108. viewport=300, itemHeight=100.
  // logicalY = 108 - (300 - 100)/2 = 108 - 100 = 8.
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(4, 4, 100, 0, 8, 0, 300), 8);

  // Row 2.
  // itemY = 8 + 2 * 100 = 208. logicalY = 208 - 100 = 108.
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(8, 4, 100, 0, 8, 0, 300), 108);
}

void TestEventHelpers::scroll_addsHeaderOffset() {
  // Same as above but with a 50-pixel list header offset. Final logicalY
  // shifts by exactly +50.
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(4, 4, 100, 0, 8, 50, 300), 58);
}

void TestEventHelpers::scroll_addsVerticalSpacing() {
  // selectedIndex=8 -> row 2 with verticalSpacing=20.
  // itemY = 8 + 2 * (100 + 20) = 248. logicalY = 248 - 100 = 148.
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(8, 4, 100, 20, 8, 0, 300), 148);
}

void TestEventHelpers::scroll_canReturnNegativeForTopRows() {
  // Tall viewport, top row -> logicalY can underflow zero. Helper does NOT
  // clamp (caller bounds against scroll max).
  // Row 0, viewport 1000, itemHeight 100, no spacing/margin/header.
  // logicalY = 0 - (1000 - 100)/2 = -450.
  QCOMPARE(EventHelpers::computeLogicalCenteredScrollY(0, 4, 100, 0, 0, 0, 1000), -450);
}

QTEST_APPLESS_MAIN(TestEventHelpers)
#include "test_eventhelpers.moc"
