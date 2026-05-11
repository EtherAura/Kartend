// Tests for OverlayHelpers — pure helpers extracted from
// SelectionOverlayManager. The manager itself owns QWidget +
// QPropertyAnimation graphs that can't be exercised in isolation; the
// geometry math and the glide-duration policy live here.

#include <QPoint>
#include <QRect>
#include <QTest>

#include "overlayhelpers.h"

using OverlayHelpers::chooseGlideStart;
using OverlayHelpers::glideDurationMs;
using OverlayHelpers::overlayRectForPosition;

class TestOverlayHelpers : public QObject {
  Q_OBJECT
private slots:
  // glideDurationMs
  void glideDuration_zeroDistanceClampsToMin();
  void glideDuration_longDistanceClampsToMax();
  void glideDuration_midRangeValueIsExpected();
  void glideDuration_pythagoreanDiagonal();
  void glideDuration_negativeOrZeroSpeedReturnsMax();
  void glideDuration_doesNotReturnBelowMin();

  // chooseGlideStart
  void chooseStart_visibleAndValidStartUsesIt();
  void chooseStart_invisibleFallsBackToTarget();
  void chooseStart_invalidStartFallsBackToTarget();
  void chooseStart_invisibleEvenWithValidStartFallsBack();

  // overlayRectForPosition
  void overlayRectForPosition_insetsAllSides();
  void overlayRectForPosition_zeroInsetReturnsExactRect();
  void overlayRectForPosition_negativePositionStillExpands();
};

// --------------------------- glideDurationMs --------------------------------

void TestOverlayHelpers::glideDuration_zeroDistanceClampsToMin() {
  // Coincident centers → 0 distance → 0 ms before clamp; clamp lifts it
  // to MIN_DURATION so the overlay still has a visible (brief) glide.
  QCOMPARE(glideDurationMs(QPoint(100, 100), QPoint(100, 100), 1000.0, 50, 300), 50);
}

void TestOverlayHelpers::glideDuration_longDistanceClampsToMax() {
  // 10000 px at 1000 px/s = 10000 ms before clamp; max wins.
  QCOMPARE(glideDurationMs(QPoint(0, 0), QPoint(10000, 0), 1000.0, 50, 300), 300);
}

void TestOverlayHelpers::glideDuration_midRangeValueIsExpected() {
  // 200 px at 1000 px/s = 200 ms; sits inside [50, 300].
  QCOMPARE(glideDurationMs(QPoint(0, 0), QPoint(200, 0), 1000.0, 50, 300), 200);
}

void TestOverlayHelpers::glideDuration_pythagoreanDiagonal() {
  // 3-4-5 triangle: 5 px at 1000 px/s = 5 ms before clamp; clamp to min.
  QCOMPARE(glideDurationMs(QPoint(0, 0), QPoint(3, 4), 1000.0, 50, 300), 50);
}

void TestOverlayHelpers::glideDuration_negativeOrZeroSpeedReturnsMax() {
  // Defensive: a misconfigured constant shouldn't produce a divide-by-zero
  // or negative duration. Max is the most-conservative fallback.
  QCOMPARE(glideDurationMs(QPoint(0, 0), QPoint(100, 0), 0.0, 50, 300), 300);
  QCOMPARE(glideDurationMs(QPoint(0, 0), QPoint(100, 0), -10.0, 50, 300), 300);
}

void TestOverlayHelpers::glideDuration_doesNotReturnBelowMin() {
  // Very small distance + very high speed could round to 0; clamp must
  // catch it so the animation duration is never set to 0 (which would
  // jump-cut instead of glide).
  QCOMPARE(glideDurationMs(QPoint(0, 0), QPoint(1, 0), 1000000.0, 50, 300), 50);
}

// --------------------------- chooseGlideStart -------------------------------

void TestOverlayHelpers::chooseStart_visibleAndValidStartUsesIt() {
  const QRect start(10, 10, 100, 100);
  const QRect target(200, 200, 100, 100);
  QCOMPARE(chooseGlideStart(start, target, /*overlayVisible=*/true), start);
}

void TestOverlayHelpers::chooseStart_invisibleFallsBackToTarget() {
  // First-show case: no need to animate from (0,0) or wherever the
  // overlay was hiding — just appear at target.
  const QRect start(10, 10, 100, 100);
  const QRect target(200, 200, 100, 100);
  QCOMPARE(chooseGlideStart(start, target, /*overlayVisible=*/false), target);
}

void TestOverlayHelpers::chooseStart_invalidStartFallsBackToTarget() {
  // Caller passed an empty/default rect — defensive fallback to target
  // so we don't start the glide from a degenerate position.
  QCOMPARE(chooseGlideStart(QRect(), QRect(50, 50, 80, 80), /*overlayVisible=*/true),
           QRect(50, 50, 80, 80));
}

void TestOverlayHelpers::chooseStart_invisibleEvenWithValidStartFallsBack() {
  // Visibility wins over rect validity: a hidden overlay can't visibly
  // glide from anywhere, so jump to target.
  const QRect start(10, 10, 100, 100);
  const QRect target(200, 200, 100, 100);
  QCOMPARE(chooseGlideStart(start, target, /*overlayVisible=*/false), target);
}

// --------------------------- overlayRectForPosition -------------------------

void TestOverlayHelpers::overlayRectForPosition_insetsAllSides() {
  // 100×80 item at (50, 60) with 4-px inset → top-left shifts to (46, 56)
  // and size grows by 2*4 on each axis.
  const auto r = overlayRectForPosition(QPoint(50, 60), 100, 80, 4);
  QCOMPARE(r, QRect(46, 56, 108, 88));
}

void TestOverlayHelpers::overlayRectForPosition_zeroInsetReturnsExactRect() {
  QCOMPARE(overlayRectForPosition(QPoint(10, 20), 30, 40, 0), QRect(10, 20, 30, 40));
}

void TestOverlayHelpers::overlayRectForPosition_negativePositionStillExpands() {
  // Off-screen item (scrolled out of view) should still produce a valid
  // rect — the caller decides whether to render it.
  const auto r = overlayRectForPosition(QPoint(-10, -20), 50, 50, 5);
  QCOMPARE(r, QRect(-15, -25, 60, 60));
}

QTEST_APPLESS_MAIN(TestOverlayHelpers)
#include "test_overlayhelpers.moc"
