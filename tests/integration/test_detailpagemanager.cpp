#include "test_detailpagemanager.h"

#include "applicationmanager.h"
#include "detailpagemanager.h"
#include "idetailpageoverlay.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"

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
  KartendTest::MainWindowFixture fixture;
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
