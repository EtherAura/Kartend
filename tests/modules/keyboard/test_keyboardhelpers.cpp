// Tests for KeyboardHelpers — pure helpers extracted from KeyboardManager.

#include <Qt>
#include <QTest>

#include "collectiontypes.h"
#include "keyboardhelpers.h"

class TestKeyboardHelpers : public QObject {
  Q_OBJECT

private slots:
  // isConfirmKey
  void confirm_exactMatch();
  void confirm_returnTriggersOnEnter();
  void confirm_enterTriggersOnReturn();
  void confirm_unrelatedKeyRejected();
  void confirm_customConfirmKeyRequiresExactMatch();

  // scaleRepeatInterval
  void scale_zeroMultiplierReturnsBase();
  void scale_negativeMultiplierReturnsBase();
  void scale_oneMultiplierReturnsBase();
  void scale_doubleMultiplierHalvesInterval();
  void scale_halfMultiplierDoublesInterval();
  void scale_clampsToMinimum();
  void scale_roundsHalfAwayFromZero();

  // computeArrowDirection
  void arrow_invalidWhenGridWidthZero();
  void arrow_unknownKeyReturnsInvalid();

  void arrow_gridLeftRightStepOne();
  void arrow_gridUpDownStepGridWidth();

  void arrow_listAllAxesStepOne();
  void arrow_coverFlowAllAxesStepOne();

  void arrow_horizontalLeftRightStepGridWidthVerticalTrue();
  void arrow_horizontalUpDownStepOneVerticalFalse();

  void arrow_respectsRebinding();
};

// ------------------------------ isConfirmKey -------------------------------

void TestKeyboardHelpers::confirm_exactMatch() {
  QVERIFY(KeyboardHelpers::isConfirmKey(static_cast<int>(Qt::Key_Return),
                                         static_cast<int>(Qt::Key_Return)));
}

void TestKeyboardHelpers::confirm_returnTriggersOnEnter() {
  // Configured = Return, user pressed keypad Enter — should fire.
  QVERIFY(KeyboardHelpers::isConfirmKey(static_cast<int>(Qt::Key_Enter),
                                         static_cast<int>(Qt::Key_Return)));
}

void TestKeyboardHelpers::confirm_enterTriggersOnReturn() {
  // Configured = Enter, user pressed Return.
  QVERIFY(KeyboardHelpers::isConfirmKey(static_cast<int>(Qt::Key_Return),
                                         static_cast<int>(Qt::Key_Enter)));
}

void TestKeyboardHelpers::confirm_unrelatedKeyRejected() {
  QVERIFY(!KeyboardHelpers::isConfirmKey(static_cast<int>(Qt::Key_Space),
                                          static_cast<int>(Qt::Key_Return)));
}

void TestKeyboardHelpers::confirm_customConfirmKeyRequiresExactMatch() {
  // User rebinds confirm to Space. Return / Enter should NOT fire — the
  // Return/Enter equivalence is only when one of the two is the configured
  // key.
  QVERIFY(KeyboardHelpers::isConfirmKey(static_cast<int>(Qt::Key_Space),
                                         static_cast<int>(Qt::Key_Space)));
  QVERIFY(!KeyboardHelpers::isConfirmKey(static_cast<int>(Qt::Key_Return),
                                          static_cast<int>(Qt::Key_Space)));
  QVERIFY(!KeyboardHelpers::isConfirmKey(static_cast<int>(Qt::Key_Enter),
                                          static_cast<int>(Qt::Key_Space)));
}

// ------------------------------ scaleRepeatInterval ------------------------

void TestKeyboardHelpers::scale_zeroMultiplierReturnsBase() {
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(260, 0.0, 10), 260);
}

void TestKeyboardHelpers::scale_negativeMultiplierReturnsBase() {
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(260, -0.5, 10), 260);
}

void TestKeyboardHelpers::scale_oneMultiplierReturnsBase() {
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(260, 1.0, 10), 260);
}

void TestKeyboardHelpers::scale_doubleMultiplierHalvesInterval() {
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(260, 2.0, 10), 130);
}

void TestKeyboardHelpers::scale_halfMultiplierDoublesInterval() {
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(260, 0.5, 10), 520);
}

void TestKeyboardHelpers::scale_clampsToMinimum() {
  // Pathological multiplier that would otherwise produce ~5ms.
  // Helper must not let the event loop saturate.
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(50, 100.0, 10), 10);
}

void TestKeyboardHelpers::scale_roundsHalfAwayFromZero() {
  // 100 / 3 = 33.333... → 33 with round-to-nearest.
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(100, 3.0, 1), 33);
  // 100 / 8 = 12.5 → 13 (half away from zero).
  QCOMPARE(KeyboardHelpers::scaleRepeatInterval(100, 8.0, 1), 13);
}

// ------------------------------ computeArrowDirection ----------------------

namespace {
constexpr int kLeft = static_cast<int>(Qt::Key_Left);
constexpr int kRight = static_cast<int>(Qt::Key_Right);
constexpr int kUp = static_cast<int>(Qt::Key_Up);
constexpr int kDown = static_cast<int>(Qt::Key_Down);
} // namespace

void TestKeyboardHelpers::arrow_invalidWhenGridWidthZero() {
  const auto out = KeyboardHelpers::computeArrowDirection(kLeft, kLeft, kRight, kUp, kDown,
                                                           ViewType::Grid, 0);
  QVERIFY(!out.valid);
  const auto neg = KeyboardHelpers::computeArrowDirection(kLeft, kLeft, kRight, kUp, kDown,
                                                           ViewType::Grid, -3);
  QVERIFY(!neg.valid);
}

void TestKeyboardHelpers::arrow_unknownKeyReturnsInvalid() {
  const auto out = KeyboardHelpers::computeArrowDirection(static_cast<int>(Qt::Key_A), kLeft,
                                                           kRight, kUp, kDown, ViewType::Grid, 6);
  QVERIFY(!out.valid);
}

void TestKeyboardHelpers::arrow_gridLeftRightStepOne() {
  const auto l =
      KeyboardHelpers::computeArrowDirection(kLeft, kLeft, kRight, kUp, kDown, ViewType::Grid, 6);
  QVERIFY(l.valid);
  QCOMPARE(l.direction, -1);
  QVERIFY(!l.vertical);

  const auto r =
      KeyboardHelpers::computeArrowDirection(kRight, kLeft, kRight, kUp, kDown, ViewType::Grid, 6);
  QVERIFY(r.valid);
  QCOMPARE(r.direction, 1);
  QVERIFY(!r.vertical);
}

void TestKeyboardHelpers::arrow_gridUpDownStepGridWidth() {
  const auto u =
      KeyboardHelpers::computeArrowDirection(kUp, kLeft, kRight, kUp, kDown, ViewType::Grid, 6);
  QVERIFY(u.valid);
  QCOMPARE(u.direction, -6);
  QVERIFY(u.vertical);

  const auto d =
      KeyboardHelpers::computeArrowDirection(kDown, kLeft, kRight, kUp, kDown, ViewType::Grid, 6);
  QVERIFY(d.valid);
  QCOMPARE(d.direction, 6);
  QVERIFY(d.vertical);
}

void TestKeyboardHelpers::arrow_listAllAxesStepOne() {
  // List collapses Up/Down to ±1 (vertical=true), Left/Right stay ±1 (horizontal).
  const auto u =
      KeyboardHelpers::computeArrowDirection(kUp, kLeft, kRight, kUp, kDown, ViewType::List, 6);
  QCOMPARE(u.direction, -1);
  QVERIFY(u.vertical);

  const auto d =
      KeyboardHelpers::computeArrowDirection(kDown, kLeft, kRight, kUp, kDown, ViewType::List, 6);
  QCOMPARE(d.direction, 1);
  QVERIFY(d.vertical);

  const auto l =
      KeyboardHelpers::computeArrowDirection(kLeft, kLeft, kRight, kUp, kDown, ViewType::List, 6);
  QCOMPARE(l.direction, -1);
  QVERIFY(!l.vertical);
}

void TestKeyboardHelpers::arrow_coverFlowAllAxesStepOne() {
  const auto u = KeyboardHelpers::computeArrowDirection(kUp, kLeft, kRight, kUp, kDown,
                                                         ViewType::CoverFlow, 6);
  QCOMPARE(u.direction, -1);
  QVERIFY(u.vertical);
}

void TestKeyboardHelpers::arrow_horizontalLeftRightStepGridWidthVerticalTrue() {
  // In Horizontal view, Left/Right step ±gridWidth and the helper marks
  // them as vertical=true so the column-wrap path activates downstream.
  const auto l = KeyboardHelpers::computeArrowDirection(kLeft, kLeft, kRight, kUp, kDown,
                                                         ViewType::Horizontal, 4);
  QCOMPARE(l.direction, -4);
  QVERIFY(l.vertical);

  const auto r = KeyboardHelpers::computeArrowDirection(kRight, kLeft, kRight, kUp, kDown,
                                                         ViewType::Horizontal, 4);
  QCOMPARE(r.direction, 4);
  QVERIFY(r.vertical);
}

void TestKeyboardHelpers::arrow_horizontalUpDownStepOneVerticalFalse() {
  const auto u = KeyboardHelpers::computeArrowDirection(kUp, kLeft, kRight, kUp, kDown,
                                                         ViewType::Horizontal, 4);
  QCOMPARE(u.direction, -1);
  QVERIFY(!u.vertical);

  const auto d = KeyboardHelpers::computeArrowDirection(kDown, kLeft, kRight, kUp, kDown,
                                                         ViewType::Horizontal, 4);
  QCOMPARE(d.direction, 1);
  QVERIFY(!d.vertical);
}

void TestKeyboardHelpers::arrow_respectsRebinding() {
  // User remaps Left/Right to W/A. Pressing the Qt::Key_Left default should
  // NOT trigger when the configured key is something else.
  constexpr int kA = static_cast<int>(Qt::Key_A);
  constexpr int kD = static_cast<int>(Qt::Key_D);
  const auto remapped =
      KeyboardHelpers::computeArrowDirection(kA, kA, kD, kUp, kDown, ViewType::Grid, 6);
  QCOMPARE(remapped.direction, -1);
  QVERIFY(!remapped.vertical);

  // Default Left/Right do nothing because the helper compares against the
  // remapped values.
  const auto defaultMiss =
      KeyboardHelpers::computeArrowDirection(kLeft, kA, kD, kUp, kDown, ViewType::Grid, 6);
  QVERIFY(!defaultMiss.valid);
}

QTEST_APPLESS_MAIN(TestKeyboardHelpers)
#include "test_keyboardhelpers.moc"
