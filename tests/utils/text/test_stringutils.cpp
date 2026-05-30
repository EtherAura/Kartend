/**
 * @file test_stringutils.cpp
 * @brief Unit tests for StringUtils helpers
 */

#include "stringutils.h"
#include <QTest>

class TestStringUtils : public QObject {
  Q_OBJECT

private slots:
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
};

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
  // Small negatives (size <= 3) are returned unchanged
  QCOMPARE(StringUtils::formatCountNumber(-1), QStringLiteral("-1"));
  QCOMPARE(StringUtils::formatCountNumber(-42), QStringLiteral("-42"));
}

void TestStringUtils::formatCountNumber_negativeThousands() {
  // -1000 (size 5): comma inserted at pos 2 -> "-1,000"
  QCOMPARE(StringUtils::formatCountNumber(-1000), QStringLiteral("-1,000"));
  // -1000000 (size 8): commas at pos 5 then pos 2 -> "-1,000,000"
  QCOMPARE(StringUtils::formatCountNumber(-1000000), QStringLiteral("-1,000,000"));
}

void TestStringUtils::formatCountNumber_largeQint64() {
  QCOMPARE(StringUtils::formatCountNumber(9223372036854775807LL),
           QStringLiteral("9,223,372,036,854,775,807"));
}

QTEST_APPLESS_MAIN(TestStringUtils)
#include "test_stringutils.moc"
