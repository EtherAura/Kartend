#include "test_librarytoolscontroller.h"

#include "collection/collectionconfig.h"
#include "idatabasemanager.h"
#include "librarytoolscontroller.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QWidget>

namespace {

/// Headless harness: the guard prompts route through stubbed DialogRunners
/// (so no modal ever opens) and the database getter records each resolution
/// while returning null (so no flow proceeds past the guard).
struct LibraryToolsHarness {
  QWidget host;
  QList<CollectionConfig> collections;
  int currentIndex = -1;
  int databaseResolutions = 0;
  QStringList infoTitles;
  QStringList warnTitles;
  LibraryToolsController controller;

  LibraryToolsHarness() {
    LibraryToolsControllerContext ctx;
    ctx.getParentWindow = [this]() -> QWidget * { return &host; };
    ctx.getCollections = [this]() { return &collections; };
    ctx.getCurrentCollectionIndex = [this]() { return currentIndex; };
    ctx.getDatabaseManager = [this]() -> IDatabaseManager * {
      ++databaseResolutions;
      return nullptr;
    };
    ctx.dialogs.info = [this](const QString &title, const QString &) { infoTitles.append(title); };
    ctx.dialogs.warn = [this](const QString &title, const QString &) { warnTitles.append(title); };
    controller.setContext(ctx);
  }

  void runAllToolFlows() {
    controller.showCollectionHealthInteractive();
    controller.showVariantGroupingInteractive();
    controller.bulkEditInteractive();
    controller.reviewMissingMetadataInteractive();
    controller.artworkWizardInteractive();
  }
};

/// The five flows' guard titles, in runAllToolFlows() order.
QStringList expectedGuardTitles() {
  return {QStringLiteral("Collection health"), QStringLiteral("Duplicates and variants"),
          QStringLiteral("Bulk edit"), QStringLiteral("Review missing metadata"),
          QStringLiteral("Assign missing artwork")};
}

} // namespace

void TestLibraryToolsController::toolFlows_withoutCollections_promptToOpenOne() {
  LibraryToolsHarness h; // no collections, index stays -1

  h.runAllToolFlows();

  // Every flow funnels through the shared guard: one "open a collection
  // first" prompt each, delivered through the stubbed runner (never a modal),
  // no identity warning, and no tool dialog constructed under the host.
  QCOMPARE(h.infoTitles, expectedGuardTitles());
  QVERIFY(h.warnTitles.isEmpty());
  QVERIFY(h.host.findChildren<QDialog *>().isEmpty());
}

void TestLibraryToolsController::toolFlows_withOutOfRangeIndex_promptToOpenOne() {
  LibraryToolsHarness h;
  CollectionConfig videos;
  videos.name = QStringLiteral("Videos");
  h.collections = {videos};
  h.currentIndex = 3; // stale index past the end — same guard as "none open"

  h.runAllToolFlows();

  QCOMPARE(h.infoTitles, expectedGuardTitles());
  QVERIFY(h.host.findChildren<QDialog *>().isEmpty());
}

void TestLibraryToolsController::toolFlows_withoutActiveCollection_skipDatabaseResolution() {
  LibraryToolsHarness h;

  h.runAllToolFlows();

  // The guard validates the collection index before touching managers — with
  // nothing open, the database getter must never be invoked.
  QCOMPARE(h.databaseResolutions, 0);
}

void TestLibraryToolsController::toolFlows_withNullDatabaseManager_stopSilently() {
  LibraryToolsHarness h;
  CollectionConfig videos;
  videos.name = QStringLiteral("Videos");
  videos.mediaDirectory = QStringLiteral("/media/videos");
  h.collections = {videos};
  h.currentIndex = 0;

  h.runAllToolFlows();

  // Active collection + resolvable uuid, but no database manager: each flow
  // must reach the db null-guard (five resolutions) and stop without any
  // prompt or dialog — mirrors the pre-wiring window during startup.
  QCOMPARE(h.databaseResolutions, 5);
  QVERIFY(h.infoTitles.isEmpty());
  QVERIFY(h.warnTitles.isEmpty());
  QVERIFY(h.host.findChildren<QDialog *>().isEmpty());
}
