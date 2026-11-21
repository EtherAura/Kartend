/**
 * @file test_durationformat.cpp
 * @brief Unit tests for DurationFormat::formatDurationMs — the compact
 * "Ns" / M:SS / H:MM:SS formatter behind the scrape dialogs' Elapsed/ETA and
 * runtime text. Pins the unit transitions, sub-second truncation, the
 * no-wrap-at-24-hours behaviour and the em dash for non-positive input.
 */

#include "durationformat.h"
#include <QTest>

class TestDurationFormat : public QObject {
  Q_OBJECT

private slots:
  void nonPositiveIsEmDash();
  void subSecondTruncatesToZeroSeconds();
  void secondsOnlyBelowOneMinute();
  void oneMinuteSwitchesToMinutesSeconds();
  void lastMinuteBeforeOneHour();
  void oneHourSwitchesToHoursMinutesSeconds();
  void hoursDoNotWrapAtOneDay();
};

void TestDurationFormat::nonPositiveIsEmDash() {
  QCOMPARE(DurationFormat::formatDurationMs(0), QStringLiteral("—"));
  QCOMPARE(DurationFormat::formatDurationMs(-1), QStringLiteral("—"));
}

void TestDurationFormat::subSecondTruncatesToZeroSeconds() {
  // 999 ms is positive, so no em dash, but whole seconds truncate to 0.
  QCOMPARE(DurationFormat::formatDurationMs(999), QStringLiteral("0s"));
}

void TestDurationFormat::secondsOnlyBelowOneMinute() {
  QCOMPARE(DurationFormat::formatDurationMs(1999), QStringLiteral("1s"));
  QCOMPARE(DurationFormat::formatDurationMs(59999), QStringLiteral("59s"));
}

void TestDurationFormat::oneMinuteSwitchesToMinutesSeconds() {
  QCOMPARE(DurationFormat::formatDurationMs(60000), QStringLiteral("1:00"));
  QCOMPARE(DurationFormat::formatDurationMs(61000), QStringLiteral("1:01"));
}

void TestDurationFormat::lastMinuteBeforeOneHour() {
  QCOMPARE(DurationFormat::formatDurationMs(3599999), QStringLiteral("59:59"));
}

void TestDurationFormat::oneHourSwitchesToHoursMinutesSeconds() {
  QCOMPARE(DurationFormat::formatDurationMs(3600000), QStringLiteral("1:00:00"));
  QCOMPARE(DurationFormat::formatDurationMs(3661000), QStringLiteral("1:01:01"));
}

void TestDurationFormat::hoursDoNotWrapAtOneDay() {
  // 25 hours stays in H:MM:SS; there is no day unit.
  QCOMPARE(DurationFormat::formatDurationMs(90000000), QStringLiteral("25:00:00"));
}

QTEST_GUILESS_MAIN(TestDurationFormat)
#include "test_durationformat.moc"
