#include "test_selectiondisplaymanager.h"

#include "applicationmanager.h"
#include "collection/generalsettings.h"
#include "mainwindow.h"
// Kartend-xrj9r: this suite asserts only on in-memory coordinator state
// (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "artworkpreviewoverlay.h"
#include "mocks/mockedmainwindowfixture.h"
#include "scrollmanager.h"
#include "selectiondisplaymanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"

#include <QRect>
#include <QScrollArea>
#include <QString>
#include <QStringLiteral>
#include <QTest>

void TestSelectionDisplayManager::testConstructionInitialDefaults() {
  SelectionDisplayManager mgr;

  // Ctor initialises overlay + state tracker eagerly so ScrollManager's
  // raw aliases (m_overlayManager / m_selectionState in scrollmanager.h)
  // are valid the moment selectionDisplay is wired. A regression that
  // delayed sub-object creation would crash inside ScrollManager's
  // setup, but here it would surface as null getters.
  QVERIFY(mgr.overlay() != nullptr);
  QVERIFY(mgr.state() != nullptr);

  // List header is lazy — created on first updateListHeader() in list mode.
  QVERIFY(mgr.listHeader() == nullptr);

  // Column widths match UIConstants::ListView defaults. They get overridden
  // by applyGeneralSettings during setup if the user has persisted widths.
  QCOMPARE(mgr.collectionColumnWidth(), 150);
  QCOMPARE(mgr.artworkColumnWidth(), 32);
}

void TestSelectionDisplayManager::testApplyGeneralSettingsUpdatesPositiveWidths() {
  SelectionDisplayManager mgr;
  GeneralSettings settings;
  settings.view.listCollectionColumnWidth = 240;
  settings.view.listArtworkColumnWidth = 48;

  mgr.applyGeneralSettings(&settings);

  // Both widths must propagate independently — a copy-paste regression
  // that bound both setters to the same source field would silently
  // collapse the artwork column to match the collection column.
  QCOMPARE(mgr.collectionColumnWidth(), 240);
  QCOMPARE(mgr.artworkColumnWidth(), 48);
}

void TestSelectionDisplayManager::testApplyGeneralSettingsIgnoresZeroOrNegativeWidths() {
  SelectionDisplayManager mgr;
  // Persisted settings can legitimately carry 0 when the column has never
  // been resized. The guard `width > 0` keeps the constructor defaults
  // in that case so the list view still renders a sensible header.
  GeneralSettings settings;
  settings.view.listCollectionColumnWidth = 0;
  settings.view.listArtworkColumnWidth = -5;

  mgr.applyGeneralSettings(&settings);

  QCOMPARE(mgr.collectionColumnWidth(), 150);
  QCOMPARE(mgr.artworkColumnWidth(), 32);
}

void TestSelectionDisplayManager::testApplyGeneralSettingsNullPtrIsNoOp() {
  SelectionDisplayManager mgr;
  // setupReferences may call applyGeneralSettings before the settings
  // graph is fully wired (e.g. headless tests). A null guard prevents a
  // crash on the first column-width read; verify defaults survive.
  mgr.applyGeneralSettings(nullptr);
  QCOMPARE(mgr.collectionColumnWidth(), 150);
  QCOMPARE(mgr.artworkColumnWidth(), 32);
}

void TestSelectionDisplayManager::testArtworkPreviewNotVisibleAtConstruction() {
  SelectionDisplayManager mgr;
  // The preview overlay is lazy-created on the first showArtworkPreview
  // call to keep idle list mode cheap. isVisible() must return false
  // for an unconstructed overlay so consumers can guard with this
  // predicate before calling hideArtworkPreview().
  QVERIFY(!mgr.isArtworkPreviewVisible());
}

void TestSelectionDisplayManager::testHideArtworkPreviewReturnsFalseWhenNotVisible() {
  SelectionDisplayManager mgr;
  // hideArtworkPreview's return value drives the launch path in
  // InteractionManager: a true return means "the overlay swallowed the
  // activation" so the default launch should not run. With no overlay
  // created yet, the activation must fall through.
  QCOMPARE(mgr.hideArtworkPreview(), false);
}

void TestSelectionDisplayManager::testShowArtworkPreviewWithoutScrollAreaIsNoOp() {
  SelectionDisplayManager mgr;
  // showArtworkPreview requires the media scroll area as the overlay
  // parent (the overlay positions itself relative to the viewport).
  // Without the scroll area wired up, the call must early-return rather
  // than crash inside ArtworkPreviewOverlay::create.
  mgr.showArtworkPreview(QStringLiteral("/tmp/none"), QStringLiteral("/tmp/none"));
  QVERIFY(!mgr.isArtworkPreviewVisible());
}

void TestSelectionDisplayManager::testDestroyListHeaderIsIdempotent() {
  SelectionDisplayManager mgr;
  // ~SelectionDisplayManager calls destroyListHeader(), which must be
  // safe to call when the header was never created — otherwise tests
  // that construct + destroy without ever reaching list mode would
  // crash on shutdown. Call twice to also catch a double-destroy bug.
  mgr.destroyListHeader();
  mgr.destroyListHeader();
  QVERIFY(mgr.listHeader() == nullptr);
}

void TestSelectionDisplayManager::testUpdateListHeaderEarlyReturnsWithoutContextOrMetrics() {
  SelectionDisplayManager mgr;
  // The header build path needs both the collection context (for view
  // type) and grid metrics (for header width). With neither wired, the
  // method must early-return without creating a header — a regression
  // here would attempt to construct ListHeaderWidget against a null
  // viewport and crash.
  mgr.updateListHeader();
  QVERIFY(mgr.listHeader() == nullptr);
}

void TestSelectionDisplayManager::testSelectionOverlayRectForInvalidIndexReturnsEmpty() {
  SelectionDisplayManager mgr;
  // Without a selection coordinator and without metrics / item-position
  // callbacks, the fallback path must return an empty QRect for any
  // index. selectionOverlayRectForIndex is called from the animation
  // path; a non-empty rect for a never-configured manager would attempt
  // to draw an overlay against a null viewport.
  const QRect r = mgr.selectionOverlayRectForIndex(0);
  QVERIFY(r.isEmpty());

  const QRect rNeg = mgr.selectionOverlayRectForIndex(-1);
  QVERIFY(rNeg.isEmpty());
}

void TestSelectionDisplayManager::testFixtureWiresArtworkPreviewForwardersThroughScrollManager() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *scroll = win->getApplicationManager()->getScrollManager();
  QVERIFY(scroll);

  // ScrollManager exposes the artwork-preview state through two
  // thin forwarders that delegate into m_selectionDisplay. A regression
  // that left m_selectionDisplay null after setup (or wired the wrong
  // unique_ptr) would surface either as a crash here or as a true
  // return from a freshly-constructed window where no preview was
  // ever shown.
  QVERIFY(!scroll->isArtworkPreviewVisible());
  QCOMPARE(scroll->hideArtworkPreview(), false);
}

// Kartend-4hr3d: the preview overlay was parented to the media scroll area,
// which CoverFlowController::applyVisibility HIDES whenever cover flow is the
// active view (setVisible(!active)). A child of a hidden widget can never
// become visible, so a preview requested from the cover-flow gallery strip was
// constructed and shown into nothing — the click looked dead. Asserting on the
// PARENT rather than on visibility keeps this honest in an offscreen test run,
// where nothing is truly shown either way.
void TestSelectionDisplayManager::testPreviewOverlayIsNotParentedToTheHideableScrollArea() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *scroll = win->getApplicationManager()->getScrollManager();
  QVERIFY(scroll);

  // Lazily constructs the overlay. Path resolution is irrelevant here — the
  // call is only a vehicle for ensureArtworkPreviewOverlay().
  scroll->showPreviewAtPath(QStringLiteral("/nonexistent/art.png"), /*isVideo=*/false);

  auto *overlay = win->findChild<ArtworkPreviewOverlay *>();
  QVERIFY2(overlay, "showPreviewAtPath did not construct the overlay");

  QWidget *parent = overlay->parentWidget();
  QVERIFY(parent);
  // The window itself is never hidden while the app is up; the scroll area is
  // hidden for the whole time cover flow is on screen.
  // The window is never hidden while the app is up; the media scroll area is
  // hidden for the whole time cover flow is on screen. Requiring the top-level
  // window rules that parent out by construction.
  QCOMPARE(parent, static_cast<QWidget *>(win));
  QVERIFY2(!qobject_cast<QScrollArea *>(parent),
           "overlay is parented to a scroll area — cover flow hides the media scroll area, so a "
           "preview parented there can never appear");
}
