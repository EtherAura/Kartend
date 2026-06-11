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

#include "artworkutils.h"
#include "collection/collectioncontext.h"
#include "collectiontypes.h"
#include "coverflowcontroller.h"
#include "coverflowwidget.h"
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

QTEST_MAIN(TestCoverFlowController)
#include "test_coverflowcontroller.moc"
