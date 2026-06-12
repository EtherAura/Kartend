#include <QFrame>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QtTest/QtTest>
#include <QWidget>

#include "adaptivebatcher.h"
#include "itemwidget.h"
#include "viewportartworkscheduler.h"

// Direct tests for the viewport-prioritization math extracted from
// ViewportArtworkScheduler's anonymous namespace (Kartend-nzjxm).
class TestViewportSchedulerMath : public QObject {
  Q_OBJECT
private slots:
  void determineBatchSize_overrideWinsThenAdaptiveThenHalved();
  void partitionByViewport_bandsItemsByGeometry();
  void partitionByViewport_skipsNullAndLoadedWidgets();
  void computeViewports_translatesAndExpandsByOneViewport();
};

void TestViewportSchedulerMath::determineBatchSize_overrideWinsThenAdaptiveThenHalved() {
  AdaptiveBatcher batcher; // defaults(): currentBatchSize() == 10
  using S = ViewportArtworkScheduler;

  // A positive caller override always wins, regardless of priority.
  QCOMPARE(S::determineBatchSize(true, 7, batcher), 7);
  QCOMPARE(S::determineBatchSize(false, 7, batcher), 7);

  // No override: high priority uses the full adaptive size; low priority halves
  // it to leave foreground headroom.
  QCOMPARE(S::determineBatchSize(true, 0, batcher), batcher.currentBatchSize());
  QCOMPARE(S::determineBatchSize(false, 0, batcher), qMax(2, batcher.currentBatchSize() / 2)); // 5

  // Low-priority never drops below 2 even when the adaptive size is tiny.
  AdaptiveBatcher tiny(AdaptiveBatcher::Config{2, 1, 4, 50, 0.3, 5}); // currentBatchSize 2
  QCOMPARE(S::determineBatchSize(false, 0, tiny), 2);                 // qMax(2, 2/2) == 2
}

void TestViewportSchedulerMath::partitionByViewport_bandsItemsByGeometry() {
  QWidget grid;
  grid.resize(1000, 6000);
  const auto makeItem = [&grid](int x, int y) {
    auto *iw = new ItemWidget(&grid);
    iw->setGeometry(x, y, 100, 100); // mapTo(grid) == (x, y); no layout needed
    return iw;
  };
  ItemWidget *onscreen = makeItem(10, 10);  // inside immediate
  ItemWidget *overscan = makeItem(10, 700); // outside immediate, inside extended
  ItemWidget *far = makeItem(10, 5000);     // outside both

  ViewportArtworkScheduler::Viewports vps;
  vps.immediate = QRect(0, 0, 500, 500);
  vps.extended = QRect(-500, -500, 1500, 1500); // immediate +/- one viewport on each side

  const auto info = [](ItemWidget *w) {
    ArtworkInfo i;
    i.mediaItem = w;
    return i;
  };
  QList<ArtworkInfo> pending{info(onscreen), info(overscan), info(far)};

  const auto [imm, ext, rem] = ViewportArtworkScheduler::partitionByViewport(
      pending, &grid, vps, [](ItemWidget *) { return false; });

  QCOMPARE(imm.size(), 1);
  QCOMPARE(imm.first().mediaItem.data(), onscreen);
  QCOMPARE(ext.size(), 1);
  QCOMPARE(ext.first().mediaItem.data(), overscan);
  QCOMPARE(rem.size(), 1);
  QCOMPARE(rem.first().mediaItem.data(), far);
}

void TestViewportSchedulerMath::partitionByViewport_skipsNullAndLoadedWidgets() {
  QWidget grid;
  grid.resize(1000, 1000);
  auto *loaded = new ItemWidget(&grid);
  loaded->setGeometry(10, 10, 100, 100); // would land in immediate, but is "loaded"

  ViewportArtworkScheduler::Viewports vps;
  vps.immediate = QRect(0, 0, 500, 500);
  vps.extended = QRect(-500, -500, 1500, 1500);

  ArtworkInfo nullInfo; // mediaItem is null
  ArtworkInfo loadedInfo;
  loadedInfo.mediaItem = loaded;
  QList<ArtworkInfo> pending{nullInfo, loadedInfo};

  const auto [imm, ext, rem] = ViewportArtworkScheduler::partitionByViewport(
      pending, &grid, vps, [loaded](ItemWidget *w) { return w == loaded; });

  QVERIFY2(imm.isEmpty() && ext.isEmpty() && rem.isEmpty(),
           "null and already-loaded widgets must be skipped entirely");
}

void TestViewportSchedulerMath::computeViewports_translatesAndExpandsByOneViewport() {
  QScrollArea sa;
  sa.setFrameShape(QFrame::NoFrame);
  sa.resize(400, 300);
  auto *content = new QWidget;
  content->resize(3000, 3000); // larger than the viewport so the bars have range
  sa.setWidget(content);
  sa.show();
  QApplication::processEvents();

  const QRect vp = sa.viewport()->rect();
  if (vp.isEmpty()) {
    QSKIP("scroll-area viewport did not lay out in this environment");
  }
  sa.horizontalScrollBar()->setValue(40);
  sa.verticalScrollBar()->setValue(60);
  const QPoint off(sa.horizontalScrollBar()->value(), sa.verticalScrollBar()->value());

  const auto vps = ViewportArtworkScheduler::computeViewports(&sa);
  QCOMPARE(vps.immediate, vp.translated(off));
  QCOMPARE(vps.extended.width(), 3 * vp.width());
  QCOMPARE(vps.extended.height(), 3 * vp.height());
  QCOMPARE(vps.extended.x(), vps.immediate.x() - vp.width());
  QCOMPARE(vps.extended.y(), vps.immediate.y() - vp.height());
  QVERIFY(vps.extended.contains(vps.immediate));
}

QTEST_MAIN(TestViewportSchedulerMath)
#include "test_viewportschedulermath.moc"
