/**
 * @file test_stringutils.cpp
 * @brief Unit tests for StringUtils helpers
 */

#include "stringutils.h"
#include <QLocale>
#include <QTest>

class TestStringUtils : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void formatCountNumber_zero();
  void formatCountNumber_smallValues();
  void formatCountNumber_threeDigits();
  void formatCountNumber_fourDigits();
  void formatCountNumber_thousands();
  void formatCountNumber_millions();
  void formatCountNumber_billions();
  void formatCountNumber_negative();
  void formatCountNumber_negativeThousands();
  void formatCountNumber_largeQint64();
  void formatCountNumber_respectsLocale();
  void relativePastTime_neverAndJustNow();
  void relativePastTime_minutesHoursDays();
  void relativePastTime_weeksMonthsYears();
  void relativePastTime_singularVsPlural();
  void relativePastTime_futureClamps();

private:
  QLocale m_originalLocale;
};

void TestStringUtils::initTestCase() {
  // formatCountNumber now formats via the default QLocale, so pin a known one
  // (en_US) — its ',' grouping keeps the assertions below deterministic
  // regardless of the host / CI locale. Restored in cleanupTestCase.
  m_originalLocale = QLocale();
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void TestStringUtils::cleanupTestCase() {
  QLocale::setDefault(m_originalLocale);
}

void TestStringUtils::formatCountNumber_zero() {
  QCOMPARE(StringUtils::formatCountNumber(0), QStringLiteral("0"));
}

void TestStringUtils::formatCountNumber_smallValues() {
  QCOMPARE(StringUtils::formatCountNumber(1), QStringLiteral("1"));
  QCOMPARE(StringUtils::formatCountNumber(42), QStringLiteral("42"));
}

void TestStringUtils::formatCountNumber_threeDigits() {
  QCOMPARE(StringUtils::formatCountNumber(999), QStringLiteral("999"));
  QCOMPARE(StringUtils::formatCountNumber(100), QStringLiteral("100"));
}

void TestStringUtils::formatCountNumber_fourDigits() {
  QCOMPARE(StringUtils::formatCountNumber(1000), QStringLiteral("1,000"));
  QCOMPARE(StringUtils::formatCountNumber(9999), QStringLiteral("9,999"));
}

void TestStringUtils::formatCountNumber_thousands() {
  QCOMPARE(StringUtils::formatCountNumber(12345), QStringLiteral("12,345"));
  QCOMPARE(StringUtils::formatCountNumber(123456), QStringLiteral("123,456"));
}

void TestStringUtils::formatCountNumber_millions() {
  QCOMPARE(StringUtils::formatCountNumber(1234567), QStringLiteral("1,234,567"));
  QCOMPARE(StringUtils::formatCountNumber(10000000), QStringLiteral("10,000,000"));
}

void TestStringUtils::formatCountNumber_billions() {
  QCOMPARE(StringUtils::formatCountNumber(1000000000LL), QStringLiteral("1,000,000,000"));
}

void TestStringUtils::formatCountNumber_negative() {
  QCOMPARE(StringUtils::formatCountNumber(-1), QStringLiteral("-1"));
  QCOMPARE(StringUtils::formatCountNumber(-42), QStringLiteral("-42"));
  // Regression guard for the sign bug: the old hand-rolled grouping inserted
  // a separator right after the minus sign, yielding "-,123" (Kartend-ixrhn).
  QCOMPARE(StringUtils::formatCountNumber(-123), QStringLiteral("-123"));
}

void TestStringUtils::formatCountNumber_negativeThousands() {
  QCOMPARE(StringUtils::formatCountNumber(-1000), QStringLiteral("-1,000"));
  QCOMPARE(StringUtils::formatCountNumber(-1000000), QStringLiteral("-1,000,000"));
}

void TestStringUtils::formatCountNumber_largeQint64() {
  QCOMPARE(StringUtils::formatCountNumber(9223372036854775807LL),
           QStringLiteral("9,223,372,036,854,775,807"));
}

void TestStringUtils::formatCountNumber_respectsLocale() {
  // The fix delegates to QLocale, so the grouping separator follows the active
  // locale: a dot in de_DE, a comma in en_US. This is the whole point of the
  // change — the old code hardcoded ',' (Kartend-ixrhn).
  const QLocale previous = QLocale();
  QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
  QCOMPARE(StringUtils::formatCountNumber(1234567), QStringLiteral("1.234.567"));
  QLocale::setDefault(previous);
  QCOMPARE(StringUtils::formatCountNumber(1234567), QStringLiteral("1,234,567"));
}

namespace {
constexpr qint64 kNow = 1700000000000LL; // fixed "now" (2023-11) so cases stay deterministic
constexpr qint64 kSec = 1000LL;
constexpr qint64 kMin = 60 * kSec;
constexpr qint64 kHour = 60 * kMin;
constexpr qint64 kDay = 24 * kHour;
} // namespace

void TestStringUtils::relativePastTime_neverAndJustNow() {
  // 0 / negative stamp = "never"; anything under a minute = "just now".
  QCOMPARE(StringUtils::relativePastTime(0, kNow), QStringLiteral("never"));
  QCOMPARE(StringUtils::relativePastTime(-5, kNow), QStringLiteral("never"));
  QCOMPARE(StringUtils::relativePastTime(kNow, kNow), QStringLiteral("just now"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 59 * kSec, kNow), QStringLiteral("just now"));
}

void TestStringUtils::relativePastTime_minutesHoursDays() {
  QCOMPARE(StringUtils::relativePastTime(kNow - 5 * kMin, kNow), QStringLiteral("5 minutes ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 59 * kMin, kNow), QStringLiteral("59 minutes ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 3 * kHour, kNow), QStringLiteral("3 hours ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 23 * kHour, kNow), QStringLiteral("23 hours ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 6 * kDay, kNow), QStringLiteral("6 days ago"));
}

void TestStringUtils::relativePastTime_weeksMonthsYears() {
  QCOMPARE(StringUtils::relativePastTime(kNow - 14 * kDay, kNow), QStringLiteral("2 weeks ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 60 * kDay, kNow), QStringLiteral("2 months ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 800 * kDay, kNow), QStringLiteral("2 years ago"));
}

void TestStringUtils::relativePastTime_singularVsPlural() {
  // Exact boundaries land on the singular form (not "1 minute(s) ago").
  QCOMPARE(StringUtils::relativePastTime(kNow - kMin, kNow), QStringLiteral("1 minute ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - kHour, kNow), QStringLiteral("1 hour ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - kDay, kNow), QStringLiteral("1 day ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 7 * kDay, kNow), QStringLiteral("1 week ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 30 * kDay, kNow), QStringLiteral("1 month ago"));
  QCOMPARE(StringUtils::relativePastTime(kNow - 365 * kDay, kNow), QStringLiteral("1 year ago"));
}

void TestStringUtils::relativePastTime_futureClamps() {
  // A stamp ahead of "now" (clock skew) clamps to "just now" rather than going
  // negative.
  QCOMPARE(StringUtils::relativePastTime(kNow + 5 * kMin, kNow), QStringLiteral("just now"));
}

QTEST_APPLESS_MAIN(TestStringUtils)
#include "test_stringutils.moc"
