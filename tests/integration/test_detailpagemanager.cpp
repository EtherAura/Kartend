#include "test_detailpagemanager.h"

#include "applicationcontext.h"
#include "applicationmanager.h"
#include "detailpagemanager.h"
#include "idetailpageoverlay.h"
#include "idetailspanemanager.h"
#include "itemdetaildata.h"
#include "mainwindow.h"
// Kartend-xrj9r: this suite asserts only on in-memory coordinator state
// (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "mocks/mockdatabasemanager.h"
#include "mocks/mockedmainwindowfixture.h"

#include <QTest>

namespace {

/// Records the calls DetailPageManager makes through the IDetailPageOverlay
/// role so tests can assert on forwarder behaviour without instantiating
/// the concrete DetailPageOverlay widget (which needs the full chrome
/// graph). Mirrors the contract documented in IDetailPageOverlay.
class StubDetailPageOverlay : public IDetailPageOverlay {
public:
  void showWith(const Payload &payload) override {
    ++showCount;
    lastPayload = payload;
  }
  void hideOverlay() override { ++hideCount; }
  [[nodiscard]] bool isActive() const override { return active; }

  int showCount = 0;
  int hideCount = 0;
  bool active = false;
  Payload lastPayload;
};

/// Minimal IDetailsPaneManager that hands back a caller-set ItemContext. Only
/// currentItemContext() matters for DetailPageManager; the rest are no-ops.
class StubDetailsPaneManager : public IDetailsPaneManager {
public:
  void toggleSidebar() override {}
  void updateSidebarMetadata(ItemWidget *) override {}
  void updateSidebarMetadata(const QString &, const QString &) override {}
  void refreshSidebarMetadataImmediate() override {}
  void applySidebarStateForCollection(int) override {}
  void updateSidebarLayout(int) override {}
  [[nodiscard]] bool isSidebarVisible() const override { return false; }
  [[nodiscard]] IDetailsPane *sidebarWidget() const override { return nullptr; }
  [[nodiscard]] const ItemContext &currentItemContext() const override { return ctx; }

  ItemContext ctx;
};

/// Capturing DatabaseManager double: records the async detail request and lets
/// the test emit the result on demand (the inherited itemDetailLoaded signal),
/// so the orchestration can be driven deterministically without a worker
/// thread. No own Q_OBJECT needed — it only emits a base-class signal.
class CapturingDetailDb : public KartendTest::MockDatabaseManager {
public:
  void loadItemDetailAsync(int token, const QString &, const QString &filePath, const QString &,
                           const QString &, const QString &) override {
    ++requestCount;
    lastToken = token;
    lastFilePath = filePath;
  }
  void emitDetailLoaded(const ItemDetailData &data, int token) {
    emit itemDetailLoaded(data, token);
  }

  int requestCount = 0;
  int lastToken = -1;
  QString lastFilePath;
};

} // namespace

void TestDetailPageManager::testConstructionInitialDefaults() {
  DetailPageManager mgr;
  // Before setupReferences, the manager has no overlay pointer. isOverlayActive
  // must short-circuit to false — otherwise navigation events that consult
  // this flag to decide whether to dismiss the page would crash on a null
  // overlay dereference.
  QVERIFY(!mgr.isOverlayActive());
}

void TestDetailPageManager::testHideOverlayWithoutOverlayIsNoOp() {
  DetailPageManager mgr;
  // hideOverlay is wired to navigation events (collection change, search,
  // etc.). It runs before setupReferences during certain teardown paths,
  // so the null-guard inside the implementation is the only thing keeping
  // those paths from crashing. Verify the call survives.
  mgr.hideOverlay();
  QVERIFY(!mgr.isOverlayActive());
}

void TestDetailPageManager::testShowForCurrentSelectionWithoutOverlayIsNoOp() {
  DetailPageManager mgr;
  // The "show details" keyboard shortcut routes here. Without an overlay
  // wired, the call must early-return before the 4 DB queries inside —
  // a regression that issued those queries against a null DatabaseManager
  // would crash here.
  mgr.showForCurrentSelection();
  QVERIFY(!mgr.isOverlayActive());
}

void TestDetailPageManager::testIsOverlayActiveDelegatesToOverlay() {
  DetailPageManager mgr;
  StubDetailPageOverlay overlay;
  DetailPageManagerSetup setup;
  setup.overlay = &overlay;
  mgr.setupReferences(setup);

  // isOverlayActive is a pure pass-through (m_overlay && m_overlay->isActive()).
  // Both branches of the boolean AND matter: a regression that returned a
  // cached snapshot would lag behind the live overlay state.
  overlay.active = false;
  QVERIFY(!mgr.isOverlayActive());

  overlay.active = true;
  QVERIFY(mgr.isOverlayActive());

  overlay.active = false;
  QVERIFY(!mgr.isOverlayActive());
}

void TestDetailPageManager::testHideOverlayDelegatesToOverlay() {
  DetailPageManager mgr;
  StubDetailPageOverlay overlay;
  DetailPageManagerSetup setup;
  setup.overlay = &overlay;
  mgr.setupReferences(setup);

  // Every hideOverlay call must reach the overlay — this is the navigation
  // dismissal contract. A regression that conditioned the forward on
  // m_overlay->isActive() would silently drop the call when the overlay
  // had already lowered itself between request and dispatch.
  QCOMPARE(overlay.hideCount, 0);
  mgr.hideOverlay();
  QCOMPARE(overlay.hideCount, 1);
  mgr.hideOverlay();
  QCOMPARE(overlay.hideCount, 2);
}

void TestDetailPageManager::testShowForCurrentSelectionWithoutContextIsNoOp() {
  DetailPageManager mgr;
  StubDetailPageOverlay overlay;
  DetailPageManagerSetup setup;
  // ctx left null — the production setupReferences in the live wiring
  // always passes ctx, but ApplicationManager teardown can race with a
  // pending "show details" key press. The null-ctx guard must short-circuit
  // before reaching m_ctx->detailsPaneManager().
  setup.ctx = nullptr;
  setup.overlay = &overlay;
  mgr.setupReferences(setup);

  mgr.showForCurrentSelection();

  // Overlay must not have been driven — without ctx, the manager has no
  // way to compute a payload.
  QCOMPARE(overlay.showCount, 0);
}

void TestDetailPageManager::testFixtureExposesDetailPageManagerViaApplicationManager() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *detailPage = win->getApplicationManager()->getDetailPageManager();

  // The live wiring constructs DetailPageManager during ApplicationManager
  // init and feeds it the real DetailPageOverlay via setupReferences. After
  // fixture construction it must be reachable and inactive (no item has
  // been resolved by the sidebar yet, so even a forced show would no-op
  // on the invalid item context).
  QVERIFY(detailPage != nullptr);
  QVERIFY(!detailPage->isOverlayActive());
}

void TestDetailPageManager::testShowForCurrentSelectionShowsCheapOverlayThenRequestsLoad() {
  StubDetailsPaneManager pane;
  pane.ctx.filePath = QStringLiteral("/games/sonic.bin");
  pane.ctx.itemName = QStringLiteral("Sonic");
  pane.ctx.uuid = QStringLiteral("uuid-1");

  CapturingDetailDb db;
  StubDetailPageOverlay overlay;
  ApplicationContext ctx;
  ctx.managers.detailsPaneManager = &pane;
  ctx.managers.databaseManager = &db;

  DetailPageManager mgr;
  DetailPageManagerSetup setup;
  setup.ctx = &ctx;
  setup.overlay = &overlay;
  mgr.setupReferences(setup);

  mgr.showForCurrentSelection();

  // Kartend-4p8o: the overlay is shown immediately with the cheap fields and
  // no artwork (that comes from the async load), so the open never blocks on
  // the DB / filesystem.
  QCOMPARE(overlay.showCount, 1);
  QCOMPARE(overlay.lastPayload.filePath, QStringLiteral("/games/sonic.bin"));
  QCOMPARE(overlay.lastPayload.itemName, QStringLiteral("Sonic"));
  QVERIFY(overlay.lastPayload.artwork.isEmpty());

  // The detail load was dispatched for that file with a fresh (positive) token.
  QCOMPARE(db.requestCount, 1);
  QCOMPARE(db.lastFilePath, QStringLiteral("/games/sonic.bin"));
  QVERIFY(db.lastToken > 0);
}

void TestDetailPageManager::testItemDetailLoadedPopulatesOverlay() {
  StubDetailsPaneManager pane;
  pane.ctx.filePath = QStringLiteral("/games/sonic.bin");
  pane.ctx.itemName = QStringLiteral("Sonic");
  pane.ctx.uuid = QStringLiteral("uuid-1");

  CapturingDetailDb db;
  StubDetailPageOverlay overlay;
  ApplicationContext ctx;
  ctx.managers.detailsPaneManager = &pane;
  ctx.managers.databaseManager = &db;

  DetailPageManager mgr;
  DetailPageManagerSetup setup;
  setup.ctx = &ctx;
  setup.overlay = &overlay;
  mgr.setupReferences(setup);

  mgr.showForCurrentSelection();
  QCOMPARE(overlay.showCount, 1);

  ItemDetailData data;
  data.valid = true;
  data.metadata.title = QStringLiteral("Sonic the Hedgehog"); // wins over the file stem
  data.manualPath = QStringLiteral("/manuals/sonic.pdf");
  data.typedArtwork.append({QStringLiteral("front"), QStringLiteral("/art/front/sonic.png")});
  data.videoPath = QStringLiteral("/video/sonic.mp4");
  data.fileSize = 4096;
  data.fileExtension = QStringLiteral(".bin");
  db.emitDetailLoaded(data, db.lastToken);

  // The overlay was refreshed with the loaded payload.
  QCOMPARE(overlay.showCount, 2);
  QCOMPARE(overlay.lastPayload.itemName, QStringLiteral("Sonic the Hedgehog"));
  QCOMPARE(overlay.lastPayload.manualPath, QStringLiteral("/manuals/sonic.pdf"));
  QCOMPARE(overlay.lastPayload.fileSize, qint64(4096));
  // Gallery order: video tile first, then the typed 'front' tile.
  QCOMPARE(overlay.lastPayload.artwork.size(), 2);
  QVERIFY(overlay.lastPayload.artwork.first().isVideo);
  QCOMPARE(overlay.lastPayload.artwork.first().path, QStringLiteral("/video/sonic.mp4"));
  QCOMPARE(overlay.lastPayload.artwork.at(1).path, QStringLiteral("/art/front/sonic.png"));
}

void TestDetailPageManager::testStaleItemDetailResultIsIgnored() {
  StubDetailsPaneManager pane;
  pane.ctx.filePath = QStringLiteral("/a");
  pane.ctx.itemName = QStringLiteral("A");
  pane.ctx.uuid = QStringLiteral("uuid-a");

  CapturingDetailDb db;
  StubDetailPageOverlay overlay;
  ApplicationContext ctx;
  ctx.managers.detailsPaneManager = &pane;
  ctx.managers.databaseManager = &db;

  DetailPageManager mgr;
  DetailPageManagerSetup setup;
  setup.ctx = &ctx;
  setup.overlay = &overlay;
  mgr.setupReferences(setup);

  mgr.showForCurrentSelection(); // token for item A
  const int staleToken = db.lastToken;

  // The user opens item B before A's load returns.
  pane.ctx.filePath = QStringLiteral("/b");
  pane.ctx.itemName = QStringLiteral("B");
  pane.ctx.uuid = QStringLiteral("uuid-b");
  mgr.showForCurrentSelection(); // cheap show for B
  QCOMPARE(overlay.showCount, 2);

  // A's (now stale) result arrives — must be dropped, not painted over B.
  ItemDetailData stale;
  stale.valid = true;
  stale.metadata.title = QStringLiteral("A-details");
  db.emitDetailLoaded(stale, staleToken);
  QCOMPARE(overlay.showCount, 2); // unchanged

  // B's current result is applied.
  ItemDetailData fresh;
  fresh.valid = true;
  fresh.metadata.title = QStringLiteral("B-details");
  db.emitDetailLoaded(fresh, db.lastToken);
  QCOMPARE(overlay.showCount, 3);
  QCOMPARE(overlay.lastPayload.itemName, QStringLiteral("B-details"));
}
