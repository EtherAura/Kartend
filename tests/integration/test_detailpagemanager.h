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
 * Payload assembly (4 DB queries + artwork resolution) inside
 * showForCurrentSelection needs the DatabaseManager + DetailsPaneManager
 * graph and is exercised by the live UI smoke and the existing details-pane
 * fixture tests. Here we lock the defensive null-guards and forwarder contracts.
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

  void testFixtureExposesDetailPageManagerViaApplicationManager();
};

#endif // KARTEND_TESTS_TEST_DETAILPAGEMANAGER_H
