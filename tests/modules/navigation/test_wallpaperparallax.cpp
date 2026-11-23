// Image wallpaper parallax (Kartend-w0zi1).
//
// The wallpaper used to be a viewport stylesheet. Its `QWidget` rule also
// styled the scroll contents, which painted the image in its own coordinates
// and scrolled it 1:1 with the tiles, and QSS dropped the pixel offset meant
// for parallax, so the toggle and strength did nothing. These tests check the
// geometry, and then where the wallpaper actually lands in a rendered scroll
// area, since the old code had plausible maths and still showed no drift.
//
// The render tests compare a whole screen column against the expected
// placement rather than tracking one feature: the old code drew TWO copies
// (a fixed one on the viewport, a scrolling one on the contents), and at some
// scroll positions they line up, so a single marker can land in the right
// place for the wrong reason.
#include "backgroundimagewidget.h"
#include "collectionbackgroundcontroller.h"

#include <algorithm>
#include <QColor>
#include <QImage>
#include <QLineEdit>
#include <QList>
#include <QMenuBar>
#include <QRectF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizeF>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

#include "collection/collectionconfig.h"

namespace {

// The test wallpaper is a vertical green ramp, so every screen row says which
// wallpaper row it shows. It is smaller than the viewport, so drawing it at
// its pixel size (the old behaviour) leaves most of the view uncovered.
constexpr int kWallSize = 200;

qreal rampGreen(qreal wallRow) {
  return qBound(0.0, wallRow, kWallSize - 1.0) * 255.0 / (kWallSize - 1);
}

QString writeWallpaper(const QTemporaryDir &dir) {
  QImage wall(kWallSize, kWallSize, QImage::Format_RGB32);
  for (int y = 0; y < kWallSize; ++y) {
    const QColor c(0, qRound(rampGreen(y)), 128);
    for (int x = 0; x < kWallSize; ++x) {
      wall.setPixelColor(x, y, c);
    }
  }
  const QString path = dir.filePath(QStringLiteral("wall.png"));
  return wall.save(path) ? path : QString();
}

// Largest difference, in green levels, between the middle column of @p frame
// and the wallpaper drawn at @p target. A level is under two screen rows here,
// so a tolerance of a few levels still pins the image to a few pixels.
int worstRowError(const QImage &frame, const QRectF &target) {
  const int x = frame.width() / 2;
  const qreal scale = target.height() / kWallSize;
  int worst = 0;
  for (int y = 0; y < frame.height(); ++y) {
    const qreal wallRow = (y + 0.5 - target.y()) / scale - 0.5;
    const int expected = qRound(rampGreen(wallRow));
    worst = std::max(worst, qAbs(frame.pixelColor(x, y).green() - expected));
  }
  return worst;
}

} // namespace

class TestWallpaperParallax : public QObject {
  Q_OBJECT
private slots:
  void staticWallpaperIsCentredCoverFit();
  void driftSpansTheMarginFromTopToBottom();
  void wallpaperAlwaysCoversTheView();
  void inputsAreClamped();
  void emptySizesGiveAnEmptyRect();
  void scrollFractionOfARange();

  void parallaxWallpaperDriftsSlowerThanTheTiles();
  void wallpaperWithoutParallaxStaysPutWhileTheTilesScroll();
};

void TestWallpaperParallax::staticWallpaperIsCentredCoverFit() {
  // Same aspect: exactly fills the view.
  QCOMPARE(BackgroundImageWidget::paintRect(QSizeF(1920, 1080), QSizeF(1600, 900), 0, 0.0),
           QRectF(0, 0, 1600, 900));
  // A square image in a wide view is scaled to the width and centred
  // vertically, cropping top and bottom equally.
  QCOMPARE(BackgroundImageWidget::paintRect(QSizeF(1000, 1000), QSizeF(1600, 900), 0, 0.0),
           QRectF(0, -350, 1600, 1600));
  // With no drift the scroll position must not matter.
  QCOMPARE(BackgroundImageWidget::paintRect(QSizeF(1000, 1000), QSizeF(1600, 900), 0, 1.0),
           QRectF(0, -350, 1600, 1600));
}

void TestWallpaperParallax::driftSpansTheMarginFromTopToBottom() {
  // Strength 50 adds half the view's height: cover-fit to 1600x1350.
  const QSizeF image(1600, 900);
  const QSizeF view(1600, 900);
  QCOMPARE(BackgroundImageWidget::paintRect(image, view, 50, 0.0), QRectF(-400, 0, 2400, 1350));
  QCOMPARE(BackgroundImageWidget::paintRect(image, view, 50, 0.5), QRectF(-400, -225, 2400, 1350));
  // At the bottom of the list the image's bottom edge meets the view's.
  const QRectF bottom = BackgroundImageWidget::paintRect(image, view, 50, 1.0);
  QCOMPARE(bottom, QRectF(-400, -450, 2400, 1350));
  QCOMPARE(bottom.bottom(), view.height());
}

void TestWallpaperParallax::wallpaperAlwaysCoversTheView() {
  const QRectF viewRect(0, 0, 1343, 1000);
  const QList<QSizeF> images = {QSizeF(3840, 2160), QSizeF(1000, 1000), QSizeF(600, 1400),
                                QSizeF(5000, 800)};
  for (const QSizeF &image : images) {
    for (int strength = 0; strength <= 100; strength += 10) {
      for (const qreal t : {0.0, 0.3, 0.5, 1.0}) {
        const QRectF r = BackgroundImageWidget::paintRect(image, viewRect.size(), strength, t);
        QVERIFY2(r.contains(viewRect),
                 qPrintable(QStringLiteral("image %1x%2 strength %3 t %4 leaves a gap")
                                .arg(image.width())
                                .arg(image.height())
                                .arg(strength)
                                .arg(t)));
      }
    }
  }
}

void TestWallpaperParallax::inputsAreClamped() {
  const QSizeF image(1600, 900);
  const QSizeF view(1600, 900);
  QCOMPARE(BackgroundImageWidget::paintRect(image, view, 150, 0.5),
           BackgroundImageWidget::paintRect(image, view, 100, 0.5));
  QCOMPARE(BackgroundImageWidget::paintRect(image, view, -10, 0.5),
           BackgroundImageWidget::paintRect(image, view, 0, 0.5));
  QCOMPARE(BackgroundImageWidget::paintRect(image, view, 40, 2.0),
           BackgroundImageWidget::paintRect(image, view, 40, 1.0));
  QCOMPARE(BackgroundImageWidget::paintRect(image, view, 40, -1.0),
           BackgroundImageWidget::paintRect(image, view, 40, 0.0));

  BackgroundImageWidget widget;
  widget.setParallaxStrength(250);
  QCOMPARE(widget.parallaxStrength(), 100);
  widget.setParallaxStrength(-3);
  QCOMPARE(widget.parallaxStrength(), 0);
  widget.setScrollFraction(7.0);
  QCOMPARE(widget.scrollFraction(), 1.0);
}

void TestWallpaperParallax::emptySizesGiveAnEmptyRect() {
  QVERIFY(BackgroundImageWidget::paintRect(QSizeF(), QSizeF(100, 100), 30, 0.0).isEmpty());
  QVERIFY(BackgroundImageWidget::paintRect(QSizeF(100, 100), QSizeF(0, 100), 30, 0.0).isEmpty());
}

void TestWallpaperParallax::scrollFractionOfARange() {
  QCOMPARE(BackgroundImageWidget::scrollFractionOf(0, 0, 0), 0.0);    // cannot scroll
  QCOMPARE(BackgroundImageWidget::scrollFractionOf(10, 10, 10), 0.0); // cannot scroll
  QCOMPARE(BackgroundImageWidget::scrollFractionOf(0, 0, 200), 0.0);
  QCOMPARE(BackgroundImageWidget::scrollFractionOf(50, 0, 200), 0.25);
  QCOMPARE(BackgroundImageWidget::scrollFractionOf(200, 0, 200), 1.0);
  QCOMPARE(BackgroundImageWidget::scrollFractionOf(300, 0, 200), 1.0);
  QCOMPARE(BackgroundImageWidget::scrollFractionOf(-5, 0, 200), 0.0);
}

namespace {

// A scroll area shaped like the items page: a plain QWidget contents, much
// taller than the viewport, with the controller applying an image background.
struct ItemsPage {
  QScrollArea scrollArea;
  QWidget topBar;
  QMenuBar menubar;
  QLineEdit searchBar;
  QList<CollectionConfig> collections;
  int currentIndex = 0;
  CollectionBackgroundController controller;

  ItemsPage(const QString &wallpaper, bool parallax, int strength) {
    scrollArea.setWidgetResizable(true);
    scrollArea.resize(kWallSize, 300);
    auto *contents = new QWidget; // like itemScrollAreaWidgetContents
    contents->setMinimumHeight(1500);
    scrollArea.setWidget(contents);

    CollectionConfig collection;
    collection.name = QStringLiteral("Parallax");
    collection.background.backgroundType = BackgroundType::Image;
    collection.background.backgroundImage = wallpaper;
    collection.background.wallpaperParallax = parallax;
    collection.background.parallaxStrength = strength;
    collections.append(collection);

    CollectionBackgroundControllerSetup setup;
    setup.itemScrollArea = &scrollArea;
    setup.itemsTopBar = &topBar;
    setup.menubar = &menubar;
    setup.searchBar = &searchBar;
    setup.currentCollectionIndex = &currentIndex;
    setup.collections = &collections;
    controller.setupReferences(setup);

    scrollArea.show();
    controller.applyBackgroundForCollection(0);
    QCoreApplication::processEvents();
  }

  // Scroll the grid to @p value and capture what the viewport shows.
  QImage frameAt(int value) {
    scrollArea.verticalScrollBar()->setValue(value);
    QCoreApplication::processEvents();
    return scrollArea.viewport()->grab().toImage();
  }
};

} // namespace

void TestWallpaperParallax::parallaxWallpaperDriftsSlowerThanTheTiles() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString wallpaper = writeWallpaper(dir);
  QVERIFY(!wallpaper.isEmpty());

  ItemsPage page(wallpaper, true, 50);
  const int maxScroll = page.scrollArea.verticalScrollBar()->maximum();
  const QSizeF view = page.scrollArea.viewport()->size();
  QVERIFY2(maxScroll > 2 * view.height(), "the contents must scroll well past the drift");

  // The wallpaper lands where paintRect says along the whole list.
  for (const int value : {0, maxScroll / 3, maxScroll / 2, maxScroll}) {
    const qreal t = BackgroundImageWidget::scrollFractionOf(value, 0, maxScroll);
    const QRectF expected =
        BackgroundImageWidget::paintRect(QSizeF(kWallSize, kWallSize), view, 50, t);
    const int error = worstRowError(page.frameAt(value), expected);
    QVERIFY2(error <= 3, qPrintable(QStringLiteral("scroll %1 of %2: a row is %3 levels off")
                                        .arg(value)
                                        .arg(maxScroll)
                                        .arg(error)));
  }

  // And that placement is a drift of half the view's height over the whole
  // list, far less than the tiles travel. The stylesheet version moved its
  // scrolling copy the full maxScroll.
  const qreal drift =
      BackgroundImageWidget::paintRect(QSizeF(kWallSize, kWallSize), view, 50, 0).y() -
      BackgroundImageWidget::paintRect(QSizeF(kWallSize, kWallSize), view, 50, 1).y();
  QCOMPARE(qRound(drift), qRound(view.height() / 2));
  QVERIFY(drift < maxScroll);
}

void TestWallpaperParallax::wallpaperWithoutParallaxStaysPutWhileTheTilesScroll() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString wallpaper = writeWallpaper(dir);
  QVERIFY(!wallpaper.isEmpty());

  // The toggle off overrides any strength left in the config.
  ItemsPage page(wallpaper, false, 80);
  const int maxScroll = page.scrollArea.verticalScrollBar()->maximum();
  QVERIFY(maxScroll > 0);

  const QRectF still = BackgroundImageWidget::paintRect(QSizeF(kWallSize, kWallSize),
                                                        page.scrollArea.viewport()->size(), 0, 0);
  for (const int value : {0, maxScroll / 3, maxScroll / 2, maxScroll}) {
    const int error = worstRowError(page.frameAt(value), still);
    QVERIFY2(error <= 3, qPrintable(QStringLiteral("scroll %1 of %2: a row is %3 levels off")
                                        .arg(value)
                                        .arg(maxScroll)
                                        .arg(error)));
  }
}

QTEST_MAIN(TestWallpaperParallax)
#include "test_wallpaperparallax.moc"
