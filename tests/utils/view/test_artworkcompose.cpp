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

QTEST_MAIN(TestArtworkCompose)
#include "test_artworkcompose.moc"
