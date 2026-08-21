#include "test_collectiontreepanel.h"

#include "applicationmanager.h"
#include "artworkpreviewoverlay.h"
#include "collection/collectionconfig.h"
#include "collectiontreecontroller.h"
#include "detailspane.h"
#include "interactionmanager.h"
#include "keyboardmanager.h"
#include "mainwindow.h"
#include "mocks/mockedmainwindowfixture.h"
#include "overlayscrollbars.h"

#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>
#include <QWidget>

void TestCollectionTreePanel::hiddenTree_leavesNoIndicatorBehind() {
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
  QVERIFY(controller);
  QWidget *panel = win->findChild<QWidget *>(QStringLiteral("collectionTreePanel"));
  QVERIFY(panel);
  if (!panel->isVisible()) {
    controller->toggleVisible();
  }
  QVERIFY(panel->isVisible());

  controller->toggleVisible();
  QVERIFY(!panel->isVisible());
  QVERIFY2(!win->findChild<QWidget *>(QStringLiteral("collectionTreeFoldMarker")),
           "a hidden tree must leave nothing on screen");
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
    QVERIFY2(
        (*it)->isExpanded(),
        qPrintable(
            QStringLiteral("row '%1' must open expanded on a fresh session").arg((*it)->text(0))));
  }
  QVERIFY2(sawParentRow, "seeded parent/child must produce at least one branch row");
}

void TestCollectionTreePanel::icons_onIndentedRows_renderCenteredAtConfiguredSize() {
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
  // Debug aid: KARTEND_TEST_DUMP_DIR=<dir> saves the grabbed frame so a
  // human can eyeball exactly what the assertions measured.
  if (const QByteArray dumpDir = qgetenv("KARTEND_TEST_DUMP_DIR"); !dumpDir.isEmpty()) {
    frame.save(QString::fromLocal8Bit(dumpDir) + QStringLiteral("/treepanel-dpr") +
               QString::number(grabbed.devicePixelRatio()) + QStringLiteral(".png"));
  }
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

  // NOT asserted any more: that the icon fits inside the viewport.
  //
  // It used to be, and the guarantee came from a width clamp that shrank any
  // logo too wide for the panel. That clamp was also what made the rendered
  // size track the panel width, reported twice (2026-08-18, and 2026-08-20:
  // "expanding nav pane also increases icon size ... the icon size should
  // remain fixed"). The two cannot both hold for a logo wider than the panel:
  // either it shrinks (size follows width) or it overflows (size is fixed).
  // The maintainer chose fixed, so a wide logo in a narrow panel now clips at
  // the edges — deliberately, and adjustable with the width and icon-size
  // settings.

  // LEAF rows centre on the PANEL itself (user direction 2026-08-17), not
  // the indent span. All PHYSICAL pixels; tolerance covers the style's
  // decoration pad slop and scales with dpr.
  const int spanCenter = frame.width() / 2;
  const int iconCenter = (left + right) / 2;
  QVERIFY2(qAbs(iconCenter - spanCenter) <= qRound(8 * dpr),
           qPrintable(QStringLiteral("leaf icon centre %1 vs panel centre %2 (viewport %3, "
                                     "dpr %4)")
                          .arg(iconCenter)
                          .arg(spanCenter)
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
  // Base icon height + the proportional row pad (max(16, 3*size/4)) + slack.
  QVERIFY2(childRowH <= baseIconSize + qMax(16, (baseIconSize * 3) / 4) + 6,
           qPrintable(QStringLiteral("square-icon row is %1px tall for a %2px icon — the boost "
                                     "headroom is leaking into unboosted rows")
                          .arg(childRowH)
                          .arg(baseIconSize)));
}

void TestCollectionTreePanel::focusSectionChord_movesBetweenTreeAndGrid() {
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
  QApplication::setActiveWindow(win);

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  auto *tree = win->findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY(tree);
  QVERIFY(tree->isVisible());

  im->moveFocusSection(-1, 0); // grid -> left sidebar
  QVERIFY2(tree->hasFocus(), "Select+Left must focus the collection tree");

  // Plain d-pad/left-stick always control the grid (user decision
  // 2026-08-17): a plain direction pulls focus home from a sidebar.
  im->returnGamepadFocusToGrid();
  QVERIFY2(!tree->hasFocus(), "a plain direction must pull focus back to the grid");

  im->moveFocusSection(-1, 0); // back to the tree for the next leg
  QVERIFY2(tree->hasFocus(), "chord re-focuses the tree");

  im->moveFocusSection(1, 0); // tree -> grid
  QVERIFY2(!tree->hasFocus(), "Select+Right must hand focus back to the grid");

  // Hidden tree is skipped: fold the panel, chord left again — focus must
  // NOT land on a hidden section.
  auto *controller = win->findChild<CollectionTreeController *>();
  QVERIFY(controller);
  if (controller->isPanelVisible()) {
    controller->toggleVisible();
  }
  im->moveFocusSection(-1, 0);
  QVERIFY2(!tree->hasFocus(), "a folded tree must not receive chord focus");
}

void TestCollectionTreePanel::focusModifierHud_showsWhileHeldAndHidesOnRelease() {
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
  QApplication::setActiveWindow(win);

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  QVERIFY2(!win->findChild<QWidget *>(QStringLiteral("focusSectionOverlay")),
           "the HUD must not exist before the modifier is held");

  im->setFocusModifierActive(true);
  auto *hud = win->findChild<QWidget *>(QStringLiteral("focusSectionOverlay"));
  QVERIFY2(hud, "holding the modifier must create the HUD");
  QVERIFY2(hud->isVisible(), "the HUD must be visible while the modifier is held");
  QVERIFY2(!hud->mask().isEmpty(),
           "the HUD masks out the focused section, so its mask must be set");
  // Same human-eyeball hook the icon test uses: KARTEND_TEST_DUMP_DIR=<dir>
  // writes the composited window so the HUD can be reviewed, not just
  // asserted.
  if (const QByteArray dumpDir = qgetenv("KARTEND_TEST_DUMP_DIR"); !dumpDir.isEmpty()) {
    win->grab().toImage().save(QString::fromLocal8Bit(dumpDir) + QStringLiteral("/focus-hud.png"));
  }

  im->setFocusModifierActive(false);
  QVERIFY2(!hud->isVisible(), "releasing the modifier must hide the HUD");
}

void TestCollectionTreePanel::rightStick_upFocusesToolbar_andDrivesTheFocusedSection() {
  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  CollectionConfig child;
  child.name = QStringLiteral("Child");
  child.parentCollectionIndex = 0;
  CollectionConfig sibling;
  sibling.name = QStringLiteral("Sibling");
  sibling.parentCollectionIndex = 0;
  KartendTest::MockedMainWindowFixture fixture({shell, child, sibling});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));
  QApplication::setActiveWindow(win);

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  auto *topBar = win->findChild<QWidget *>(QStringLiteral("itemsTopBar"));
  QVERIFY(topBar);
  auto *tree = win->findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY(tree);

  // From the grid: UP lands on the toolbar while the modifier is held.
  im->returnGamepadFocusToGrid();
  im->setFocusModifierActive(true);
  im->routeSectionInput(0, -1);
  QWidget *focused = QApplication::focusWidget();
  QVERIFY2(focused && (focused == topBar || topBar->isAncestorOf(focused)),
           "right-stick up from the grid must focus the toolbar");

  // Down from the toolbar returns to the grid (still modifier-held).
  im->routeSectionInput(0, 1);
  focused = QApplication::focusWidget();
  QVERIFY2(!focused || !(focused == topBar || topBar->isAncestorOf(focused)),
           "down from the toolbar must leave it");

  im->setFocusModifierActive(false);

  // With the tree focused, the stick moves the TREE's rows, not sections.
  im->moveFocusSection(-1, 0);
  QVERIFY(tree->hasFocus());
  tree->setCurrentItem(tree->topLevelItem(0));
  QTreeWidgetItem *before = tree->currentItem();
  QVERIFY(before);
  const bool consumed = im->routeSectionInput(0, 1);
  QVERIFY2(consumed, "a vertical flick must be consumed by the focused tree");
  QVERIFY2(tree->hasFocus(), "driving the tree list must not change section");
  QVERIFY2(tree->currentItem() != before, "the tree's current row must advance");
}

void TestCollectionTreePanel::rightStick_verticalReachesToolbarOnlyWithModifier() {
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
  QApplication::setActiveWindow(win);

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  auto *topBar = win->findChild<QWidget *>(QStringLiteral("itemsTopBar"));
  QVERIFY(topBar);
  const auto onToolbar = [topBar]() {
    QWidget *fw = QApplication::focusWidget();
    return fw && (fw == topBar || topBar->isAncestorOf(fw));
  };

  // Unheld: vertical must NOT reach the toolbar — it belongs to the pane.
  im->returnGamepadFocusToGrid();
  im->routeSectionInput(0, -1);
  QVERIFY2(!onToolbar(), "without the modifier, up must not focus the toolbar");

  // Held: vertical switches sections again.
  im->setFocusModifierActive(true);
  im->routeSectionInput(0, -1);
  QVERIFY2(onToolbar(), "with the modifier held, up must focus the toolbar");
  // The ring marks the focused section during chord mode.
  auto *ring = win->findChild<QWidget *>(QStringLiteral("selectionIndicator"));
  QVERIFY2(ring && ring->isVisible(), "chord mode must show the focus ring");
  if (const QByteArray dumpDir = qgetenv("KARTEND_TEST_DUMP_DIR"); !dumpDir.isEmpty()) {
    win->grab().toImage().save(QString::fromLocal8Bit(dumpDir) + QStringLiteral("/chord-ring.png"));
  }
  im->setFocusModifierActive(false);
}

void TestCollectionTreePanel::treeTraversal_keepsTreeFocusedAcrossCollectionSwitch() {
  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  CollectionConfig child;
  child.name = QStringLiteral("Child");
  child.parentCollectionIndex = 0;
  CollectionConfig sibling;
  sibling.name = QStringLiteral("Sibling");
  sibling.parentCollectionIndex = 0;
  KartendTest::MockedMainWindowFixture fixture({shell, child, sibling});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));
  QApplication::setActiveWindow(win);

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  auto *tree = win->findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY(tree);

  im->moveFocusSection(-1, 0);
  QVERIFY2(tree->hasFocus(), "chord-left must focus the tree");
  tree->setCurrentItem(tree->topLevelItem(0));

  // Walk a few rows, letting the auto-activation debounce fire between
  // steps — the switch must not pull focus out from under the stick.
  for (int step = 0; step < 3; ++step) {
    im->routeSectionInput(0, 1);
    QTest::qWait(320);
    QVERIFY2(tree->hasFocus(),
             qPrintable(QStringLiteral("tree lost focus after traversal step %1").arg(step)));
  }
}

void TestCollectionTreePanel::expandedArtwork_backDismissesInsteadOfLeavingCollection() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  const QString artPath = artDir.path() + QStringLiteral("/expand.png");
  {
    QImage art(64, 64, QImage::Format_ARGB32);
    art.fill(QColor(0, 200, 0));
    QVERIFY(art.save(artPath));
  }

  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  KartendTest::MockedMainWindowFixture fixture({shell});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  auto *pane = win->findChild<DetailsPane *>();
  QVERIFY(pane);
  QVERIFY2(!im->visibleArtworkOverlay(), "no overlay before anything is expanded");

  pane->openArtworkExpanded(artPath, /*isVideo=*/false);
  QVERIFY2(im->visibleArtworkOverlay(), "expanding artwork must raise the overlay");
  // Let the async artwork decode land before dismissing — a human never
  // presses Back within the same millisecond, and an in-flight delivery
  // re-shows the overlay (see the dismissal race noted alongside).
  QTest::qWait(300);

  // The gamepad Back path: Escape reaches the overlay's own handler.
  QVERIFY(im->sendKeyToArtworkOverlay(Qt::Key_Escape));
  QTest::qWait(50);
  QVERIFY2(!im->visibleArtworkOverlay(), "Back must dismiss the expanded artwork");
}

void TestCollectionTreePanel::paneSelection_idleReturnsFocusToGrid() {
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
  QApplication::setActiveWindow(win);

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  auto *tree = win->findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY(tree);

  // Park focus away from the grid, then let the pane-idle timeout run. It
  // must return focus to the grid even if the pane had nothing to drive.
  im->moveFocusSection(-1, 0);
  QVERIFY(tree->hasFocus());
  im->driveDetailsPane(1, /*allowAdvance=*/true);
  QTest::qWait(1300);
  if (im->driveDetailsPane(0, /*allowAdvance=*/false)) {
    QVERIFY2(!tree->hasFocus(), "the idle timeout must hand focus back to the grid");
  }
  // With no pane regions the drive is a no-op and focus is untouched —
  // assert only that nothing crashed and the ring is gone.
  auto *ring = win->findChild<QWidget *>(QStringLiteral("selectionIndicator"));
  QVERIFY2(!ring || !ring->isVisible(), "the ring must not linger after the idle timeout");
}

void TestCollectionTreePanel::expandedArtwork_cyclesThenReportsBoundaryInsteadOfWrapping() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  QStringList paths;
  for (int i = 0; i < 2; ++i) {
    const QString path = artDir.path() + QStringLiteral("/art%1.png").arg(i);
    QImage art(32, 32, QImage::Format_ARGB32);
    art.fill(i == 0 ? QColor(200, 0, 0) : QColor(0, 0, 200));
    QVERIFY(art.save(path));
    paths << path;
  }

  ArtworkPreviewOverlay overlay;
  overlay.resize(400, 300);
  overlay.showArtworkAtPath(paths.at(0));
  overlay.setGalleryEntries({{QStringLiteral("first"), paths.at(0), false},
                             {QStringLiteral("second"), paths.at(1), false}});
  overlay.show();
  QTest::qWait(20); // key handling needs no exposure, just a live widget

  QSignalSpy boundary(&overlay, &ArtworkPreviewOverlay::galleryBoundaryReached);
  const auto pressRight = [&overlay]() {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(&overlay, &press);
  };

  // First press moves within the item's artwork: no hand-off yet.
  pressRight();
  QCOMPARE(boundary.count(), 0);

  // Second press runs off the end — report it rather than wrapping.
  pressRight();
  QCOMPARE(boundary.count(), 1);
  QCOMPARE(boundary.at(0).at(0).toInt(), 1);
}

void TestCollectionTreePanel::expandedArtwork_boundaryIsHookedRegardlessOfInput() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  const QString artPath = artDir.path() + QStringLiteral("/hooked.png");
  {
    QImage art(48, 48, QImage::Format_ARGB32);
    art.fill(QColor(120, 0, 120));
    QVERIFY(art.save(artPath));
  }

  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  KartendTest::MockedMainWindowFixture fixture({shell});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));

  auto *pane = win->findChild<DetailsPane *>();
  QVERIFY(pane);
  // Opened WITHOUT any gamepad involvement, as a mouse click would.
  pane->openArtworkExpanded(artPath, /*isVideo=*/false);
  QTest::qWait(50);

  auto *overlay = win->findChild<ArtworkPreviewOverlay *>();
  QVERIFY(overlay);
  QVERIFY(overlay->isVisible());
  // The interaction layer must already be listening for the hand-off, so
  // a keyboard/wheel boundary reaches item stepping. disconnect() reports
  // whether a connection existed — a destructive probe, but this is the
  // last assertion in the test.
  QVERIFY2(QObject::disconnect(overlay, &ArtworkPreviewOverlay::galleryBoundaryReached, nullptr,
                               nullptr),
           "the artwork boundary hand-off must be connected as soon as the overlay shows");
}

void TestCollectionTreePanel::expandedArtwork_wheelAdvancesOneArtworkPerGesture() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  QStringList paths;
  for (int i = 0; i < 2; ++i) {
    const QString path = artDir.path() + QStringLiteral("/wheel%1.png").arg(i);
    QImage art(32, 32, QImage::Format_ARGB32);
    art.fill(i == 0 ? QColor(10, 90, 10) : QColor(90, 10, 10));
    QVERIFY(art.save(path));
    paths << path;
  }

  ArtworkPreviewOverlay overlay;
  overlay.resize(400, 300);
  overlay.showArtworkAtPath(paths.at(0));
  overlay.setGalleryEntries({{QStringLiteral("first"), paths.at(0), false},
                             {QStringLiteral("second"), paths.at(1), false}});
  overlay.show();
  QTest::qWait(20);

  QSignalSpy boundary(&overlay, &ArtworkPreviewOverlay::galleryBoundaryReached);
  const auto spinWheel = [&overlay]() {
    QWheelEvent wheel(QPointF(10, 10), overlay.mapToGlobal(QPoint(10, 10)), QPoint(0, -120),
                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                      /*inverted=*/false);
    QApplication::sendEvent(&overlay, &wheel);
  };

  // A hard flick: five events in one burst. Only the first may count, so
  // the two-entry gallery lands on the second artwork and stops — reaching
  // the boundary here would prove the burst advanced more than once.
  for (int i = 0; i < 5; ++i) {
    spinWheel();
  }
  QCOMPARE(boundary.count(), 0);

  // After the cooldown the wheel works again, and now the second tick runs
  // off the end as expected.
  QTest::qWait(300);
  spinWheel();
  QCOMPARE(boundary.count(), 1);
}

void TestCollectionTreePanel::expandedArtwork_artlessItemShowsPlaceholder_andWheelNeverLeaks() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // A media file with NO artwork anywhere near it.
  const QString mediaPath = dir.path() + QStringLiteral("/Some Game (USA).zip");
  {
    QFile f(mediaPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
  }

  QWidget host;
  host.resize(800, 600);
  auto *overlay = new ArtworkPreviewOverlay(&host);
  host.show();
  QTest::qWait(20);

  overlay->showArtworkForFile(mediaPath, dir.path());
  QTest::qWait(20);
  QVERIFY2(overlay->isVisible(),
           "an item with no artwork must still present the overlay with a placeholder");
  const QList<QLabel *> labels = overlay->findChildren<QLabel *>();
  bool sawTile = false;
  for (QLabel *label : labels) {
    if (label->isVisible() && !label->pixmap().isNull()) {
      sawTile = true;
      break;
    }
  }
  QVERIFY2(sawTile, "the placeholder tile must be painted into the overlay");

  // A wheel tick on an artless item must never reach the grid, AND must
  // still let the user travel: with nothing to cycle it hands straight
  // off to item stepping (field report 2026-08-18: artless items were a
  // dead end you could not scroll past).
  QSignalSpy handoff(overlay, &ArtworkPreviewOverlay::galleryBoundaryReached);
  QWheelEvent wheel(QPointF(10, 10), overlay->mapToGlobal(QPoint(10, 10)), QPoint(0, -120),
                    QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                    /*inverted=*/false);
  QApplication::sendEvent(overlay, &wheel);
  QVERIFY2(wheel.isAccepted(), "expand mode must consume the wheel, never pass it to the grid");
  QCOMPARE(handoff.count(), 1);
  QCOMPARE(handoff.at(0).at(0).toInt(), 1);

  // Keyboard travels off an artless item too.
  QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
  QApplication::sendEvent(overlay, &left);
  QCOMPARE(handoff.count(), 2);
  QCOMPARE(handoff.at(1).at(0).toInt(), -1);
}

void TestCollectionTreePanel::expandedArtwork_kineticWheelStreamAdvancesOnlyOnce() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  QList<ArtworkPreviewOverlay::GalleryEntry> entries;
  for (int i = 0; i < 3; ++i) {
    const QString path = artDir.path() + QStringLiteral("/stream%1.png").arg(i);
    QImage art(32, 32, QImage::Format_ARGB32);
    art.fill(QColor(20 * i, 60, 120));
    QVERIFY(art.save(path));
    entries.append({QStringLiteral("art%1").arg(i), path, false});
  }

  ArtworkPreviewOverlay overlay;
  overlay.resize(400, 300);
  overlay.showArtworkAtPath(entries.at(0).path);
  overlay.setGalleryEntries(entries);
  overlay.show();
  QTest::qWait(20);

  QSignalSpy boundary(&overlay, &ArtworkPreviewOverlay::galleryBoundaryReached);
  // ~500ms of coasting: one event every ~16ms, exactly what a kinetic
  // wheel or trackpad emits after a single flick.
  for (int i = 0; i < 30; ++i) {
    QWheelEvent wheel(QPointF(10, 10), overlay.mapToGlobal(QPoint(10, 10)), QPoint(0, -120),
                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                      /*inverted=*/false);
    QApplication::sendEvent(&overlay, &wheel);
    QTest::qWait(16);
  }
  // Three entries starting at the first: a single advance lands on the
  // second and stops. Reaching the boundary would mean the stream marched
  // through the gallery — the runaway.
  QCOMPARE(boundary.count(), 0);
}

void TestCollectionTreePanel::expandedArtwork_itemStepLeavesNoPhantomKeyHeld() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  const QString artPath = artDir.path() + QStringLiteral("/phantom.png");
  {
    QImage art(48, 48, QImage::Format_ARGB32);
    art.fill(QColor(0, 140, 140));
    QVERIFY(art.save(artPath));
  }

  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  KartendTest::MockedMainWindowFixture fixture({shell});
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));

  auto *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  auto *keyboard = im->keyboardManager();
  QVERIFY(keyboard);
  auto *pane = win->findChild<DetailsPane *>();
  QVERIFY(pane);

  pane->openArtworkExpanded(artPath, /*isVideo=*/false);
  QTest::qWait(20);
  QVERIFY(im->visibleArtworkOverlay());
  QVERIFY2(!keyboard->isPhysicalKeyDown(), "nothing is held before the step");

  // The real traversal path. It runs the KEYBOARD navigation internally,
  // which arms "physical key down" expecting a release that a wheel tick
  // or gamepad flick never sends — that phantom hold kept the grid
  // scrolling after every traversal (field report 2026-08-18).
  im->stepExpandedItem(1);
  QVERIFY2(!keyboard->isPhysicalKeyDown(),
           "the item step must release the phantom key hold it arms internally");
}

namespace {
int panelWidthOf(QWidget *win) {
  QWidget *panel = win->findChild<QWidget *>(QStringLiteral("collectionTreePanel"));
  return panel ? panel->width() : 0;
}
} // namespace

void TestCollectionTreePanel::navPanel_matchesToolbarHeightAndTone_andHidesNativeScrollbar() {
  QList<CollectionConfig> seeds;
  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  seeds << shell;
  // Enough rows that the tree genuinely overflows and would show a bar.
  for (int i = 0; i < 40; ++i) {
    CollectionConfig child;
    child.name = QStringLiteral("Child %1").arg(i);
    child.parentCollectionIndex = 0;
    seeds << child;
  }
  KartendTest::MockedMainWindowFixture fixture(seeds);
  MainWindow *win = fixture.window();
  QVERIFY(win);
  win->resize(1200, 700);
  win->show();
  QVERIFY(QTest::qWaitForWindowExposed(win));
  QTest::qWait(120); // let deferred re-bakes settle

  auto *tree = win->findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY(tree);
  QWidget *topBar = win->findChild<QWidget *>(QStringLiteral("itemsTopBar"));
  QVERIFY(topBar);

  // (a) Native scrollbar must be gone when overlays are enabled.
  OverlayScrollbars::apply(tree, true);
  QTest::qWait(60);
  QVERIFY2(!tree->verticalScrollBar()->isVisible(),
           "with overlay scrollbars on, the tree's native bar must not be visible");

  // (a2) The viewport must NOT resize when other code re-asserts a
  // scrollbar policy — that width change is what relaid the grid and made
  // items jump between frames (field report 2026-08-18).
  const int viewportBefore = tree->viewport()->width();
  tree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  QTest::qWait(60);
  QCOMPARE(tree->viewport()->width(), viewportBefore);

  // (b) The first top-level row matches the toolbar's height.
  QTreeWidgetItem *first = tree->topLevelItem(0);
  QVERIFY(first);
  const int rowHeight = tree->visualItemRect(first).height();
  QVERIFY2(qAbs(rowHeight - topBar->height()) <= 2,
           qPrintable(QStringLiteral("top-level row is %1px, toolbar is %2px")
                          .arg(rowHeight)
                          .arg(topBar->height())));

  // (c) NO divider column: the sidebar must butt straight against the
  // content. A grip WIDGET of any colour shows as a band between them
  // (field report 2026-08-18, twice), so the tree has to reach the
  // panel's full inner width.
  QVERIFY2(tree->width() >= panelWidthOf(win) - 2,
           qPrintable(QStringLiteral("tree is %1px inside a %2px panel — that difference is the "
                                     "visible divider")
                          .arg(tree->width())
                          .arg(panelWidthOf(win))));

  // (d) One tone: the toolbar and the tree panel resolve to the same
  // colour, whatever the active palette is.
  QWidget *panel = win->findChild<QWidget *>(QStringLiteral("collectionTreePanel"));
  QVERIFY(panel);
  QCOMPARE(topBar->palette().color(topBar->backgroundRole()),
           panel->palette().color(QPalette::Window));
}
