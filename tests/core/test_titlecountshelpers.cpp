// Tests for TitleCountsHelpers::refreshTitleCounts — the window-title renderer
// extracted from MainWindow (Kartend-tu2hq, second standalone src/core test).
// Covers the early-exit guards that don't require a fully-wired manager graph:
// null host, missing DatabaseManager, and out-of-range collection index.

#include <QApplication>
#include <QLabel>
#include <QTest>
#include <QWidget>

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "mockdatabasemanager.h"
#include "titlecountshelpers.h"

class TestTitleCountsHelpers : public QObject {
  Q_OBJECT
private slots:
  void nullHostIsNoOp();
  void noDatabaseManagerIsNoOp();
  void outOfRangeIndexResetsToApplicationName();
};

void TestTitleCountsHelpers::nullHostIsNoOp() {
  // No title host → must return without dereferencing anything.
  ApplicationContext ctx;
  QList<CollectionConfig> collections;
  TitleCountsHelpers::refreshTitleCounts(nullptr, ctx, collections, 0, nullptr);
  QVERIFY(true); // reaching here (no crash) is the assertion
}

void TestTitleCountsHelpers::noDatabaseManagerIsNoOp() {
  // Without a DatabaseManager the helper bails before touching the title, so a
  // host title set by the caller is left untouched.
  QWidget host;
  host.setWindowTitle(QStringLiteral("untouched"));
  ApplicationContext ctx; // ctx.databaseManager() == nullptr
  QList<CollectionConfig> collections;
  collections.append(CollectionConfig{});
  TitleCountsHelpers::refreshTitleCounts(&host, ctx, collections, 0, nullptr);
  QCOMPARE(host.windowTitle(), QStringLiteral("untouched"));
}

void TestTitleCountsHelpers::outOfRangeIndexResetsToApplicationName() {
  // DatabaseManager present (so the helper proceeds past the early guard) but
  // the current index is out of range → the title resets to the application
  // name rather than indexing past the end of the collection list.
  QWidget host;
  host.setWindowTitle(QStringLiteral("stale"));
  KartendTest::MockDatabaseManager db;
  ApplicationContext ctx;
  ctx.managers.databaseManager = &db;
  QList<CollectionConfig> collections; // empty → index 0 is out of range
  TitleCountsHelpers::refreshTitleCounts(&host, ctx, collections, 0, nullptr);
  QCOMPARE(host.windowTitle(), qApp->applicationName());
}

QTEST_MAIN(TestTitleCountsHelpers)
#include "test_titlecountshelpers.moc"
