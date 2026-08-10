/**
 * @file test_selectiondisplaymanager.h
 * @brief Integration tests for SelectionDisplayManager's public surface.
 *
 * Construction defaults, sub-object access, column-width persistence,
 * preview-overlay state machine, and defensive early-returns are exercised
 * on a bare instance — they have a clean DI seam (setters / applyGeneralSettings)
 * and need no MainWindow scaffolding.
 *
 * The fixture-backed test confirms ScrollManager's forwarder methods
 * (isArtworkPreviewVisible / hideArtworkPreview) reach the live
 * SelectionDisplayManager owned inside ScrollManager. A regression that
 * left the unique_ptr null after setup would surface here as a
 * forwarder crash or wrong-state return.
 *
 * The full selection-update path (updateSelectionForIndex,
 * refreshSelectionOverlayState, onArrowKeyViewUpdate) requires the
 * SelectionCoordinator + GridMetrics + activeWidgets graph from
 * ScrollManager — its end-to-end behaviour is covered by
 * test_scrollmanager.cpp / test_selectionoverlaymanager.cpp.
 */

#ifndef KARTEND_TESTS_TEST_SELECTIONDISPLAYMANAGER_H
#define KARTEND_TESTS_TEST_SELECTIONDISPLAYMANAGER_H

#include <QObject>

class TestSelectionDisplayManager : public QObject {
  Q_OBJECT

private slots:
  void testConstructionInitialDefaults();
  void testApplyGeneralSettingsUpdatesPositiveWidths();
  void testApplyGeneralSettingsIgnoresZeroOrNegativeWidths();
  void testApplyGeneralSettingsNullPtrIsNoOp();
  void testArtworkPreviewNotVisibleAtConstruction();
  void testHideArtworkPreviewReturnsFalseWhenNotVisible();
  void testShowArtworkPreviewWithoutScrollAreaIsNoOp();
  void testDestroyListHeaderIsIdempotent();
  void testUpdateListHeaderEarlyReturnsWithoutContextOrMetrics();
  void testSelectionOverlayRectForInvalidIndexReturnsEmpty();

  void testFixtureWiresArtworkPreviewForwardersThroughScrollManager();
  // Kartend-4hr3d
  void testPreviewOverlayIsNotParentedToTheHideableScrollArea();
};

#endif // KARTEND_TESTS_TEST_SELECTIONDISPLAYMANAGER_H
