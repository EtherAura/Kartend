#include "test_interactionmanager.h"

#include "applicationmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "selectionmanager.h"
// Kartend-xrj9r: this suite asserts only on in-memory coordinator state
// (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "mocks/mockedmainwindowfixture.h"

#include <QTest>

namespace {
InteractionManager *interaction(KartendTest::MockedMainWindowFixture &fixture) {
  return fixture.window()->getApplicationManager()->getInteractionManager();
}
} // namespace

void TestInteractionManager::testSubManagersAreWired() {
  // The construction contract: ApplicationManager builds InteractionManager and
  // every sub-manager it coordinates. A null here means a wiring regression
  // that the rest of the input stack would crash on at first interaction.
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);
  QVERIFY(im->animationManager());
  QVERIFY(im->selectionManager());
  QVERIFY(im->keyboardManager());
  QVERIFY(im->mouseManager());
  QVERIFY(im->viewportManager());
  QVERIFY(im->eventManager());
  QVERIFY(im->searchManager());
  QVERIFY(im->launchManager());
}

void TestInteractionManager::testClearSelectionYieldsNoSelection() {
  // No selection invariant: after clearSelection() the index is -1 and the
  // selected file path is empty. The whole input stack treats index == -1 as
  // "nothing selected", so this contract is load-bearing.
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);

  im->clearSelection();
  QCOMPARE(im->currentSelectedIndex(), -1);
  QVERIFY(im->selectedFilePath().isEmpty());
}

void TestInteractionManager::testClearSelectionAndFocusYieldsNoSelection() {
  // clearSelectionAndFocus() is the IInteraction override the navigation /
  // overlay code calls when leaving a view; it must land in the same
  // no-selection state as clearSelection().
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);

  im->clearSelectionAndFocus();
  QCOMPARE(im->currentSelectedIndex(), -1);
}

void TestInteractionManager::testIsWheelScrollingInitiallyFalse() {
  // A freshly-built manager isn't mid wheel-scroll; the attract / idle paths
  // gate on this, so a stuck-true would suppress them forever.
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);
  QVERIFY(!im->isWheelScrolling());
}

void TestInteractionManager::testStopRepeatResetsTransientInputState() {
  // stopRepeat is the single funnel every input surface (key release, mouse
  // release, focus loss) uses to end continuous navigation. It must clear
  // the transient hold/click flags, or the next interaction starts in a
  // phantom "still holding" state (stuck fast-scroll, swallowed first click).
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);

  im->state().scroll().keyContinuous = true;
  im->state().scroll().horizHoldActive = true;
  im->state().click().armFirstClickDelay = true;
  im->state().click().pendingInitialCenter = true;

  im->stopRepeat();

  QVERIFY(!im->state().scroll().keyContinuous);
  QVERIFY(!im->state().scroll().horizHoldActive);
  QVERIFY(!im->state().click().armFirstClickDelay);
  QVERIFY(!im->state().click().pendingInitialCenter);
}

void TestInteractionManager::testStopRepeatFlushesPendingSuppressedSelection() {
  // During a hold-scroll row change the selection commit is deferred
  // (selectionSuppressed + pendingSelectionIndex). Releasing the hold routes
  // through stopRepeat, which must consume the pending pair — leaving it
  // armed would re-apply a stale selection on the next unrelated update.
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);

  im->state().click().selectionSuppressed = true;
  im->state().click().pendingSelectionIndex = 3;

  im->stopRepeat();

  QVERIFY(!im->state().click().selectionSuppressed);
  QCOMPARE(im->state().click().pendingSelectionIndex, -1);
}

void TestInteractionManager::testArtworkPreviewLaunchRequestClearsExpandState() {
  // The overlay-activation slot must always tear down the expand-mode state,
  // even when it cannot resolve a path to launch (empty view here) — a
  // lingering expandedItemIndex would make the NEXT activation on that index
  // read as second-stage and launch instead of expanding.
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);

  im->state().setExpandedItemIndex(5);
  im->onArtworkPreviewLaunchRequested(QString());

  QCOMPARE(im->state().expandedItemIndex(), -1);
  // Nothing was launched or selected as a side effect.
  QCOMPARE(im->currentSelectedIndex(), -1);
}

void TestInteractionManager::testSelectionChangeNotificationResetsExpandState() {
  // Wiring contract: SelectionManager::selectionChanged → expand state reset,
  // so moving the selection between the two activations of expand-mode makes
  // the next activation a fresh first-stage expand (never a surprise launch
  // of the newly-selected item).
  KartendTest::MockedMainWindowFixture fixture;
  InteractionManager *im = interaction(fixture);
  QVERIFY(im);

  im->state().setExpandedItemIndex(7);
  im->selectionManager()->notifySelectionChanged();

  QCOMPARE(im->state().expandedItemIndex(), -1);
}
