// Kartend-0eeuk: StarRatingWidget value semantics — the setRating clamp
// contract ([-1, 10] with -1 as the "unset" sentinel), change-only signal
// emission, and the click-to-value math in ratingAtPosition (left half =
// half-star, right half = full star, inter-star gap = previous star's full
// value, outside the strip = no-op). Mouse events are synthesized directly
// via QApplication::sendEvent so the widget never has to be shown; the
// geometry constants mirrored here (padding 2, star 22, spacing 4) are the
// widget's own layout constants — a change to either side must keep them in
// sync or these tests flag the drift.

#include "starratingwidget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>

namespace {

// Mirror of the widget's private layout constants. starLeft(i) is the x of
// star i's first pixel; the half/full boundary sits at starLeft(i) + 11.
constexpr int kPadding = 2;
constexpr int kStarSize = 22;
constexpr int kSpacing = 4;

int starLeft(int index) {
  return kPadding + index * (kStarSize + kSpacing);
}

void click(StarRatingWidget &w, int x, Qt::MouseButton button = Qt::LeftButton) {
  const QPointF pos(x, kPadding + kStarSize / 2);
  QMouseEvent press(QEvent::MouseButtonPress, pos, pos, button, button, Qt::NoModifier);
  QApplication::sendEvent(&w, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, pos, pos, button, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&w, &release);
}

} // namespace

class TestStarRatingWidget : public QObject {
  Q_OBJECT

private slots:
  void setRatingClampsIntoRange();
  void zeroIsDistinctFromUnset();
  void signalFiresOnlyOnActualChange();
  void leftHalfClickSetsHalfStar();
  void rightHalfClickSetsFullStar();
  void interStarGapClicksAsPreviousFullStar();
  void clickOutsideStripIsANoOp();
  void clickingSameValueTogglesToUnset();
  void rightClickClearsRating();
};

void TestStarRatingWidget::setRatingClampsIntoRange() {
  StarRatingWidget w;
  w.setRating(15);
  QCOMPARE(w.rating(), 10); // above range clamps to max

  w.setRating(-5);
  QCOMPARE(w.rating(), -1); // any negative collapses to the unset sentinel

  w.setRating(7);
  QCOMPARE(w.rating(), 7); // in-range passes through
}

void TestStarRatingWidget::zeroIsDistinctFromUnset() {
  StarRatingWidget w;
  w.setRating(0);
  // "User explicitly rated zero stars" must not collapse into "never rated".
  QCOMPARE(w.rating(), 0);
  w.setRating(-1);
  QCOMPARE(w.rating(), -1);
}

void TestStarRatingWidget::signalFiresOnlyOnActualChange() {
  StarRatingWidget w;
  QSignalSpy spy(&w, &StarRatingWidget::ratingChanged);

  w.setRating(6);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 6);

  w.setRating(6); // same value — no emission
  QCOMPARE(spy.count(), 0);

  w.setRating(12); // clamps to 10 — change, one emission with the clamped value
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 10);

  w.setRating(15); // also clamps to 10 — no change, no emission
  QCOMPARE(spy.count(), 0);
}

void TestStarRatingWidget::leftHalfClickSetsHalfStar() {
  StarRatingWidget w;
  click(w, starLeft(0) + 5); // left half of star 0
  QCOMPARE(w.rating(), 1);

  click(w, starLeft(2) + 5); // left half of star 2
  QCOMPARE(w.rating(), 5);

  click(w, starLeft(4) + 5); // left half of star 4
  QCOMPARE(w.rating(), 9);
}

void TestStarRatingWidget::rightHalfClickSetsFullStar() {
  StarRatingWidget w;
  click(w, starLeft(0) + 15); // right half of star 0
  QCOMPARE(w.rating(), 2);

  click(w, starLeft(2) + 15); // right half of star 2
  QCOMPARE(w.rating(), 6);

  click(w, starLeft(4) + 15); // right half of star 4
  QCOMPARE(w.rating(), 10);
}

void TestStarRatingWidget::interStarGapClicksAsPreviousFullStar() {
  StarRatingWidget w;
  // One pixel into the gap after star 0 — reads as star 0's full value.
  click(w, starLeft(0) + kStarSize + 1);
  QCOMPARE(w.rating(), 2);

  // Gap after star 3 — reads as star 3's full value.
  click(w, starLeft(3) + kStarSize + 1);
  QCOMPARE(w.rating(), 8);
}

void TestStarRatingWidget::clickOutsideStripIsANoOp() {
  StarRatingWidget w;
  w.setRating(4);

  click(w, 0); // left of the padding
  QCOMPARE(w.rating(), 4);

  click(w, starLeft(5) + 50); // far beyond the last star slot
  QCOMPARE(w.rating(), 4);
}

void TestStarRatingWidget::clickingSameValueTogglesToUnset() {
  StarRatingWidget w;
  const int x = starLeft(1) + 15; // full star 1 → rating 4
  click(w, x);
  QCOMPARE(w.rating(), 4);
  click(w, x); // same value again — toggles back to unset
  QCOMPARE(w.rating(), -1);
  click(w, x); // and a third click re-sets it
  QCOMPARE(w.rating(), 4);
}

void TestStarRatingWidget::rightClickClearsRating() {
  StarRatingWidget w;
  w.setRating(8);
  click(w, starLeft(0) + 5, Qt::RightButton); // position is irrelevant
  QCOMPARE(w.rating(), -1);
}

QTEST_MAIN(TestStarRatingWidget)
#include "test_starratingwidget.moc"
