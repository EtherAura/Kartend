// Tests for CoverFlowController's view-type gating + null-safe no-op contract
// (Kartend-kv0wa), plus the pending-artwork retry pipeline (Kartend-6x8tn):
// cards built against a cold DirectoryCache must be patched by the trailing
// retry once the cache warms, dropped on a cached negative, and cleared on
// rebuild / deactivation. The controller borrows every dependency and is
// designed to no-op cleanly when they are unwired (headless smoke / early
// shutdown), so most of its public surface is exercisable without standing
// up the GUI stack.
#include <QDir>
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
  void clearedSelection_doesNotSnapCarouselToItemZero();

  // Pending-artwork retry (Kartend-6x8tn)
  void retry_fillsArtworkAfterPrewarm();
  void retry_cachedNegativeDropsPendingWithoutRearm();
  void retry_rebuildClearsPendingAndTimer();
  void retry_deactivationClearsPendingAndTimer();
  // Kartend-t4rjw
  void retry_warmRootWithColdCoverSubdirIsNotTreatedAsArtless();
  // Kartend-1js9j — hand-linked covers on the card face
  void manualLink_paintsCardFaceWithoutWaitingOnTheDirectoryCache();
  void manualLink_outranksAnAutoDiscoveredCover();
  void manualLink_emptyMapLeavesAutoDiscoveryUntouched();

  // Kartend-5dhlv
  void subcollectionCard_fallsBackToNamedImageInParentArtworkDir();
  void subcollectionCard_collectionIconStillWins();

  // Activation must translate the carousel's filtered-visual index into the
  // store's actual-index space before classifying (subcollection / virtual
  // folder / media), mirroring rebuildCards' mapping.
  void itemActivated_mapsFilteredVisualToActual();

  // Attract-mode carousel drift (Kartend-wmxwg)
  void drift_isDriftableNeedsActiveCarouselWithRoomToMove();
  void drift_pixelsConvertToCardsViaCardPitch();
  void drift_commitsSelectionWhenANewCardBecomesNearestCentre();
  void drift_reportsFalseAtEachEndAndClamps();
  void drift_selectionCommitSnapsInsteadOfGliding();
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

// Kartend-g7hbx: a cleared canonical selection (-1) is a transient — the
// restore pipeline clears before re-selecting, search transitions clear
// between filters, and (before its own fix) the hidden grid's empty-click
// path cleared too. CoverFlowWidget clamps negatives to 0, so forwarding the
// clear glided the carousel to the FIRST item with a phantom selection
// border while the toolbar counter kept the old index. The controller must
// hold the carousel in place instead.
void TestCoverFlowController::clearedSelection_doesNotSnapCarouselToItemZero() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.store.filePaths() << mediaDir.filePath(QStringLiteral("clip0.mp4"))
                      << mediaDir.filePath(QStringLiteral("clip1.mp4"))
                      << mediaDir.filePath(QStringLiteral("clip2.mp4"));
  h.controller.ensureWidget();
  QVERIFY(h.controller.widget() != nullptr);
  h.controller.rebuildCards();
  QCOMPARE(h.controller.widget()->cardCount(), 3);

  h.controller.onSelectionChanged(2);
  QCOMPARE(h.controller.widget()->selectedIndex(), 2);

  h.controller.onSelectionChanged(-1);
  QCOMPARE(h.controller.widget()->selectedIndex(), 2);
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

// Kartend-t4rjw: the reported symptom was cover-flow art missing until the
// user clicked the item. A cover lookup cascades from the flat artwork root
// into the typed cover subdirs (`front/` — where the scrape pipeline writes
// covers), and those are separate cache entries warmed in a later phase than
// the root. The pending-artwork gate used to test the ROOT alone and read a
// warm root plus an empty result as "cached negative, genuinely artless", so a
// card whose cover sat in front/ was never registered for the retry and kept
// its placeholder until some unrelated rebuild re-resolved it.
void TestCoverFlowController::retry_warmRootWithColdCoverSubdirIsNotTreatedAsArtless() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.store.filePaths() << mediaDir.filePath(QStringLiteral("clip0.mp4"));
  h.controller.ensureWidget();
  QVERIFY(h.controller.widget() != nullptr);

  // Put the cache in the state the bug needs: flat root warm, cover subdir
  // cold. Warming the root BEFORE front/ exists is the deterministic way to
  // get there — prewarmDirectories expands a root with the cover subdirs that
  // exist AT THAT MOMENT, so a subdir created afterwards is not covered.
  //
  // In production the same observable state arises from the walk itself:
  // prewarmDirectories hands root and front/ to one blockingMap, and their
  // listings land independently. The root of a scraped collection is usually
  // near-empty (covers go into the typed subdirs) so it caches almost
  // instantly, while front/ can hold thousands of files — leaving a window,
  // comfortably wider than the 400ms retry tick, where exactly this holds.
  auto &cache = ArtworkUtils::DirectoryCache::instance();
  cache.prewarmDirectories({artDir.path()});
  QVERIFY(cache.isDirectoryCached(artDir.path()));

  QVERIFY(QDir(artDir.path()).mkpath(QStringLiteral("front")));
  const QString artwork = touchFile(artDir.filePath(QStringLiteral("front/clip0.png")));
  QVERIFY(!artwork.isEmpty());
  QVERIFY(!cache.isDirectoryCached(QDir(artDir.path()).absoluteFilePath(QStringLiteral("front"))));

  h.controller.rebuildCards();
  QCOMPARE(h.controller.widget()->cardCount(), 1);
  QVERIFY(h.controller.widget()->cardAt(0).artworkPath.isEmpty());
  // The card MUST be registered: the lookup has not settled, because the one
  // directory actually holding the cover has never been scanned. Pre-fix this
  // was 0 and the retry timer never armed.
  QCOMPARE(h.controller.pendingArtworkCount(), 1);
  QVERIFY(h.controller.artworkRetryActive());

  // The retry warms the whole cascade itself, so the card resolves with no
  // further help — and reaching a non-empty path also proves the retry pass
  // did not erase the slot on the strength of the warm root alone.
  QTRY_COMPARE_WITH_TIMEOUT(h.controller.widget()->cardAt(0).artworkPath, artwork, 5000);
  QCOMPARE(h.controller.pendingArtworkCount(), 0);
  QTRY_VERIFY_WITH_TIMEOUT(!h.controller.artworkRetryActive(), 5000);
}

// Kartend-5dhlv: a subcollection TILE has two artwork sources, in this order —
// the child's own collectionIcon, then an image named after the child in the
// PARENT's artwork directory. buildCard read the icon and stopped, so a
// subcollection following the naming convention (the mechanism that predates
// the key, and the only one Grid honoured before Kartend-kb2vx) showed the
// placeholder in cover flow alone.
// ----- Hand-linked covers on the card face (Kartend-1js9j) -----

void TestCoverFlowController::manualLink_paintsCardFaceWithoutWaitingOnTheDirectoryCache() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QTemporaryDir elsewhere;
  QVERIFY(artDir.isValid() && mediaDir.isValid() && elsewhere.isValid());
  // The linked image lives OUTSIDE the artwork directory and is named nothing
  // like the item — exactly the case name-based discovery can never answer,
  // and the case that used to leave the card on a placeholder while the
  // sidebar gallery showed the cover.
  const QString linked = touchFile(elsewhere.filePath(QStringLiteral("hand-picked.png")));
  QVERIFY(!linked.isEmpty());

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  const QString item = mediaDir.filePath(QStringLiteral("Overture.flac"));
  h.store.filePaths() << item;
  h.controller.setManualCoverPaths({{item, linked}});
  h.controller.ensureWidget();

  h.controller.rebuildCards();
  QCOMPARE(h.controller.widget()->cardCount(), 1);
  QCOMPARE(h.controller.widget()->cardAt(0).artworkPath, linked);
  // A link is a database fact, so it resolves on the very first build against
  // a stone-cold DirectoryCache — the slot never enters the pending-artwork
  // retry and the timer stays disarmed.
  QCOMPARE(h.controller.pendingArtworkCount(), 0);
  QVERIFY(!h.controller.artworkRetryActive());
}

void TestCoverFlowController::manualLink_outranksAnAutoDiscoveredCover() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QTemporaryDir elsewhere;
  QVERIFY(artDir.isValid() && mediaDir.isValid() && elsewhere.isValid());
  // Auto-discovery WOULD find this one: it is named after the item and sits in
  // the artwork directory.
  const QString autoArt = touchFile(artDir.filePath(QStringLiteral("Overture.png")));
  const QString linked = touchFile(elsewhere.filePath(QStringLiteral("hand-picked.png")));
  QVERIFY(!autoArt.isEmpty() && !linked.isEmpty());
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artDir.path()});

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  const QString item = mediaDir.filePath(QStringLiteral("Overture.flac"));
  h.store.filePaths() << item;
  h.controller.setManualCoverPaths({{item, linked}});
  h.controller.ensureWidget();

  h.controller.rebuildCards();
  // Manual beats auto — the precedence the sidebar gallery, loadItemDetail and
  // items.artwork_path all already apply.
  QCOMPARE(h.controller.widget()->cardAt(0).artworkPath, linked);
}

void TestCoverFlowController::manualLink_emptyMapLeavesAutoDiscoveryUntouched() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());
  const QString autoArt = touchFile(artDir.filePath(QStringLiteral("Overture.png")));
  QVERIFY(!autoArt.isEmpty());
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artDir.path()});

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  const QString item = mediaDir.filePath(QStringLiteral("Overture.flac"));
  h.store.filePaths() << item;
  // No links anywhere in the library — the overwhelmingly common case. The
  // name cascade must still be what answers.
  h.controller.ensureWidget();

  h.controller.rebuildCards();
  QCOMPARE(h.controller.widget()->cardAt(0).artworkPath, autoArt);
}

void TestCoverFlowController::subcollectionCard_fallsBackToNamedImageInParentArtworkDir() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid());
  // Named after the CHILD, sitting in the PARENT's artwork directory.
  const QString tileArt = touchFile(artDir.filePath(QStringLiteral("Shelf.png")));
  QVERIFY(!tileArt.isEmpty());

  static const QList<CollectionConfig> collections = [] {
    QList<CollectionConfig> out;
    CollectionConfig shelf;
    shelf.name = QStringLiteral("Shelf");
    // collectionIcon deliberately unset — the convention must answer alone.
    out.append(shelf);
    return out;
  }();
  static const QList<int> subs = {0};

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.context.currentIndex = 0;
  h.context.hasSubcollectionOverride = true;
  h.context.subcollectionOverride = subs;
  h.store.initializeSubcollections(h.context, &collections, nullptr);

  CoverFlowControllerSetup setup;
  setup.mediaScrollArea = h.scrollArea;
  setup.context = &h.context;
  setup.collections = &collections;
  setup.dataManager = &h.store;
  h.controller.setupReferences(setup);
  h.controller.ensureWidget();
  QVERIFY(h.controller.widget() != nullptr);

  h.controller.rebuildCards();
  QCOMPARE(h.controller.widget()->cardCount(), 1);
  // Cold cache on the first build, so the tile resolves through the same
  // trailing retry media cards use — which could not even register a
  // subcollection slot before this fix (artworkDirForActual bailed on
  // anything failing isMediaIndex).
  QTRY_COMPARE_WITH_TIMEOUT(h.controller.widget()->cardAt(0).artworkPath, tileArt, 5000);
}

void TestCoverFlowController::subcollectionCard_collectionIconStillWins() {
  QTemporaryDir artDir;
  QTemporaryDir mediaDir;
  QTemporaryDir iconDir;
  QVERIFY(artDir.isValid() && mediaDir.isValid() && iconDir.isValid());
  // Both sources present: the explicit per-collection choice must win, or
  // setting collectionIcon on a collection that already follows the naming
  // convention would appear to do nothing.
  QVERIFY(!touchFile(artDir.filePath(QStringLiteral("Shelf.png"))).isEmpty());
  const QString icon = touchFile(iconDir.filePath(QStringLiteral("explicit.png")));
  QVERIFY(!icon.isEmpty());

  static const QString iconPath = icon;
  static const QList<CollectionConfig> collections = [] {
    QList<CollectionConfig> out;
    CollectionConfig shelf;
    shelf.name = QStringLiteral("Shelf");
    shelf.collectionIcon = iconPath;
    out.append(shelf);
    return out;
  }();
  static const QList<int> subs = {0};

  CoverFlowHarness h(artDir.path(), mediaDir.path());
  h.context.currentIndex = 0;
  h.context.hasSubcollectionOverride = true;
  h.context.subcollectionOverride = subs;
  h.store.initializeSubcollections(h.context, &collections, nullptr);

  CoverFlowControllerSetup setup;
  setup.mediaScrollArea = h.scrollArea;
  setup.context = &h.context;
  setup.collections = &collections;
  setup.dataManager = &h.store;
  h.controller.setupReferences(setup);
  h.controller.ensureWidget();

  h.controller.rebuildCards();
  QCOMPARE(h.controller.widget()->cardCount(), 1);
  // Resolved synchronously from the config — no cache, no retry involved.
  QCOMPARE(h.controller.widget()->cardAt(0).artworkPath, icon);
  QCOMPARE(h.controller.pendingArtworkCount(), 0);
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

// ─────────────────────────────────────────────────────────────────────────────
// Attract-mode carousel drift (Kartend-wmxwg)
//
// Attract autoscroll drives the item scroll area's scrollbar, which Cover Flow
// hides. driftBy() is the carousel's stand-in, and its contract is narrow:
// convert pixels via the card pitch, keep the CANONICAL selection on the card
// nearest to centre, and report the ends so attract can bounce.
//
// The selectItemByIndex → onSelectionChanged loop below is the real production
// round trip in miniature: in the app that signal reaches SelectionManager,
// whose selectionChanged comes back into onSelectionChanged as a synchronous
// (Direct) call. Wiring it to itself here reproduces that without the manager
// stack, and without it the "did the selection actually commit" assertions
// would be measuring nothing.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Harness with N cards and the selection round trip closed.
struct DriftHarness {
  explicit DriftHarness(int cards) : h(QString(), QString()) {
    h.controller.ensureWidget();
    QList<CoverFlowCardData> list;
    list.reserve(cards);
    for (int i = 0; i < cards; ++i) {
      list.append(CoverFlowCardData{QStringLiteral("Card %1").arg(i), {}, {}});
    }
    h.controller.widget()->setCards(list);
    QObject::connect(&h.controller, &CoverFlowController::selectItemByIndex, &h.controller,
                     &CoverFlowController::onSelectionChanged);
  }
  CoverFlowWidget *w() const { return h.controller.widget(); }
  /// Absolute carousel position — the coordinate driftBy() works in and
  /// CoverFlowWidget::currentPositionF() paints from.
  qreal pos() const { return w()->selectedIndex() + w()->selectionPositionF(); }
  /// Pixels equivalent to @p cards of travel at the widget's current size.
  qreal px(qreal cards) const { return cards * w()->cardPitchPx(); }

  CoverFlowHarness h;
};

} // namespace

void TestCoverFlowController::drift_isDriftableNeedsActiveCarouselWithRoomToMove() {
  CoverFlowController bare;
  QVERIFY(!bare.isDriftable()); // no context, no widget
  QVERIFY(!bare.driftBy(10.0));

  DriftHarness d(1);
  // Active and widgeted, but a single card has nowhere to go — attract must
  // decline to start rather than tick against a carousel that cannot move.
  QVERIFY(d.h.controller.isActive());
  QVERIFY(!d.h.controller.isDriftable());
  QVERIFY(!d.h.controller.driftBy(d.px(1.0)));

  DriftHarness many(6);
  QVERIFY(many.h.controller.isDriftable());

  // View type is still the gate: leaving Cover Flow makes the carousel
  // undriftable even though the cards are still loaded.
  many.h.context.config.viewType = ViewType::Grid;
  QVERIFY(!many.h.controller.isDriftable());
  QVERIFY(!many.h.controller.driftBy(many.px(1.0)));
}

void TestCoverFlowController::drift_pixelsConvertToCardsViaCardPitch() {
  DriftHarness d(10);
  QVERIFY(d.w()->cardPitchPx() > 0.0);

  // A quarter card of travel stays inside card 0: nearest-to-centre is still
  // card 0, so only the fractional offset moves.
  QVERIFY(d.h.controller.driftBy(d.px(0.25)));
  QCOMPARE(d.w()->selectedIndex(), 0);
  QVERIFY(qAbs(d.w()->selectionPositionF() - 0.25) < 0.001);

  // Drift accumulates rather than restarting from the selected card.
  QVERIFY(d.h.controller.driftBy(d.px(0.2)));
  QVERIFY(qAbs(d.pos() - 0.45) < 0.001);

  // Sub-pixel speeds are representable — the carousel position is a qreal, so
  // unlike the scrollbar path there is no integer accumulator to round them to
  // a standstill.
  const qreal before = d.pos();
  QVERIFY(d.h.controller.driftBy(0.1));
  QVERIFY(d.pos() > before);
}

void TestCoverFlowController::drift_commitsSelectionWhenANewCardBecomesNearestCentre() {
  DriftHarness d(10);
  QSignalSpy selectSpy(&d.h.controller, &CoverFlowController::selectItemByIndex);

  // Just short of the half-card mark: card 0 is still the nearest to centre,
  // so the selection must NOT move yet.
  QVERIFY(d.h.controller.driftBy(d.px(0.49)));
  QCOMPARE(selectSpy.count(), 0);
  QCOMPARE(d.w()->selectedIndex(), 0);

  // Past it: card 1 is now centred and owns the selection. Selection changing
  // hands at the half-card mark is what keeps the centred card and the
  // selected item the same card, which the carousel has no way to express
  // otherwise — it has no "scrolled away from the selection" state.
  QVERIFY(d.h.controller.driftBy(d.px(0.02)));
  QCOMPARE(selectSpy.count(), 1);
  QCOMPARE(selectSpy.at(0).at(0).toInt(), 1);
  QCOMPARE(d.w()->selectedIndex(), 1);

  // The absolute position is continuous across the hand-off — no visual jump.
  QVERIFY(qAbs(d.pos() - 0.51) < 0.001);
  QVERIFY(qAbs(d.w()->selectionPositionF() - (-0.49)) < 0.001);

  // Backwards works the same way.
  QVERIFY(d.h.controller.driftBy(d.px(-0.02)));
  QCOMPARE(selectSpy.count(), 2);
  QCOMPARE(selectSpy.at(1).at(0).toInt(), 0);
}

void TestCoverFlowController::drift_reportsFalseAtEachEndAndClamps() {
  DriftHarness d(4);

  // Overshooting the last card clamps there and reports the end — attract's
  // cue to bounce, matching what nextScrollPosition reports for a scrollbar.
  QVERIFY(!d.h.controller.driftBy(d.px(99.0)));
  QCOMPARE(d.w()->selectedIndex(), 3);
  QVERIFY(qAbs(d.pos() - 3.0) < 0.001);

  // Still pinned, still reporting the end, rather than running off the list.
  QVERIFY(!d.h.controller.driftBy(d.px(5.0)));
  QCOMPARE(d.w()->selectedIndex(), 3);

  // And symmetrically at the near end.
  QVERIFY(!d.h.controller.driftBy(d.px(-99.0)));
  QCOMPARE(d.w()->selectedIndex(), 0);
  QVERIFY(qAbs(d.pos()) < 0.001);

  // Away from both ends it reports "still room".
  QVERIFY(d.h.controller.driftBy(d.px(1.0)));
}

void TestCoverFlowController::drift_selectionCommitSnapsInsteadOfGliding() {
  DriftHarness d(10);
  // The glide only runs on a VISIBLE widget (setSelectedIndex snaps otherwise),
  // so the suppression this pins is only observable once shown.
  d.h.host.resize(800, 600);
  d.h.host.show();
  if (!QTest::qWaitForWindowExposed(&d.h.host)) {
    QSKIP("window never exposed; the glide path this test discriminates cannot run");
  }
  // ensureWidget() leaves the carousel hidden — applyVisibility is what shows
  // it once Cover Flow is the active view.
  d.h.controller.applyVisibility();
  if (!d.w()->isVisible()) {
    QSKIP("carousel not visible under this QPA; the glide path cannot run");
  }

  QVERIFY(d.h.controller.driftBy(d.px(0.6)));
  QCOMPARE(d.w()->selectedIndex(), 1);
  const qreal settled = d.w()->selectionPositionF();
  QVERIFY(qAbs(settled - (-0.4)) < 0.001);

  // A glide would be animating selectionPositionF from -1.0 back toward 0 over
  // 240ms, overwriting the drift on every frame inside that window and leaving
  // the carousel visually stuck. Snapped, the offset is untouched by the clock.
  QTest::qWait(120);
  QCOMPARE(d.w()->selectedIndex(), 1);
  QVERIFY2(qAbs(d.w()->selectionPositionF() - settled) < 0.001,
           "selectionPositionF moved on its own after a drift commit — the glide was not "
           "suppressed, so it is fighting the drift and will stall the carousel");
}

QTEST_MAIN(TestCoverFlowController)
#include "test_coverflowcontroller.moc"
