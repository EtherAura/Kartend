// Tests for ArtworkUtils::composeArtworkCard (Kartend-63wg).
//
// composeArtworkCard is the scale-to-fit + center-on-background + rounded-mask
// step the artwork worker now runs off the GUI thread (and ItemWidget reuses
// for its fallback). These lock the contract the worker and the GUI both rely
// on: physical output size + DPR, transparent rounded corners, opaque square
// corners, and a letterboxed background for non-matching aspect ratios.
#include "artworkutils.h"

#include <QColor>
#include <QImage>
#include <QTest>

class TestArtworkCompose : public QObject {
  Q_OBJECT
private slots:
  void cardHasPhysicalTargetSizeAndDpr();
  void nullSourceYieldsNullCard();
  void emptyTargetYieldsNullCard();
  void roundedCornersAreTransparent();
  void squareCornersAreOpaque();
  void letterboxUsesBackgroundColor();
  void wideWordmarkArtGetsSideMargins();
};

void TestArtworkCompose::cardHasPhysicalTargetSizeAndDpr() {
  QImage source(100, 100, QImage::Format_ARGB32);
  source.fill(Qt::red);

  // Logical 50x40 at dpr 2.0 -> 100x80 physical, tagged dpr 2.0 so it renders
  // crisp at the logical size.
  const QImage card = ArtworkUtils::composeArtworkCard(source, 50, 40, 2.0, /*radius=*/0, Qt::blue);
  QCOMPARE(card.size(), QSize(100, 80));
  QCOMPARE(card.devicePixelRatio(), 2.0);
}

void TestArtworkCompose::nullSourceYieldsNullCard() {
  const QImage card = ArtworkUtils::composeArtworkCard(QImage(), 50, 50, 1.0, 0, Qt::blue);
  QVERIFY(card.isNull());
}

void TestArtworkCompose::emptyTargetYieldsNullCard() {
  QImage source(10, 10, QImage::Format_ARGB32);
  source.fill(Qt::red);
  // Zero width -> nothing to render.
  QVERIFY(ArtworkUtils::composeArtworkCard(source, 0, 50, 1.0, 0, Qt::blue).isNull());
}

void TestArtworkCompose::roundedCornersAreTransparent() {
  QImage source(100, 100, QImage::Format_ARGB32);
  source.fill(Qt::red);

  const QImage card =
      ArtworkUtils::composeArtworkCard(source, 50, 50, 1.0, /*radius=*/12, Qt::blue);
  QCOMPARE(card.size(), QSize(50, 50));
  // The extreme corner sits outside the rounded rect — fully transparent.
  QCOMPARE(card.pixelColor(0, 0).alpha(), 0);
  // The center sits well inside — fully opaque.
  QCOMPARE(card.pixelColor(25, 25).alpha(), 255);
}

void TestArtworkCompose::squareCornersAreOpaque() {
  QImage source(100, 100, QImage::Format_ARGB32);
  source.fill(Qt::red);

  // radius 0 disables masking, so even the corner is opaque.
  const QImage card = ArtworkUtils::composeArtworkCard(source, 50, 50, 1.0, /*radius=*/0, Qt::blue);
  QCOMPARE(card.pixelColor(0, 0).alpha(), 255);
}

void TestArtworkCompose::letterboxUsesBackgroundColor() {
  // A wide source into a square target letterboxes top/bottom with the bg.
  QImage source(100, 50, QImage::Format_ARGB32);
  source.fill(Qt::red);

  const QImage card = ArtworkUtils::composeArtworkCard(source, 50, 50, 1.0, /*radius=*/0, Qt::blue);
  // KeepAspectRatio fits 100x50 into 50x50 -> 50x25, centered at y=12..36.
  // A pixel in the top letterbox band is the background; the center is artwork.
  QCOMPARE(card.pixelColor(25, 2), QColor(Qt::blue));
  QCOMPARE(card.pixelColor(25, 25), QColor(Qt::red));
}

void TestArtworkCompose::wideWordmarkArtGetsSideMargins() {
  // Kartend-5b5r1 user report ("SNES grid icon too wide"): art wider than
  // 2.5:1 — scraped platform wheels — fits into a narrower box so the
  // wordmark gets side margins instead of spanning edge-to-edge. Ordinary
  // aspect art keeps the full-width fit.
  QImage wordmark(600, 100, QImage::Format_ARGB32); // 6:1
  wordmark.fill(Qt::red);
  const QImage card = ArtworkUtils::composeArtworkCard(wordmark, 300, 400, 1.0, 0, Qt::black);
  QCOMPARE(card.size(), QSize(300, 400));
  // The red band must be inset: leftmost column at mid-height stays
  // background-black, and the band is at most 72% of the card wide.
  const int midY = 200;
  QCOMPARE(card.pixelColor(2, midY), QColor(Qt::black));
  int bandLeft = -1;
  int bandRight = -1;
  for (int x = 0; x < card.width(); ++x) {
    if (card.pixelColor(x, midY) == QColor(Qt::red)) {
      if (bandLeft < 0) bandLeft = x;
      bandRight = x;
    }
  }
  QVERIFY(bandLeft > 0);
  QVERIFY2(bandRight - bandLeft + 1 <= 300 * 0.72 + 2,
           qPrintable(QStringLiteral("band %1..%2").arg(bandLeft).arg(bandRight)));

  // A box cover (0.7:1) still fills the full width.
  QImage cover(280, 400, QImage::Format_ARGB32);
  cover.fill(Qt::green);
  const QImage coverCard = ArtworkUtils::composeArtworkCard(cover, 300, 400, 1.0, 0, Qt::black);
  bool touchesNearEdges = coverCard.pixelColor(15, 200) == QColor(Qt::green);
  QVERIFY(touchesNearEdges);
}

QTEST_MAIN(TestArtworkCompose)
#include "test_artworkcompose.moc"
