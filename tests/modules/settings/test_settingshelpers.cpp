// Tests for SettingsHelpers — pure helpers extracted from SettingsManager.

#include <Qt>
#include <QTest>

#include "collectiontypes.h"
#include "settingshelpers.h"

class TestSettingsHelpers : public QObject {
  Q_OBJECT

private slots:
  // coerceArtworkCycleModifier
  void coerceModifier_acceptsShift();
  void coerceModifier_acceptsControl();
  void coerceModifier_acceptsAlt();
  void coerceModifier_acceptsMeta();
  void coerceModifier_zeroFallsBackToShift();
  void coerceModifier_negativeFallsBackToShift();
  void coerceModifier_compoundBitmaskFallsBackToShift();
  void coerceModifier_nonModifierKeyFallsBackToShift();

  // coerceSortMode
  void coerceSortMode_acceptsNameAscending();
  void coerceSortMode_acceptsAllValidValues();
  void coerceSortMode_negativeFallsBackToNameAscending();
  void coerceSortMode_aboveMaxFallsBackToNameAscending();
};

// ------------------------------ coerceArtworkCycleModifier -----------------

void TestSettingsHelpers::coerceModifier_acceptsShift() {
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(static_cast<int>(Qt::ShiftModifier)),
           static_cast<int>(Qt::ShiftModifier));
}

void TestSettingsHelpers::coerceModifier_acceptsControl() {
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(static_cast<int>(Qt::ControlModifier)),
           static_cast<int>(Qt::ControlModifier));
}

void TestSettingsHelpers::coerceModifier_acceptsAlt() {
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(static_cast<int>(Qt::AltModifier)),
           static_cast<int>(Qt::AltModifier));
}

void TestSettingsHelpers::coerceModifier_acceptsMeta() {
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(static_cast<int>(Qt::MetaModifier)),
           static_cast<int>(Qt::MetaModifier));
}

void TestSettingsHelpers::coerceModifier_zeroFallsBackToShift() {
  // 0 == Qt::NoModifier — explicitly disallowed by the production switch.
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(0),
           static_cast<int>(Qt::ShiftModifier));
}

void TestSettingsHelpers::coerceModifier_negativeFallsBackToShift() {
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(-1),
           static_cast<int>(Qt::ShiftModifier));
}

void TestSettingsHelpers::coerceModifier_compoundBitmaskFallsBackToShift() {
  // Shift|Control would equal a different integer than either single modifier;
  // the helper must reject compound masks instead of pretending it's "ok-ish".
  const int compound =
      static_cast<int>(Qt::ShiftModifier) | static_cast<int>(Qt::ControlModifier);
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(compound),
           static_cast<int>(Qt::ShiftModifier));
}

void TestSettingsHelpers::coerceModifier_nonModifierKeyFallsBackToShift() {
  // A Qt::Key value (large int, not a KeyboardModifier flag).
  QCOMPARE(SettingsHelpers::coerceArtworkCycleModifier(static_cast<int>(Qt::Key_A)),
           static_cast<int>(Qt::ShiftModifier));
}

// ------------------------------ coerceSortMode -----------------------------

void TestSettingsHelpers::coerceSortMode_acceptsNameAscending() {
  QCOMPARE(SettingsHelpers::coerceSortMode(static_cast<int>(SortMode::NameAscending)),
           SortMode::NameAscending);
}

void TestSettingsHelpers::coerceSortMode_acceptsAllValidValues() {
  // Walk the closed range used by the production check.
  for (int raw = static_cast<int>(SortMode::NameAscending);
       raw <= static_cast<int>(SortMode::SizeAscending); ++raw) {
    QCOMPARE(SettingsHelpers::coerceSortMode(raw), static_cast<SortMode>(raw));
  }
}

void TestSettingsHelpers::coerceSortMode_negativeFallsBackToNameAscending() {
  QCOMPARE(SettingsHelpers::coerceSortMode(-1), SortMode::NameAscending);
  QCOMPARE(SettingsHelpers::coerceSortMode(-100), SortMode::NameAscending);
}

void TestSettingsHelpers::coerceSortMode_aboveMaxFallsBackToNameAscending() {
  // A value past SizeAscending — e.g. someone hand-edited or a future enum
  // value rolled back. Helper must not silently coerce to garbage.
  QCOMPARE(SettingsHelpers::coerceSortMode(static_cast<int>(SortMode::SizeAscending) + 1),
           SortMode::NameAscending);
  QCOMPARE(SettingsHelpers::coerceSortMode(99), SortMode::NameAscending);
}

QTEST_APPLESS_MAIN(TestSettingsHelpers)
#include "test_settingshelpers.moc"
