#include "test_scrolleventscontroller.h"

#include "applicationmanager.h"
#include "collection/generalsettings.h"
#include "collectiontypes.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "mocks/mockedmainwindowfixture.h"
#include "mocks/mocksettingsmanager.h"
#include "scrolleventscontroller.h"

#include <QTest>

namespace {

/// MockSettingsManager with a call recorder on saveGeneralSettings — the
/// only persistence hook the tested slots reach.
class RecordingSettingsManager : public KartendTest::MockSettingsManager {
public:
  using KartendTest::MockSettingsManager::MockSettingsManager;

  int saveGeneralSettingsCalls = 0;
  GeneralSettings lastSaved;

  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) override {
    ++saveGeneralSettingsCalls;
    lastSaved = settings;
    return ErrorUtils::Result<void>::success();
  }
};

} // namespace

void TestScrollEventsController::listColumnWidths_persistThroughSettingsManager() {
  GeneralSettings settings;
  RecordingSettingsManager recorder;
  ScrollEventsController controller;
  ScrollEventsControllerContext ctx;
  ctx.getGeneralSettings = [&settings]() { return &settings; };
  ctx.getSettingsManager = [&recorder]() -> ISettingsManager * { return &recorder; };
  controller.setContext(ctx);

  controller.onListColumnWidthChanged(342);
  QCOMPARE(settings.view.listCollectionColumnWidth, 342);
  QCOMPARE(recorder.saveGeneralSettingsCalls, 1);
  QCOMPARE(recorder.lastSaved.view.listCollectionColumnWidth, 342);

  controller.onListArtworkColumnWidthChanged(517);
  QCOMPARE(settings.view.listArtworkColumnWidth, 517);
  QCOMPARE(recorder.saveGeneralSettingsCalls, 2);
  QCOMPARE(recorder.lastSaved.view.listArtworkColumnWidth, 517);
}

void TestScrollEventsController::listColumnWidths_updateSettingsEvenWithoutSettingsManager() {
  // The write-through must not depend on the persistence layer being wired
  // yet — the in-memory settings still update (they're re-saved later).
  GeneralSettings settings;
  ScrollEventsController controller;
  ScrollEventsControllerContext ctx;
  ctx.getGeneralSettings = [&settings]() { return &settings; };
  controller.setContext(ctx);

  controller.onListColumnWidthChanged(211);
  controller.onListArtworkColumnWidthChanged(97);
  QCOMPARE(settings.view.listCollectionColumnWidth, 211);
  QCOMPARE(settings.view.listArtworkColumnWidth, 97);
}

void TestScrollEventsController::listColumnWidths_preferDebouncedSaverOverDirectSave() {
  // A header drag fires these slots once per mouse-move tick. With the
  // host's debounced saver wired, each tick must write through to the live
  // struct and route the persist to the saver — never a direct (per-tick)
  // saveGeneralSettings.
  GeneralSettings settings;
  RecordingSettingsManager recorder;
  int scheduleCalls = 0;
  ScrollEventsController controller;
  ScrollEventsControllerContext ctx;
  ctx.getGeneralSettings = [&settings]() { return &settings; };
  ctx.getSettingsManager = [&recorder]() -> ISettingsManager * { return &recorder; };
  ctx.scheduleSettingsSave = [&scheduleCalls]() { ++scheduleCalls; };
  controller.setContext(ctx);

  controller.onListColumnWidthChanged(300);
  controller.onListColumnWidthChanged(301);
  controller.onListColumnWidthChanged(302);
  controller.onListArtworkColumnWidthChanged(96);

  QCOMPARE(settings.view.listCollectionColumnWidth, 302);
  QCOMPARE(settings.view.listArtworkColumnWidth, 96);
  QCOMPARE(scheduleCalls, 4);
  QCOMPARE(recorder.saveGeneralSettingsCalls, 0);
}

void TestScrollEventsController::sortModeChange_withoutNavigationManagerLeavesSettingsUntouched() {
  // The slot bails before touching settings when no NavigationManager is
  // available — a reload it can't perform must not desync the persisted
  // sort mode from the visible one.
  GeneralSettings settings;
  settings.view.sortMode = SortMode::NameAscending;
  RecordingSettingsManager recorder;
  ScrollEventsController controller;
  ScrollEventsControllerContext ctx;
  ctx.getGeneralSettings = [&settings]() { return &settings; };
  ctx.getSettingsManager = [&recorder]() -> ISettingsManager * { return &recorder; };
  controller.setContext(ctx);

  controller.onSortModeChangeRequested(SortMode::SizeDescending);
  QCOMPARE(int(settings.view.sortMode), int(SortMode::NameAscending));
  QCOMPARE(recorder.saveGeneralSettingsCalls, 0);
}

void TestScrollEventsController::sortModeChange_updatesAndPersistsSortMode() {
  KartendTest::MockedMainWindowFixture fixture;
  auto *nav = fixture.window()->getApplicationManager()->getNavigationManager();
  QVERIFY(nav);

  GeneralSettings settings;
  settings.view.sortMode = SortMode::NameAscending;
  RecordingSettingsManager recorder;
  ScrollEventsController controller;
  ScrollEventsControllerContext ctx;
  ctx.getNavigationManager = [nav]() { return nav; };
  ctx.getGeneralSettings = [&settings]() { return &settings; };
  ctx.getSettingsManager = [&recorder]() -> ISettingsManager * { return &recorder; };
  // -1 keeps safeReloadCollection on its out-of-range guard; the slot's
  // settings write + persist must happen regardless.
  ctx.getCurrentCollectionIndex = []() { return -1; };
  controller.setContext(ctx);

  controller.onSortModeChangeRequested(SortMode::SizeDescending);
  QCOMPARE(int(settings.view.sortMode), int(SortMode::SizeDescending));
  QCOMPARE(recorder.saveGeneralSettingsCalls, 1);
  QCOMPARE(int(recorder.lastSaved.view.sortMode), int(SortMode::SizeDescending));
}

// Kartend-ic4h6: onSelectItemByIndex serves cover flow's user-driven selection
// (every emit site of CoverFlowWidget::selectionChangeRequested is a user
// gesture), yet unlike the grid click / arrow-key / alphabetic-jump paths it
// never cancelled the pending automatic restore. SelectionRestoreCoordinator's
// staggered verification timers then read "current != restore target" as "the
// restore has not landed" and re-asserted the restored index — snapping the
// selection back to the first item seconds after the user moved. The cancel is
// observable as the userSelectionMade flag (the exact gate
// beginSelectionRestore's skip-guard reads) plus a restoreToken bump (the gate
// the coordinator's own validators read), so this pins both revert defences.
void TestScrollEventsController::selectItemByIndex_cancelsPendingRestoreSoAVerifyCannotRevert() {
  KartendTest::MockedMainWindowFixture fixture;
  auto *interaction = fixture.window()->getApplicationManager()->getInteractionManager();
  QVERIFY(interaction);

  ScrollEventsController controller;
  ScrollEventsControllerContext ctx;
  ctx.getInteractionManager = [interaction]() { return interaction; };
  controller.setContext(ctx);

  // The state a freshly scheduled restore leaves behind: restore pending, no
  // user input yet. This is the window the verification timers fire into.
  auto &restore = interaction->state().selectionRestore();
  restore.restorePending = true;
  restore.userSelectionMade = false;
  const int tokenBefore = restore.restoreToken;

  // The user moves the selection in cover flow.
  controller.onSelectItemByIndex(3);

  // Both revert defences must now be armed: the flag beginSelectionRestore
  // checks before overriding anything, and the token bump that makes the
  // coordinator's captured validators reject their stale restore.
  QVERIFY2(restore.userSelectionMade,
           "onSelectItemByIndex did not mark the selection user-made — a pending restore "
           "verification would revert the user's cover-flow selection");
  QVERIFY2(restore.restoreToken > tokenBefore,
           "onSelectItemByIndex did not invalidate the restore token — the coordinator's "
           "staggered verify lambdas would still pass restoreStillValid");
  QVERIFY(!restore.restorePending);

  // And the flag genuinely gates a late verification: a beginSelectionRestore
  // arriving now must leave no trace of a restore having run.
  interaction->beginSelectionRestore(0);
  QVERIFY(restore.userSelectionMade); // unchanged — the skip path touches nothing
  QCOMPARE(restore.targetIndex, -1);
}

void TestScrollEventsController::selectItemByIndex_withoutInteractionManagerIsANoOp() {
  // Guard parity with the other slots: an unwired context must not crash.
  ScrollEventsController controller;
  ScrollEventsControllerContext ctx;
  controller.setContext(ctx);
  controller.onSelectItemByIndex(7);
}
