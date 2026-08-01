// Tests for CoverFlowController's view-type gating + null-safe no-op contract
// (Kartend-kv0wa), plus the pending-artwork retry pipeline (Kartend-6x8tn):
// cards built against a cold DirectoryCache must be patched by the trailing
// retry once the cache warms, dropped on a cached negative, and cleared on
// rebuild / deactivation. The controller borrows every dependency and is
// designed to no-op cleanly when they are unwired (headless smoke / early
// shutdown), so most of its public surface is exercisable without standing
// up the GUI stack.
#include <QFile>
#include <QObject>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

#include <QSignalSpy>

#include "applicationcontext.h"
#include "artworkutils.h"
#include "collection/collectioncontext.h"
#include "collectiontypes.h"
#include "coverflowcontroller.h"
#include "coverflowwidget.h"
#include "filtermanager.h"
#include "scrolldatamanager.h"
#include "videothumbnailextractor.h"

namespace {

// Live harness for the retry tests: ensureWidget() needs a QScrollArea with
// a layouted parent to insert the carousel next to, and the card pipeline
// needs a store + a CoverFlow-typed context. Member order matters — the
// controller is declared last so it is destroyed first (it borrows
// everything else).
struct CoverFlowHarness {
  CoverFlowHarness(const QString &artworkDir, const QString &mediaDir) {
    auto *layout = new QVBoxLayout(&host);
    scrollArea = new QScrollArea(&host);
    layout->addWidget(scrollArea);
    context.config.viewType = ViewType::CoverFlow;
    context.config.artworkDirectory = artworkDir;
    context.config.mediaDirectory = mediaDir;
    CoverFlowControllerSetup setup;
    setup.context = &context;
    setup.mediaScrollArea = scrollArea;
    setup.dataManager = &store;
    controller.setupReferences(setup);
  }
  QWidget host;
  QScrollArea *scrollArea = nullptr;
  CollectionContext context;
  ScrollDataStore store;
  CoverFlowController controller;
};

// Create an (empty) file so the DirectoryCache directory scan picks it up —
// artwork resolution only matches names, it never decodes the image.
QString touchFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return {};
  }
  f.write("x");
  return path;
}

} // namespace

class TestCoverFlowController : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();

  void isActive_falseWithoutContext();
  void isActive_followsViewType();
  void publicMethods_noopWithoutSetup();
  void ensureWidget_noopWithoutScrollArea();

  // Pending-artwork retry (Kartend-6x8tn)
  void retry_fillsArtworkAfterPrewarm();
  void retry_cachedNegativeDropsPendingWithoutRearm();
  void retry_rebuildClearsPendingAndTimer();
  void retry_deactivationClearsPendingAndTimer();

  // Activation must translate the carousel's filtered-visual index into the
  // store's actual-index space before classifying (subcollection / virtual
  // folder / media), mirroring rebuildCards' mapping.
  void itemActivated_mapsFilteredVisualToActual();
};

void TestCoverFlowController::initTestCase() {
  // Touch the extractor singleton before the first CoverFlowWidget ctor
  // hooks its connection — same lazy-init guard the widget test uses.
  (void)VideoThumbnailExtractor::instance();
}

void TestCoverFlowController::init() {
  // The DirectoryCache is a process-wide singleton — start every test cold
  // so one test's warm directories can't mask another's pending logic.
  ArtworkUtils::clearDirectoryCache();
}

void TestCoverFlowController::isActive_falseWithoutContext() {
  CoverFlowController controller;
  // No CollectionContext wired -> the carousel is never the active view.
  QVERIFY(!controller.isActive());
  QCOMPARE(controller.widget(), nullptr);
}

void TestCoverFlowController::isActive_followsViewType() {
  CoverFlowController controller;
  CollectionContext ctx;
  CoverFlowControllerSetup setup;
  setup.context = &ctx;
  controller.setupReferences(setup);

  ctx.config.viewType = ViewType::CoverFlow;
  QVERIFY(controller.isActive());

  ctx.config.viewType = ViewType::Grid;
  QVERIFY(!controller.isActive());

  ctx.config.viewType = ViewType::List;
  QVERIFY(!controller.isActive());
}

void TestCoverFlowController::publicMethods_noopWithoutSetup() {
  CoverFlowController controller; // nothing wired
  // The null-safe contract: exercising the public surface unwired must not
  // crash, and no carousel widget is created.
  controller.applyConfig();
  controller.applyVisibility();
  controller.rebuildCards();
  controller.rebuildCardsIfActive();
  controller.updateCardsIfActive({0, 1, 2});
  controller.refreshForViewTypeChange();
  controller.onSelectionChanged(3);
  QCOMPARE(controller.widget(), nullptr);
  QVERIFY(!controller.isActive());
}

void TestCoverFlowController::ensureWidget_noopWithoutScrollArea() {
  CoverFlowController controller;
  CollectionContext ctx;
  ctx.config.viewType = ViewType::CoverFlow; // active...
  CoverFlowControllerSetup setup;
  setup.context = &ctx; // ...but no media scroll area to host the widget
  controller.setupReferences(setup);
  controller.ensureWidget();
  QCOMPARE(controller.widget(), nullptr);
}

// ----- Pending-artwork retry (Kartend-6x8tn) -----

void TestCoverFlowController::retry_fillsArtworkAfterPrewarm() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());
  const QString artwork = touchFile(artDir.filePath(QStringLiteral("clip0.png")));
  QVERIFY(!artwork.isEmpty());

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.store.filePaths() << mediaDir.filePath(QStringLiteral("clip0.mp4"));
  h.controller.ensureWidget();
  QVERIFY(h.controller.widget() != nullptr);

  h.controller.rebuildCards();
  // Cold cache: the lookup returned empty and queued the directory — the
  // card is registered for the trailing retry and the timer is armed.
  QCOMPARE(h.controller.widget()->cardCount(), 1);
  QVERIFY(h.controller.widget()->cardAt(0).artworkPath.isEmpty());
  QCOMPARE(h.controller.pendingArtworkCount(), 1);
  QVERIFY(h.controller.artworkRetryActive());

  // Populate the cache deterministically (what schedulePrewarm does on the
  // global pool), then let the retry pass patch the pending card.
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artDir.path()});
  QTRY_COMPARE_WITH_TIMEOUT(h.controller.widget()->cardAt(0).artworkPath, artwork, 5000);
  QCOMPARE(h.controller.pendingArtworkCount(), 0);
  QVERIFY(!h.controller.artworkRetryActive());
}

void TestCoverFlowController::retry_cachedNegativeDropsPendingWithoutRearm() {
  QTemporaryDir artDir; // exists but holds no artwork → cached negative
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.store.filePaths() << mediaDir.filePath(QStringLiteral("clip0.mp4"));
  h.controller.ensureWidget();

  h.controller.rebuildCards();
  QCOMPARE(h.controller.pendingArtworkCount(), 1);
  QVERIFY(h.controller.artworkRetryActive());

  // Warm the directory: the listing is now cached with no match, so the
  // retry pass must classify the card as genuinely artless — pending
  // drops and the timer stops instead of re-arming forever.
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artDir.path()});
  QTRY_COMPARE_WITH_TIMEOUT(h.controller.pendingArtworkCount(), 0, 5000);
  QTRY_VERIFY_WITH_TIMEOUT(!h.controller.artworkRetryActive(), 5000);
  QVERIFY(h.controller.widget()->cardAt(0).artworkPath.isEmpty());
}

void TestCoverFlowController::retry_rebuildClearsPendingAndTimer() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.store.filePaths() << mediaDir.filePath(QStringLiteral("clip0.mp4"))
                      << mediaDir.filePath(QStringLiteral("clip1.mp4"));
  h.controller.ensureWidget();

  h.controller.rebuildCards();
  QCOMPARE(h.controller.pendingArtworkCount(), 2);
  QVERIFY(h.controller.artworkRetryActive());

  // A rebuild (the setCards path) rebinds every slot — the stale pending
  // set must be dropped and, with nothing left to retry, the timer stops.
  h.store.clear();
  h.controller.rebuildCards();
  QCOMPARE(h.controller.widget()->cardCount(), 0);
  QCOMPARE(h.controller.pendingArtworkCount(), 0);
  QVERIFY(!h.controller.artworkRetryActive());
}

void TestCoverFlowController::retry_deactivationClearsPendingAndTimer() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.store.filePaths() << mediaDir.filePath(QStringLiteral("clip0.mp4"));
  h.controller.ensureWidget();

  h.controller.rebuildCards();
  QCOMPARE(h.controller.pendingArtworkCount(), 1);
  QVERIFY(h.controller.artworkRetryActive());

  // Switching away from cover flow hides the carousel — applyVisibility
  // must drop the pending set and stop the retry timer.
  h.context.config.viewType = ViewType::Grid;
  h.controller.applyVisibility();
  QCOMPARE(h.controller.pendingArtworkCount(), 0);
  QVERIFY(!h.controller.artworkRetryActive());
}

void TestCoverFlowController::itemActivated_mapsFilteredVisualToActual() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());

  // One subcollection tile ahead of two media items: the store's actual-index
  // space is [Shelf 0][Alpha 1][Beta 2]. Declared before the FilterManager so
  // the manager (which stores const pointers to these containers) is
  // destroyed first.
  static const QList<CollectionConfig> collections = [] {
    QList<CollectionConfig> out;
    CollectionConfig shelf;
    shelf.name = QStringLiteral("Shelf");
    out.append(shelf);
    return out;
  }();
  static const QList<int> subs = {0};
  const QStringList paths = {QStringLiteral("/items/Alpha.mp4"), QStringLiteral("/items/Beta.mp4")};
  QHash<QString, QString> names;
  names.insert(paths.at(0), QStringLiteral("Alpha"));
  names.insert(paths.at(1), QStringLiteral("Beta"));
  const QHash<QString, QString> emptyDisplayNames;
  // Named, not a `{}` temporary: setSourceData stores const-pointers to every
  // container argument, so a braced temporary dangles at the end of the call
  // statement (ASan stack-use-after-scope; a bus error on macOS Release).
  // Same lesson as TestFilterManager's static seed helpers.
  const QStringList noVirtualFolders;

  FilterManager filter;
  filter.setCollections(&collections);
  filter.setSourceData(&paths, &names, &emptyDisplayNames, &subs, &noVirtualFolders);

  ApplicationContext appCtx;
  appCtx.managers.filterManager = &filter;

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.context.currentIndex = 0;
  h.context.hasSubcollectionOverride = true;
  h.context.subcollectionOverride = subs;
  h.store.initializeSubcollections(h.context, &collections, nullptr);
  h.store.filePaths() << paths.at(0) << paths.at(1);

  // Re-run setupReferences with ctx wired so filterMgr() resolves — the
  // harness ctor leaves ctx null (the retry tests don't need the filter).
  CoverFlowControllerSetup setup;
  setup.ctx = &appCtx;
  setup.mediaScrollArea = h.scrollArea;
  setup.context = &h.context;
  setup.collections = &collections;
  setup.dataManager = &h.store;
  h.controller.setupReferences(setup);
  h.controller.ensureWidget();
  QVERIFY(h.controller.widget() != nullptr);

  QSignalSpy subSpy(&h.controller, &CoverFlowController::subcollectionEntered);
  QSignalSpy itemSpy(&h.controller, &CoverFlowController::itemActivated);

  // Unfiltered: visual == actual, slot 0 is the subcollection tile.
  emit h.controller.widget()->itemActivated(0);
  QCOMPARE(subSpy.count(), 1);
  QCOMPARE(subSpy.at(0).at(0).toInt(), 0);
  QCOMPARE(itemSpy.count(), 0);

  // Filter down to just Beta: filtered visual slot 0 → actual 2 (media).
  // Without the visual→actual translation the handler classified slot 0 as
  // the subcollection band and navigated into Shelf instead of launching.
  filter.applyFilter(QStringLiteral("beta"));
  QVERIFY(filter.isFiltered());
  QCOMPARE(filter.filteredIndices(), (QList<int>{2}));
  emit h.controller.widget()->itemActivated(0);
  QCOMPARE(subSpy.count(), 1); // unchanged — not a subcollection activation
  QCOMPARE(itemSpy.count(), 1);
  QCOMPARE(itemSpy.at(0).at(0).toInt(), 0); // outward index stays visual

  // Filter down to just the shelf: filtered visual slot 0 → actual 0
  // (subcollection) — the mapped path still routes subcollections correctly.
  filter.applyFilter(QStringLiteral("shelf"));
  QCOMPARE(filter.filteredIndices(), (QList<int>{0}));
  emit h.controller.widget()->itemActivated(0);
  QCOMPARE(subSpy.count(), 2);
  QCOMPARE(subSpy.at(1).at(0).toInt(), 0);
  QCOMPARE(itemSpy.count(), 1);
}

QTEST_MAIN(TestCoverFlowController)
#include "test_coverflowcontroller.moc"
