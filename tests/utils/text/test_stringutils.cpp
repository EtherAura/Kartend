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

QTEST_APPLESS_MAIN(TestStringUtils)
#include "test_stringutils.moc"
