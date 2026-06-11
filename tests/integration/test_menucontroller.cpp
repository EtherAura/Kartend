#include "test_menucontroller.h"

#include "collection/generalsettings.h"
#include "collectiontypes.h"
#include "menucontroller.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QByteArray>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QTest>

namespace {

// All layout actions, so a sync test can assert the non-active ones are
// explicitly unchecked (syncLayoutActions sets checked = match for every entry).
const QList<QByteArray> kLayoutActionNames = {
    QByteArrayLiteral("actionLayoutGrid"), QByteArrayLiteral("actionLayoutList"),
    QByteArrayLiteral("actionLayoutCoverFlow"), QByteArrayLiteral("actionLayoutHorizontal")};

} // namespace

void TestMenuController::syncSortActions_checksMatchingEntry_data() {
  QTest::addColumn<int>("sortMode");
  QTest::addColumn<QByteArray>("expectedAction");

  // Only the modes that have a menu entry (the list-view-only modes are
  // covered separately). Names mirror kSortActions in menucontroller.cpp.
  QTest::newRow("NameAscending") << int(SortMode::NameAscending)
                                 << QByteArrayLiteral("actionSortNameAsc");
  QTest::newRow("NameDescending") << int(SortMode::NameDescending)
                                  << QByteArrayLiteral("actionSortNameDesc");
  QTest::newRow("DateDescending") << int(SortMode::DateDescending)
                                  << QByteArrayLiteral("actionSortDateDesc");
  QTest::newRow("DateAscending") << int(SortMode::DateAscending)
                                 << QByteArrayLiteral("actionSortDateAsc");
  QTest::newRow("SizeDescending") << int(SortMode::SizeDescending)
                                  << QByteArrayLiteral("actionSortSizeDesc");
  QTest::newRow("SizeAscending") << int(SortMode::SizeAscending)
                                 << QByteArrayLiteral("actionSortSizeAsc");
  QTest::newRow("Random") << int(SortMode::Random) << QByteArrayLiteral("actionSortRandom");
}

void TestMenuController::syncSortActions_checksMatchingEntry() {
  QFETCH(int, sortMode);
  QFETCH(QByteArray, expectedAction);

  QMainWindow win;
  Ui_MainWindow ui;
  ui.setupUi(&win);

  GeneralSettings settings;
  settings.view.sortMode = SortMode(sortMode);

  MenuController controller;
  MenuControllerContext ctx;
  ctx.ui = &ui;
  ctx.mainWindow = &win;
  ctx.getGeneralSettings = [&settings] { return &settings; };
  controller.setContext(ctx);

  controller.syncSortActions();

  auto *action = win.findChild<QAction *>(QString::fromLatin1(expectedAction));
  QVERIFY2(
      action,
      qPrintable(QStringLiteral("missing action %1").arg(QString::fromLatin1(expectedAction))));
  QVERIFY2(
      action->isChecked(),
      qPrintable(
          QStringLiteral("expected %1 to be checked").arg(QString::fromLatin1(expectedAction))));
}

void TestMenuController::syncSortActions_listViewOnlyModeLeavesPriorSelection() {
  // The list-view-only sort modes (Collection*/Artwork*) have no menu entry,
  // so syncSortActions finds no match and must leave the previously-checked
  // sort action alone rather than clearing the menu.
  QMainWindow win;
  Ui_MainWindow ui;
  ui.setupUi(&win);

  GeneralSettings settings;
  MenuController controller;
  MenuControllerContext ctx;
  ctx.ui = &ui;
  ctx.mainWindow = &win;
  ctx.getGeneralSettings = [&settings] { return &settings; };
  controller.setContext(ctx);

  // Establish a prior selection from a mode that DOES have an entry.
  settings.view.sortMode = SortMode::NameDescending;
  controller.syncSortActions();
  auto *priorAction = win.findChild<QAction *>(QStringLiteral("actionSortNameDesc"));
  QVERIFY(priorAction);
  QVERIFY(priorAction->isChecked());

  // Each list-view-only mode must leave that checkmark in place.
  for (const SortMode listOnly : {SortMode::ArtworkFirst, SortMode::ArtworkLast,
                                  SortMode::CollectionAscending, SortMode::CollectionDescending}) {
    settings.view.sortMode = listOnly;
    controller.syncSortActions();
    QVERIFY2(priorAction->isChecked(),
             "a list-view-only sort mode must leave the prior sort selection checked");
  }
}

void TestMenuController::syncSortActions_reflectsExcludeSubfoldersToggle() {
  // The independent "exclude subfolders from sort" toggle is mirrored from
  // GeneralSettings each sync, separately from the mutually-exclusive group.
  QMainWindow win;
  Ui_MainWindow ui;
  ui.setupUi(&win);

  GeneralSettings settings;
  MenuController controller;
  MenuControllerContext ctx;
  ctx.ui = &ui;
  ctx.mainWindow = &win;
  ctx.getGeneralSettings = [&settings] { return &settings; };
  controller.setContext(ctx);

  auto *subfolders = win.findChild<QAction *>(QStringLiteral("actionSortSubfolders"));
  QVERIFY(subfolders);

  settings.view.excludeSubfoldersFromSort = true;
  controller.syncSortActions();
  QVERIFY(subfolders->isChecked());

  settings.view.excludeSubfoldersFromSort = false;
  controller.syncSortActions();
  QVERIFY(!subfolders->isChecked());
}

void TestMenuController::syncLayoutActions_checksExactlyActiveViewType_data() {
  QTest::addColumn<int>("viewType");
  QTest::addColumn<QByteArray>("expectedAction");

  QTest::newRow("Grid") << int(ViewType::Grid) << QByteArrayLiteral("actionLayoutGrid");
  QTest::newRow("List") << int(ViewType::List) << QByteArrayLiteral("actionLayoutList");
  QTest::newRow("CoverFlow") << int(ViewType::CoverFlow)
                             << QByteArrayLiteral("actionLayoutCoverFlow");
  QTest::newRow("Horizontal") << int(ViewType::Horizontal)
                              << QByteArrayLiteral("actionLayoutHorizontal");
}

void TestMenuController::syncLayoutActions_checksExactlyActiveViewType() {
  QFETCH(int, viewType);
  QFETCH(QByteArray, expectedAction);

  QMainWindow win;
  Ui_MainWindow ui;
  ui.setupUi(&win);

  MenuController controller;
  MenuControllerContext ctx;
  ctx.ui = &ui;
  ctx.mainWindow = &win;
  controller.setContext(ctx);

  controller.syncLayoutActions(ViewType(viewType));

  // syncLayoutActions sets checked == (entry matches) for every layout entry,
  // so exactly the active view type's action is checked and the rest are not.
  for (const QByteArray &name : kLayoutActionNames) {
    auto *action = win.findChild<QAction *>(QString::fromLatin1(name));
    QVERIFY2(action,
             qPrintable(QStringLiteral("missing action %1").arg(QString::fromLatin1(name))));
    QCOMPARE(action->isChecked(), name == expectedAction);
  }
}
