#include <QtTest/QtTest>

#include "textzoom.h"

// Direct tests for the process-wide text-zoom math: setPercent/primeFromSettings
// clamping and zoomedFontSize's edge cases. The zoom percent is process-global
// state, so each test sets the percent it needs and the suite restores the
// default at the end.
class TestTextZoom : public QObject {
  Q_OBJECT
private slots:
  void setPercent_clampsIntoRangeAndReturnsClamped();
  void primeFromSettings_clampsIntoRange();
  void zoomedFontSize_identityAt100AndNonPositiveBase();
  void zoomedFontSize_scalesAndFloorsToOne();
  void cleanupTestCase();
};

void TestTextZoom::setPercent_clampsIntoRangeAndReturnsClamped() {
  using namespace TextZoom;
  QCOMPARE(setPercent(MIN_PERCENT - 40), MIN_PERCENT); // below floor clamps up to MIN
  QCOMPARE(percent(), MIN_PERCENT);
  QCOMPARE(setPercent(MAX_PERCENT + 200), MAX_PERCENT); // above ceiling clamps down to MAX
  QCOMPARE(percent(), MAX_PERCENT);
  QCOMPARE(setPercent(125), 125); // in-range value passes through unchanged
  QCOMPARE(percent(), 125);
}

void TestTextZoom::primeFromSettings_clampsIntoRange() {
  using namespace TextZoom;
  primeFromSettings(9999);
  QCOMPARE(percent(), MAX_PERCENT);
  primeFromSettings(0);
  QCOMPARE(percent(), MIN_PERCENT);
}

void TestTextZoom::zoomedFontSize_identityAt100AndNonPositiveBase() {
  using namespace TextZoom;
  setPercent(100);
  QCOMPARE(zoomedFontSize(16), 16); // identity at 100%
  setPercent(200);
  QCOMPARE(zoomedFontSize(0), 0); // non-positive base passes through at any zoom
  QCOMPARE(zoomedFontSize(-5), -5);
}

void TestTextZoom::zoomedFontSize_scalesAndFloorsToOne() {
  using namespace TextZoom;
  setPercent(200);
  QCOMPARE(zoomedFontSize(16), 32); // 16 * 200 / 100
  setPercent(150);
  QCOMPARE(zoomedFontSize(16), 24); // 16 * 150 / 100
  setPercent(MIN_PERCENT);          // 50%: 1 * 50 / 100 == 0, floored to 1
  QCOMPARE(zoomedFontSize(1), 1);   // never returns 0
}

void TestTextZoom::cleanupTestCase() {
  TextZoom::setPercent(TextZoom::DEFAULT_PERCENT); // restore the process-wide default
}

QTEST_GUILESS_MAIN(TestTextZoom)
#include "test_textzoom.moc"
