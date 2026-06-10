/**
 * @file test_detailpagemanager.h
 * @brief Integration tests for DetailPageManager's public surface.
 *
 * DetailPageManager is a thin coordinator: state lives in the overlay it
 * forwards to. We test the three external entry points (showForCurrentSelection,
 * hideOverlay, isOverlayActive) on a bare manager and on a manager wired to
 * a stub IDetailPageOverlay that records calls. The fixture-backed test
 * confirms the live wiring through ApplicationManager.
 *
 * Kartend-4p8o made the load asynchronous: showForCurrentSelection shows the
 * overlay immediately with cheap fields, then dispatches loadItemDetailAsync
 * and refreshes the overlay when itemDetailLoaded fires. The async tests below
 * drive that orchestration with a stub DetailsPaneManager + a capturing
 * DatabaseManager, asserting the cheap-first show, the request dispatch, the
 * post-result update, and that a stale result (token mismatch) is dropped. The
 * actual off-thread DB/filesystem work is covered by the live UI.
 */

#ifndef KARTEND_TESTS_TEST_DETAILPAGEMANAGER_H
#define KARTEND_TESTS_TEST_DETAILPAGEMANAGER_H

#include <QObject>

class TestDetailPageManager : public QObject {
  Q_OBJECT

private slots:
  void testConstructionInitialDefaults();
  void testHideOverlayWithoutOverlayIsNoOp();
  void testShowForCurrentSelectionWithoutOverlayIsNoOp();
  void testIsOverlayActiveDelegatesToOverlay();
  void testHideOverlayDelegatesToOverlay();
  void testShowForCurrentSelectionWithoutContextIsNoOp();

  // Kartend-4p8o: async load orchestration.
  void testShowForCurrentSelectionShowsCheapOverlayThenRequestsLoad();
  void testItemDetailLoadedPopulatesOverlay();
  void testStaleItemDetailResultIsIgnored();

  void testFixtureExposesDetailPageManagerViaApplicationManager();
};

#endif // KARTEND_TESTS_TEST_DETAILPAGEMANAGER_H
