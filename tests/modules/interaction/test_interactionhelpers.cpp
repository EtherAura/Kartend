// Tests for InteractionHelpers — pure helpers extracted from InteractionManager.
//
// These tests use plain integers / view-type values and exercise the helpers
// without instantiating any Qt object infrastructure.

#include <QTest>

#include "collectiontypes.h"
#include "interactionhelpers.h"

class TestInteractionHelpers : public QObject {
  Q_OBJECT
private slots:
  // stepSizeForViewType
  void stepSizeListIsOne();
  void stepSizeCoverFlowIsOne();
  void stepSizeGridIsGridWidth();
  void stepSizeHorizontalIsGridWidth();
  void stepSizeFallsBackToOneForNonPositiveGridWidth();

  // computeVerticalHoldStep — guard rails
  void holdStepReturnsInvalidWhenTotalItemsZeroOrNegative();
  void holdStepReturnsInvalidWhenStepSizeZeroOrNegative();

  // computeVerticalHoldStep — clamp behaviour (wrap == false)
  void holdStepClampsToZeroAtTopEdge();
  void holdStepClampsToLastIndexAtBottomEdge();
  void holdStepReturnsCurrentWhenAlreadyAtEdgeWithoutWrap();
  void holdStepNormalAdvanceDownDoesNotWrap();
  void holdStepNormalAdvanceUpDoesNotWrap();

  // computeVerticalHoldStep — wrap behaviour (wrap == true)
  void holdStepWrapsBelowZeroToEnd();
  void holdStepWrapsAboveLastIndexToStart();
  void holdStepDoesNotWrapWhenStepStaysInRange();
  void holdStepWrapsLargeNegativeStep();
  void holdStepWrapsLargePositiveStep();

  // resolveOwnerIndex
  void resolvePrefersDbIndexWhenInRange();
  void resolveFallsBackToCurrentIndexWhenDbInvalid();
  void resolveReturnsMinusOneWhenBothInvalid();
  void resolveTreatsOutOfRangeDbIndexAsInvalid();
  void resolveTreatsOutOfRangeFallbackAsInvalid();
  void resolveReturnsMinusOneWhenCollectionsEmpty();

  // classifyContextTarget
  void contextTargetMediaItemWithPathShowsLaunch();
  void contextTargetMediaItemWithoutPathHidesLaunch();
  void contextTargetSubcollectionShowsOpenOnly();
  void contextTargetVirtualFolderShowsOpenOnly();

  // playlistContextFlags
  void playlistFlagsAllOffOutsidePlaylist();
  void playlistFlagsStaticPlaylistOffersRemoveAndDelete();
  void playlistFlagsSmartPlaylistOffersEditFilterNotRemove();
  void playlistFlagsReservedPlaylistHidesDelete();

  // pickLauncherIndex
  void launcherPickPrefersValidOverride();
  void launcherPickIgnoresOutOfRangeOverride();
  void launcherPickClampsDefaultIntoRange();
  void launcherPickReturnsZeroWhenNoLaunchers();

  // classifyExpandActivation
  void expandActivationLaunchesDirectlyWhenModeOff();
  void expandActivationCollapsesWhenSameItemStillVisible();
  void expandActivationReExpandsWhenOverlayDismissed();
  void expandActivationExpandsOnDifferentItem();
  void expandActivationNegativeIndexNeverCollapses();

  // displayTitleForFilePath
  void displayTitleFlattensUnderscoresAndWhitespace();
  void displayTitleDropsExtensionKeepsInnerDots();
  void displayTitleEmptyPathYieldsEmptyTitle();
};

// ------------------------------ stepSizeForViewType -------------------------

void TestInteractionHelpers::stepSizeListIsOne() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::List, 5), 1);
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::List, 1), 1);
}

void TestInteractionHelpers::stepSizeCoverFlowIsOne() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::CoverFlow, 5), 1);
}

void TestInteractionHelpers::stepSizeGridIsGridWidth() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, 5), 5);
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, 12), 12);
}

void TestInteractionHelpers::stepSizeHorizontalIsGridWidth() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Horizontal, 4), 4);
}

void TestInteractionHelpers::stepSizeFallsBackToOneForNonPositiveGridWidth() {
  // Defensive fallback: gridWidth <= 0 should never happen at the call site
  // (the manager short-circuits earlier), but the helper must remain usable.
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, 0), 1);
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, -3), 1);
}

// ------------------------------ computeVerticalHoldStep — guards ------------

void TestInteractionHelpers::holdStepReturnsInvalidWhenTotalItemsZeroOrNegative() {
  const auto a = InteractionHelpers::computeVerticalHoldStep(/*currentIndex=*/0,
                                                             /*direction=*/1,
                                                             /*stepSize=*/1,
                                                             /*totalItems=*/0,
                                                             /*wrap=*/true);
  QCOMPARE(a.nextIndex, -1);
  QVERIFY(!a.didWrap);

  const auto b = InteractionHelpers::computeVerticalHoldStep(0, 1, 1, -5, false);
  QCOMPARE(b.nextIndex, -1);
  QVERIFY(!b.didWrap);
}

void TestInteractionHelpers::holdStepReturnsInvalidWhenStepSizeZeroOrNegative() {
  const auto a = InteractionHelpers::computeVerticalHoldStep(0, 1, 0, 10, true);
  QCOMPARE(a.nextIndex, -1);
  const auto b = InteractionHelpers::computeVerticalHoldStep(0, 1, -2, 10, true);
  QCOMPARE(b.nextIndex, -1);
}

// ------------------------------ computeVerticalHoldStep — clamp -------------

void TestInteractionHelpers::holdStepClampsToZeroAtTopEdge() {
  // currentIndex=2, direction=-1, stepSize=5, totalItems=10, wrap=false
  // raw next = 2 - 5 = -3 → clamp to 0.
  const auto step = InteractionHelpers::computeVerticalHoldStep(2, -1, 5, 10, false);
  QCOMPARE(step.nextIndex, 0);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepClampsToLastIndexAtBottomEdge() {
  // currentIndex=8, direction=+1, stepSize=5, totalItems=10, wrap=false
  // raw next = 13 → clamp to 9.
  const auto step = InteractionHelpers::computeVerticalHoldStep(8, 1, 5, 10, false);
  QCOMPARE(step.nextIndex, 9);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepReturnsCurrentWhenAlreadyAtEdgeWithoutWrap() {
  // currentIndex=0, direction=-1: clamps right back to 0.
  const auto top = InteractionHelpers::computeVerticalHoldStep(0, -1, 5, 10, false);
  QCOMPARE(top.nextIndex, 0);

  // currentIndex=9, direction=+1: clamps to 9.
  const auto bot = InteractionHelpers::computeVerticalHoldStep(9, 1, 5, 10, false);
  QCOMPARE(bot.nextIndex, 9);
}

void TestInteractionHelpers::holdStepNormalAdvanceDownDoesNotWrap() {
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, 1, 5, 100, false);
  QCOMPARE(step.nextIndex, 5);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepNormalAdvanceUpDoesNotWrap() {
  const auto step = InteractionHelpers::computeVerticalHoldStep(20, -1, 5, 100, false);
  QCOMPARE(step.nextIndex, 15);
  QVERIFY(!step.didWrap);
}

// ------------------------------ computeVerticalHoldStep — wrap --------------

void TestInteractionHelpers::holdStepWrapsBelowZeroToEnd() {
  // currentIndex=2, direction=-1, stepSize=5, totalItems=10, wrap=true
  // raw next = -3 → ((-3 % 10) + 10) % 10 = 7.
  const auto step = InteractionHelpers::computeVerticalHoldStep(2, -1, 5, 10, true);
  QCOMPARE(step.nextIndex, 7);
  QVERIFY(step.didWrap);
}

void TestInteractionHelpers::holdStepWrapsAboveLastIndexToStart() {
  // currentIndex=8, direction=+1, stepSize=5, totalItems=10, wrap=true
  // raw next = 13 → 13 % 10 = 3.
  const auto step = InteractionHelpers::computeVerticalHoldStep(8, 1, 5, 10, true);
  QCOMPARE(step.nextIndex, 3);
  QVERIFY(step.didWrap);
}

void TestInteractionHelpers::holdStepDoesNotWrapWhenStepStaysInRange() {
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, 1, 5, 10, true);
  QCOMPARE(step.nextIndex, 5);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepWrapsLargeNegativeStep() {
  // raw next = 0 - 23 = -23 → ((-23 % 10) + 10) % 10 = 7.
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, -1, 23, 10, true);
  QCOMPARE(step.nextIndex, 7);
  QVERIFY(step.didWrap);
}

void TestInteractionHelpers::holdStepWrapsLargePositiveStep() {
  // raw next = 0 + 23 = 23 → 23 % 10 = 3.
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, 1, 23, 10, true);
  QCOMPARE(step.nextIndex, 3);
  QVERIFY(step.didWrap);
}

// ------------------------------ resolveOwnerIndex ---------------------------

void TestInteractionHelpers::resolvePrefersDbIndexWhenInRange() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(2, 0, 5), 2);
}

void TestInteractionHelpers::resolveFallsBackToCurrentIndexWhenDbInvalid() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(-1, 3, 5), 3);
}

void TestInteractionHelpers::resolveReturnsMinusOneWhenBothInvalid() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(-1, -1, 5), -1);
}

void TestInteractionHelpers::resolveTreatsOutOfRangeDbIndexAsInvalid() {
  // dbIndex >= collectionsSize is treated as invalid → fallback wins.
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(99, 1, 5), 1);
}

void TestInteractionHelpers::resolveTreatsOutOfRangeFallbackAsInvalid() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(-1, 99, 5), -1);
}

void TestInteractionHelpers::resolveReturnsMinusOneWhenCollectionsEmpty() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(0, 0, 0), -1);
}

// ------------------------------ classifyContextTarget -----------------------

void TestInteractionHelpers::contextTargetMediaItemWithPathShowsLaunch() {
  const auto flags = InteractionHelpers::classifyContextTarget(
      /*isSubcollection=*/false, /*isVirtualFolder=*/false, /*hasFilePath=*/true);
  QVERIFY(flags.isMediaItem);
  QVERIFY(flags.showLaunch);
  QVERIFY(!flags.showOpen);
}

void TestInteractionHelpers::contextTargetMediaItemWithoutPathHidesLaunch() {
  // A media tile without a resolved path has nothing to hand the launcher —
  // "Launch" must not be offered even though the item is media-kind.
  const auto flags = InteractionHelpers::classifyContextTarget(false, false, false);
  QVERIFY(flags.isMediaItem);
  QVERIFY(!flags.showLaunch);
  QVERIFY(!flags.showOpen);
}

void TestInteractionHelpers::contextTargetSubcollectionShowsOpenOnly() {
  // Subcollection tiles get "Open" (the enter-equivalent) and never "Launch",
  // even when a stray file path is attached.
  const auto flags = InteractionHelpers::classifyContextTarget(true, false, true);
  QVERIFY(!flags.isMediaItem);
  QVERIFY(!flags.showLaunch);
  QVERIFY(flags.showOpen);
}

void TestInteractionHelpers::contextTargetVirtualFolderShowsOpenOnly() {
  const auto flags = InteractionHelpers::classifyContextTarget(false, true, false);
  QVERIFY(!flags.isMediaItem);
  QVERIFY(!flags.showLaunch);
  QVERIFY(flags.showOpen);
}

// ------------------------------ playlistContextFlags ------------------------

void TestInteractionHelpers::playlistFlagsAllOffOutsidePlaylist() {
  // Outside a playlist none of the playlist-scoped actions apply, regardless
  // of the (meaningless) smart/reserved inputs.
  const auto flags = InteractionHelpers::playlistContextFlags(
      /*insidePlaylist=*/false, /*isSmartPlaylist=*/true, /*isReservedPlaylist=*/false);
  QVERIFY(!flags.showRemoveFromPlaylist);
  QVERIFY(!flags.showEditSmartFilter);
  QVERIFY(!flags.showDeletePlaylist);
}

void TestInteractionHelpers::playlistFlagsStaticPlaylistOffersRemoveAndDelete() {
  const auto flags = InteractionHelpers::playlistContextFlags(true, false, false);
  QVERIFY(flags.showRemoveFromPlaylist);
  QVERIFY(!flags.showEditSmartFilter);
  QVERIFY(flags.showDeletePlaylist);
}

void TestInteractionHelpers::playlistFlagsSmartPlaylistOffersEditFilterNotRemove() {
  // Removal from a smart playlist would not stick (the next open re-evaluates
  // the filter), so the action is hidden; the filter editor appears instead.
  const auto flags = InteractionHelpers::playlistContextFlags(true, true, false);
  QVERIFY(!flags.showRemoveFromPlaylist);
  QVERIFY(flags.showEditSmartFilter);
  QVERIFY(flags.showDeletePlaylist);
}

void TestInteractionHelpers::playlistFlagsReservedPlaylistHidesDelete() {
  // Built-ins (favorites) keep rename but hide delete — PlaylistManager
  // refuses the call anyway.
  const auto flags = InteractionHelpers::playlistContextFlags(true, false, true);
  QVERIFY(flags.showRemoveFromPlaylist);
  QVERIFY(!flags.showDeletePlaylist);
}

// ------------------------------ pickLauncherIndex ---------------------------

void TestInteractionHelpers::launcherPickPrefersValidOverride() {
  QCOMPARE(InteractionHelpers::pickLauncherIndex(/*override=*/2, /*default=*/0, /*count=*/4), 2);
}

void TestInteractionHelpers::launcherPickIgnoresOutOfRangeOverride() {
  // A stale override (launcher list shrank since it was stored) must fall
  // back to the default, not index past the end.
  QCOMPARE(InteractionHelpers::pickLauncherIndex(/*override=*/7, /*default=*/1, /*count=*/3), 1);
  QCOMPARE(InteractionHelpers::pickLauncherIndex(/*override=*/-1, /*default=*/1, /*count=*/3), 1);
}

void TestInteractionHelpers::launcherPickClampsDefaultIntoRange() {
  // defaultLauncherIndex can also go stale; it clamps to the last launcher.
  QCOMPARE(InteractionHelpers::pickLauncherIndex(-1, /*default=*/9, /*count=*/3), 2);
  QCOMPARE(InteractionHelpers::pickLauncherIndex(-1, /*default=*/-4, /*count=*/3), 0);
}

void TestInteractionHelpers::launcherPickReturnsZeroWhenNoLaunchers() {
  // The "no configured launchers" placeholder the preview path uses.
  QCOMPARE(InteractionHelpers::pickLauncherIndex(-1, 0, /*count=*/0), 0);
  QCOMPARE(InteractionHelpers::pickLauncherIndex(3, 2, /*count=*/-1), 0);
}

// ------------------------------ classifyExpandActivation --------------------

void TestInteractionHelpers::expandActivationLaunchesDirectlyWhenModeOff() {
  QCOMPARE(InteractionHelpers::classifyExpandActivation(/*enabled=*/false, /*expanded=*/3,
                                                        /*activation=*/3, /*visible=*/true),
           InteractionHelpers::ExpandActivation::LaunchDirectly);
}

void TestInteractionHelpers::expandActivationCollapsesWhenSameItemStillVisible() {
  // Second activation on the already-expanded item while the overlay is up
  // → collapse and fall through to launch.
  QCOMPARE(InteractionHelpers::classifyExpandActivation(true, 3, 3, true),
           InteractionHelpers::ExpandActivation::CollapseThenLaunch);
}

void TestInteractionHelpers::expandActivationReExpandsWhenOverlayDismissed() {
  // The user dismissed the overlay (click outside) without changing
  // selection: the next activation is a fresh first-stage expand, not a
  // launch.
  QCOMPARE(InteractionHelpers::classifyExpandActivation(true, 3, 3, /*visible=*/false),
           InteractionHelpers::ExpandActivation::TryExpand);
}

void TestInteractionHelpers::expandActivationExpandsOnDifferentItem() {
  QCOMPARE(
      InteractionHelpers::classifyExpandActivation(true, /*expanded=*/1, /*activation=*/4, true),
      InteractionHelpers::ExpandActivation::TryExpand);
}

void TestInteractionHelpers::expandActivationNegativeIndexNeverCollapses() {
  // expandedItemIndex == activationIndex == -1 (nothing expanded, no valid
  // activation) must not read as "same item" — it is a first-stage expand.
  QCOMPARE(InteractionHelpers::classifyExpandActivation(true, -1, -1, true),
           InteractionHelpers::ExpandActivation::TryExpand);
}

// ------------------------------ displayTitleForFilePath ---------------------

void TestInteractionHelpers::displayTitleFlattensUnderscoresAndWhitespace() {
  QCOMPARE(InteractionHelpers::displayTitleForFilePath(
               QStringLiteral("/media/videos/My_Holiday__Reel.mp4")),
           QStringLiteral("My Holiday Reel"));
}

void TestInteractionHelpers::displayTitleDropsExtensionKeepsInnerDots() {
  // completeBaseName drops only the part after the LAST dot.
  QCOMPARE(
      InteractionHelpers::displayTitleForFilePath(QStringLiteral("/audio/Album v1.2_final.flac")),
      QStringLiteral("Album v1.2 final"));
}

void TestInteractionHelpers::displayTitleEmptyPathYieldsEmptyTitle() {
  QVERIFY(InteractionHelpers::displayTitleForFilePath(QString()).isEmpty());
}

QTEST_APPLESS_MAIN(TestInteractionHelpers)
#include "test_interactionhelpers.moc"
