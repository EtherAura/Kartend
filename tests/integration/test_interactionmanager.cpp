#include "test_interactionmanager.h"

#include "applicationmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
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
