/**
 * @file test_detailsformat.cpp
 * @brief Unit tests for the DetailsFormat pure formatters (utils/view).
 *
 * DetailsPane / DetailPageOverlay keep thin static wrappers over these for
 * downstream callers; this suite exercises the bodies directly so the
 * formatting contract is pinned without constructing any widget. Notably
 * covers formatPersonalRating, which the widget-level integration test
 * (test_detailspane_coverflow) never reached.
 */

#include "detailsformat.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTest>

class TestDetailsFormat : public QObject {
  Q_OBJECT

private slots:
  void formatFileSize_bytesAndUnits();
  void formatRuntime_negativeReturnsEmpty();
  void formatRuntime_unitBranches();
  void formatPersonalRating_unratedSentinelReturnsEmpty();
  void formatPersonalRating_fullStars();
  void formatPersonalRating_halfStarGlyphAndFraction();
  void formatPersonalRating_clampsOutOfRange();
  void formatTags_jsonArrayStripsBrackets();
  void formatTags_commaListTrimsAndSkipsEmpty();
  void formatTags_emptyStaysEmpty();
  void formatLastScanned_invalidReturnsNeverLabel();
  void formatLastScanned_validRendersDateTimeShape();
};

void TestDetailsFormat::formatFileSize_bytesAndUnits() {
  QCOMPARE(DetailsFormat::formatFileSize(0), QStringLiteral("0 bytes"));
  QCOMPARE(DetailsFormat::formatFileSize(1023), QStringLiteral("1023 bytes"));
  QCOMPARE(DetailsFormat::formatFileSize(1024), QStringLiteral("1.00 KB"));
  const qint64 mb = 1024LL * 1024LL;
  QCOMPARE(DetailsFormat::formatFileSize(mb * 5 / 2), QStringLiteral("2.50 MB"));
  QCOMPARE(DetailsFormat::formatFileSize(mb * 1024 * 3), QStringLiteral("3.00 GB"));
}

void TestDetailsFormat::formatRuntime_negativeReturnsEmpty() {
  QCOMPARE(DetailsFormat::formatRuntime(-1), QString());
  QCOMPARE(DetailsFormat::formatRuntime(-3600), QString());
}

void TestDetailsFormat::formatRuntime_unitBranches() {
  QCOMPARE(DetailsFormat::formatRuntime(0), QStringLiteral("0s"));
  QCOMPARE(DetailsFormat::formatRuntime(45), QStringLiteral("45s"));
  QCOMPARE(DetailsFormat::formatRuntime(125), QStringLiteral("2m 05s"));
  QCOMPARE(DetailsFormat::formatRuntime(3599), QStringLiteral("59m 59s"));
  QCOMPARE(DetailsFormat::formatRuntime(3600), QStringLiteral("1h 00m"));
  QCOMPARE(DetailsFormat::formatRuntime(7325), QStringLiteral("2h 02m"));
}

void TestDetailsFormat::formatPersonalRating_unratedSentinelReturnsEmpty() {
  // -1 is the "unrated" sentinel — the Details section skips the row.
  QCOMPARE(DetailsFormat::formatPersonalRating(-1), QString());
}

void TestDetailsFormat::formatPersonalRating_fullStars() {
  // 0-10 internal scale renders as five star glyphs (filled ★ / empty ☆)
  // plus "(n / 5)"; whole ratings drop the decimal in the fraction.
  QCOMPARE(DetailsFormat::formatPersonalRating(0), QStringLiteral("☆☆☆☆☆ (0 / 5)"));
  QCOMPARE(DetailsFormat::formatPersonalRating(6), QStringLiteral("★★★☆☆ (3 / 5)"));
  QCOMPARE(DetailsFormat::formatPersonalRating(10), QStringLiteral("★★★★★ (5 / 5)"));
}

void TestDetailsFormat::formatPersonalRating_halfStarGlyphAndFraction() {
  // Odd internal values are half stars: the ½ glyph sits in the half-star
  // slot and the fraction keeps one decimal.
  QCOMPARE(DetailsFormat::formatPersonalRating(7), QStringLiteral("★★★½☆ (3.5 / 5)"));
  QCOMPARE(DetailsFormat::formatPersonalRating(1), QStringLiteral("½☆☆☆☆ (0.5 / 5)"));
}

void TestDetailsFormat::formatPersonalRating_clampsOutOfRange() {
  // Values above the 0-10 scale clamp to the 5-star maximum.
  QCOMPARE(DetailsFormat::formatPersonalRating(15), DetailsFormat::formatPersonalRating(10));
}

void TestDetailsFormat::formatTags_jsonArrayStripsBrackets() {
  QCOMPARE(DetailsFormat::formatTags(QStringLiteral("[\"a\", \"b\", \"c\"]")),
           QStringLiteral("a, b, c"));
}

void TestDetailsFormat::formatTags_commaListTrimsAndSkipsEmpty() {
  QCOMPARE(DetailsFormat::formatTags(QStringLiteral(" foo ,bar,  baz ")),
           QStringLiteral("foo, bar, baz"));
  QCOMPARE(DetailsFormat::formatTags(QStringLiteral(",foo,,bar,")), QStringLiteral("foo, bar"));
}

void TestDetailsFormat::formatTags_emptyStaysEmpty() {
  QCOMPARE(DetailsFormat::formatTags(QString()), QString());
  QCOMPARE(DetailsFormat::formatTags(QStringLiteral("   ")), QString());
}

void TestDetailsFormat::formatLastScanned_invalidReturnsNeverLabel() {
  // No QCoreApplication/translators in this suite — translate() falls back
  // to the source text, which is exactly the untranslated contract.
  QCOMPARE(DetailsFormat::formatLastScanned(QDateTime()), QStringLiteral("never"));
}

void TestDetailsFormat::formatLastScanned_validRendersDateTimeShape() {
  // The render is local-time so the exact string is TZ-dependent; assert
  // the "yyyy-MM-dd HH:mm" shape instead of pinning a wall-clock value.
  const QDateTime dt = QDateTime::fromString(QStringLiteral("2026-05-07T14:23:00Z"), Qt::ISODate);
  QVERIFY(dt.isValid());
  const QString rendered = DetailsFormat::formatLastScanned(dt);
  QVERIFY2(
      rendered.contains(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}$"))),
      qPrintable(QStringLiteral("Unexpected format: ") + rendered));
}

QTEST_APPLESS_MAIN(TestDetailsFormat)
#include "test_detailsformat.moc"
