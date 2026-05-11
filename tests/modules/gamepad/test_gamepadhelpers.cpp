// Tests for GamepadHelpers — pure input math extracted from GamepadManager.
// The manager itself owns the Qt6::Gamepad / SDL2 backend lifecycle and the
// KeyboardManager-driven repeat dispatch; these are the decisions in
// between that don't need any of that to exercise.

#include <QTest>

#include "gamepadhelpers.h"

using GamepadHelpers::AxisDirectionState;
using GamepadHelpers::axisToDirections;
using GamepadHelpers::ButtonAction;
using GamepadHelpers::combineToDirection;
using GamepadHelpers::Direction;
using GamepadHelpers::MovementIntent;
using GamepadHelpers::movementFor;
using GamepadHelpers::normalizeSdlAxis;
using GamepadHelpers::resolveButtonAction;

class TestGamepadHelpers : public QObject {
  Q_OBJECT
private slots:
  // axisToDirections — basic on/off
  void axisToDirections_belowOnThresholdIsInactive();
  void axisToDirections_aboveOnThresholdActivates();
  void axisToDirections_useStickFalseSuppressesAll();

  // axisToDirections — hysteresis
  void axisToDirections_holdsActiveBetweenOnAndOffWhenPreviouslyActive();
  void axisToDirections_releasesBelowOffThreshold();
  void axisToDirections_hysteresisDoesNotLeakAcrossDirections();

  // combineToDirection
  void combineToDirection_verticalWinsOverHorizontal();
  void combineToDirection_dpadAndStickBothContribute();
  void combineToDirection_useDpadFalseSuppressesDpadBits();
  void combineToDirection_noInputReturnsNone();
  void combineToDirection_upBeatsDown();

  // movementFor
  void movementFor_directionsMapToExpectedIntents();
  void movementFor_noneIsZeroDelta();

  // normalizeSdlAxis
  void normalizeSdlAxis_zeroIsZero();
  void normalizeSdlAxis_positiveMaxIsOne();
  void normalizeSdlAxis_negativeMaxIsMinusOne();
  void normalizeSdlAxis_asymmetricRangeHandledCorrectly();

  // resolveButtonAction
  void resolveButtonAction_caseInsensitiveMatch();
  void resolveButtonAction_confirmPriorityOverBack();
  void resolveButtonAction_backPriorityOverToggle();
  void resolveButtonAction_unboundButtonReturnsNone();
  void resolveButtonAction_emptyConfiguredBindingsIgnored();
  void resolveButtonAction_whitespaceButtonNameIsNone();
};

// --------------------------- axisToDirections (basic) -----------------------

void TestGamepadHelpers::axisToDirections_belowOnThresholdIsInactive() {
  // axisX=0.4 < kOn=0.5 → no activation when not previously active.
  const auto s = axisToDirections(/*x=*/0.4, /*y=*/0.0, Direction::None, /*on=*/0.5,
                                   /*off=*/0.3, /*useStick=*/true);
  QVERIFY(!s.right);
  QVERIFY(!s.left);
}

void TestGamepadHelpers::axisToDirections_aboveOnThresholdActivates() {
  const auto s = axisToDirections(0.6, 0.0, Direction::None, 0.5, 0.3, true);
  QVERIFY(s.right);
  QVERIFY(!s.left);
}

void TestGamepadHelpers::axisToDirections_useStickFalseSuppressesAll() {
  // Even at full deflection, useStick=false zeroes the result.
  const auto s = axisToDirections(1.0, -1.0, Direction::None, 0.5, 0.3, /*useStick=*/false);
  QVERIFY(!s.right);
  QVERIFY(!s.up);
}

// --------------------------- axisToDirections (hysteresis) ------------------

void TestGamepadHelpers::axisToDirections_holdsActiveBetweenOnAndOffWhenPreviouslyActive() {
  // axisX=0.4 between off(0.3) and on(0.5) — previously Right should still
  // be active (held by the lower release threshold).
  const auto s = axisToDirections(0.4, 0.0, Direction::Right, 0.5, 0.3, true);
  QVERIFY(s.right);
}

void TestGamepadHelpers::axisToDirections_releasesBelowOffThreshold() {
  // Drops below the off threshold → release even though previously active.
  const auto s = axisToDirections(0.2, 0.0, Direction::Right, 0.5, 0.3, true);
  QVERIFY(!s.right);
}

void TestGamepadHelpers::axisToDirections_hysteresisDoesNotLeakAcrossDirections() {
  // Previously Up shouldn't extend the active range for Right.
  const auto s = axisToDirections(0.4, 0.0, Direction::Up, 0.5, 0.3, true);
  QVERIFY(!s.right);
}

// --------------------------- combineToDirection -----------------------------

void TestGamepadHelpers::combineToDirection_verticalWinsOverHorizontal() {
  // Up+Right pressed simultaneously → Up wins (matches grid-nav feel).
  AxisDirectionState stick;
  stick.up = true;
  stick.right = true;
  QCOMPARE(combineToDirection(false, false, false, false, true, stick), Direction::Up);
}

void TestGamepadHelpers::combineToDirection_dpadAndStickBothContribute() {
  // D-pad-right alone is enough; stick is empty.
  QCOMPARE(combineToDirection(false, true, false, false, true, AxisDirectionState{}),
           Direction::Right);
  // Stick-right alone is also enough; d-pad is empty.
  AxisDirectionState stick;
  stick.right = true;
  QCOMPARE(combineToDirection(false, false, false, false, true, stick), Direction::Right);
}

void TestGamepadHelpers::combineToDirection_useDpadFalseSuppressesDpadBits() {
  // useDpad=false makes the d-pad bits invisible even if the user is
  // pressing right on the d-pad.
  QCOMPARE(combineToDirection(false, true, false, false, /*useDpad=*/false, AxisDirectionState{}),
           Direction::None);
}

void TestGamepadHelpers::combineToDirection_noInputReturnsNone() {
  QCOMPARE(combineToDirection(false, false, false, false, true, AxisDirectionState{}),
           Direction::None);
}

void TestGamepadHelpers::combineToDirection_upBeatsDown() {
  // If both up and down somehow set (snap-back transient), up wins
  // because it's checked first.
  AxisDirectionState stick;
  stick.up = true;
  stick.down = true;
  QCOMPARE(combineToDirection(false, false, false, false, true, stick), Direction::Up);
}

// --------------------------- movementFor ------------------------------------

void TestGamepadHelpers::movementFor_directionsMapToExpectedIntents() {
  QCOMPARE(movementFor(Direction::Left).delta, -1);
  QCOMPARE(movementFor(Direction::Left).vertical, false);
  QCOMPARE(movementFor(Direction::Right).delta, 1);
  QCOMPARE(movementFor(Direction::Right).vertical, false);
  QCOMPARE(movementFor(Direction::Up).delta, -1);
  QCOMPARE(movementFor(Direction::Up).vertical, true);
  QCOMPARE(movementFor(Direction::Down).delta, 1);
  QCOMPARE(movementFor(Direction::Down).vertical, true);
}

void TestGamepadHelpers::movementFor_noneIsZeroDelta() {
  QCOMPARE(movementFor(Direction::None).delta, 0);
}

// --------------------------- normalizeSdlAxis -------------------------------

void TestGamepadHelpers::normalizeSdlAxis_zeroIsZero() {
  QCOMPARE(normalizeSdlAxis(0), 0.0);
}

void TestGamepadHelpers::normalizeSdlAxis_positiveMaxIsOne() {
  // Sint16 max = 32767.
  QCOMPARE(normalizeSdlAxis(32767), 1.0);
}

void TestGamepadHelpers::normalizeSdlAxis_negativeMaxIsMinusOne() {
  // Sint16 min = -32768.
  QCOMPARE(normalizeSdlAxis(-32768), -1.0);
}

void TestGamepadHelpers::normalizeSdlAxis_asymmetricRangeHandledCorrectly() {
  // -32767 should be slightly above -1.0 (-32767/32768), because the
  // negative path divides by 32768 (not 32767). This guards against a
  // future "simplification" that uses 32767 for both signs and produces
  // -1.00003... for the negative max.
  const double v = normalizeSdlAxis(-32767);
  QVERIFY(v > -1.0);
  QVERIFY(v < -0.999);
}

// --------------------------- resolveButtonAction ----------------------------

void TestGamepadHelpers::resolveButtonAction_caseInsensitiveMatch() {
  QCOMPARE(resolveButtonAction(QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("B"),
                                QStringLiteral("Y")),
           ButtonAction::Confirm);
  QCOMPARE(resolveButtonAction(QStringLiteral("BACK"), QStringLiteral("A"), QStringLiteral("back"),
                                QStringLiteral("Y")),
           ButtonAction::Back);
}

void TestGamepadHelpers::resolveButtonAction_confirmPriorityOverBack() {
  // Same physical button bound to both — confirm wins.
  QCOMPARE(resolveButtonAction(QStringLiteral("A"), QStringLiteral("A"), QStringLiteral("A"),
                                QStringLiteral("Y")),
           ButtonAction::Confirm);
}

void TestGamepadHelpers::resolveButtonAction_backPriorityOverToggle() {
  // Same physical button bound to back and toggle — back wins.
  QCOMPARE(resolveButtonAction(QStringLiteral("B"), QStringLiteral("A"), QStringLiteral("B"),
                                QStringLiteral("B")),
           ButtonAction::Back);
}

void TestGamepadHelpers::resolveButtonAction_unboundButtonReturnsNone() {
  // No binding matches "Start" → silently ignored.
  QCOMPARE(resolveButtonAction(QStringLiteral("Start"), QStringLiteral("A"), QStringLiteral("B"),
                                QStringLiteral("Y")),
           ButtonAction::None);
}

void TestGamepadHelpers::resolveButtonAction_emptyConfiguredBindingsIgnored() {
  // An empty configured binding shouldn't match an empty incoming button
  // (the incoming side is also trimmed first). Defensive: prevents a
  // misconfigured "" binding from greedy-matching every press.
  QCOMPARE(resolveButtonAction(QStringLiteral("A"), QString(), QString(), QString()),
           ButtonAction::None);
}

void TestGamepadHelpers::resolveButtonAction_whitespaceButtonNameIsNone() {
  QCOMPARE(resolveButtonAction(QStringLiteral("   "), QStringLiteral("A"), QStringLiteral("B"),
                                QStringLiteral("Y")),
           ButtonAction::None);
}

QTEST_APPLESS_MAIN(TestGamepadHelpers)
#include "test_gamepadhelpers.moc"
