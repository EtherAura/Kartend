#include "test_collectiontreepanel.h"

#include "collection/collectionconfig.h"
#include "collectiontreecontroller.h"
#include "mainwindow.h"
#include "mocks/mockedmainwindowfixture.h"

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWidget>

void TestCollectionTreePanel::foldMarker_appearsWhenTreeHidden_andClickRestores() {
  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  CollectionConfig child;
  child.name = QStringLiteral("Child");
  KartendTest::MockedMainWindowFixture fixture({shell, child});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));

  auto *controller = win->findChild<CollectionTreeController *>();
  QVERIFY2(controller, "MainWindow must own a CollectionTreeController");
  QWidget *panel = win->findChild<QWidget *>(QStringLiteral("collectionTreePanel"));
  auto *marker = win->findChild<QToolButton *>(QStringLiteral("collectionTreeFoldMarker"));
  QVERIFY(panel);
  QVERIFY2(marker, "setupPanel must create the fold marker");

  // Normalise to a visible tree first — the seeded collections use struct
  // defaults (treeVisible=true), but the root view keeps whatever live
  // state startup left.
  if (!panel->isVisible()) {
    controller->toggleVisible();
  }
  QVERIFY(panel->isVisible());
  QVERIFY2(!marker->isVisible(), "marker must stay hidden while the panel shows");

  controller->toggleVisible();
  QVERIFY(!panel->isVisible());
  QVERIFY2(marker->isVisible(), "a hidden tree must show its fold marker");

  marker->click();
  QVERIFY2(panel->isVisible(), "clicking the marker must unfold the tree");
  QVERIFY2(!marker->isVisible(), "marker hides again once the tree is back");
}

void TestCollectionTreePanel::tree_opensFullyExpanded_onFreshSession() {
  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  CollectionConfig child;
  child.name = QStringLiteral("Child");
  child.parentCollectionIndex = 0;
  KartendTest::MockedMainWindowFixture fixture({shell, child});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));

  auto *tree = win->findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY(tree);
  bool sawParentRow = false;
  for (QTreeWidgetItemIterator it(tree); *it; ++it) {
    if ((*it)->childCount() == 0) {
      continue;
    }
    sawParentRow = true;
    QVERIFY2((*it)->isExpanded(),
             qPrintable(QStringLiteral("row '%1' must open expanded on a fresh session")
                            .arg((*it)->text(0))));
  }
  QVERIFY2(sawParentRow, "seeded parent/child must produce at least one branch row");
}

void TestCollectionTreePanel::icons_onIndentedRows_renderCenteredAndUnclipped() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  const QString iconPath = artDir.path() + QStringLiteral("/wide.png");
  {
    QImage art(240, 24, QImage::Format_ARGB32);
    art.fill(QColor(255, 0, 0));
    QVERIFY(art.save(iconPath));
  }
  const QString squarePath = artDir.path() + QStringLiteral("/square.png");
  {
    QImage art(24, 24, QImage::Format_ARGB32);
    art.fill(QColor(0, 0, 255));
    QVERIFY(art.save(squarePath));
  }

  // Depth-3 probe at MINIMUM panel width — the exact conditions of the
  // field report (2026-08-17): the deeper the row and the narrower the
  // panel, the further Qt's decoration-rect centring used to push the
  // icon past the edge. Normal style everywhere: the tinted DEFAULT would
  // recolour the probe and the red-pixel scan below would find nothing.
  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  shell.collectionTree.treeIconStyle = TreeIconStyle::Normal;
  shell.collectionTree.treeWidth = CollectionTreeSettings::kMinWidth;
  CollectionConfig child;
  child.name = QStringLiteral("Child");
  child.parentCollectionIndex = 0;
  child.collectionIcon = squarePath; // square: must NOT inherit boost height
  child.collectionTree.treeIconStyle = TreeIconStyle::Normal;
  child.collectionTree.treeWidth = CollectionTreeSettings::kMinWidth;
  CollectionConfig grand;
  grand.name = QStringLiteral("Grand");
  grand.parentCollectionIndex = 1;
  grand.collectionIcon = iconPath; // depth-3 row carries the probe icon
  grand.collectionTree.treeIconStyle = TreeIconStyle::Normal;
  grand.collectionTree.treeWidth = CollectionTreeSettings::kMinWidth;
  KartendTest::MockedMainWindowFixture fixture({shell, child, grand});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));

  auto *controller = win->findChild<CollectionTreeController *>();
  QVERIFY(controller);
  // Activate Shell so its collectionTree state (min width, Normal) applies.
  controller->onCollectionSwitched(0);

  auto *tree = win->findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY(tree);
  QVERIFY(tree->viewport());
  // Let deferred icon rebakes (viewport-resize singleShots) settle.
  QTest::qWait(80);

  const QPixmap grabbed = tree->viewport()->grab();
  const QImage frame = grabbed.toImage();
  QVERIFY(!frame.isNull());
  // The grab is PHYSICAL pixels; every widget metric below is logical.
  // Convert once and measure everything in physical, or a 2x display
  // "fails" this test on pure unit mixing (which happened — 2026-08-17).
  const qreal dpr = grabbed.devicePixelRatio() > 0 ? grabbed.devicePixelRatio() : 1.0;
  int left = frame.width();
  int right = -1;
  for (int y = 0; y < frame.height(); ++y) {
    for (int x = 0; x < frame.width(); ++x) {
      const QColor c = frame.pixelColor(x, y);
      if (c.red() > 180 && c.green() < 90 && c.blue() < 90) {
        left = qMin(left, x);
        right = qMax(right, x);
      }
    }
  }
  QVERIFY2(right >= 0, "probe icon must be rendered somewhere in the viewport");

  // Fully inside the viewport: nothing may touch the last pixels before the
  // edge (the 8px chrome margin guarantees daylight when unclipped).
  QVERIFY2(right < frame.width() - qRound(4 * dpr),
           qPrintable(QStringLiteral("icon right edge %1 hugs/clips the viewport edge %2")
                          .arg(right)
                          .arg(frame.width())));

  // Centred in the row's visible span [indent*depth, viewport width],
  // all in PHYSICAL pixels; tolerance scales with dpr.
  const int rowStart = qRound(tree->indentation() * 3 * dpr);
  const int spanCenter = rowStart + (frame.width() - rowStart) / 2;
  const int iconCenter = (left + right) / 2;
  QVERIFY2(qAbs(iconCenter - spanCenter) <= qRound(5 * dpr),
           qPrintable(QStringLiteral("icon centre %1 vs visible-span centre %2 (row start %3, "
                                     "viewport %4, dpr %5)")
                          .arg(iconCenter)
                          .arg(spanCenter)
                          .arg(rowStart)
                          .arg(frame.width())
                          .arg(dpr)));

  // Row DENSITY (field report 2026-08-17: every row ballooned to the boost
  // headroom): a square icon's row must hug the base icon height, not the
  // 1.6x boosted decoration. Logical units throughout (visualItemRect).
  QTreeWidgetItem *shellItem = tree->topLevelItem(0);
  QVERIFY(shellItem);
  QVERIFY(shellItem->childCount() >= 1);
  QTreeWidgetItem *childItem = shellItem->child(0);
  const int baseIconSize = CollectionTreeSettings{}.treeIconSize;
  const int childRowH = tree->visualItemRect(childItem).height();
  QVERIFY2(childRowH <= baseIconSize + 10,
           qPrintable(QStringLiteral("square-icon row is %1px tall for a %2px icon — the boost "
                                     "headroom is leaking into unboosted rows")
                          .arg(childRowH)
                          .arg(baseIconSize)));
}
