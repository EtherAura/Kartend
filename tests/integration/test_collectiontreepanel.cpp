#include "test_collectiontreepanel.h"

#include "collection/collectionconfig.h"
#include "collectiontreecontroller.h"
#include "mainwindow.h"
#include "mocks/mockedmainwindowfixture.h"

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
