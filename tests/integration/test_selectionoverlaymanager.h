/**
 * @file test_selectionoverlaymanager.h
 * @brief Integration tests for SelectionOverlayManager's QWidget + animation
 *        surface.
 *
 * The manager owns a child QWidget overlay and a QPropertyAnimation that
 * targets its geometry; both are too entangled for a pure unit test (they
 * need QApplication + a real parent widget hierarchy). The geometry math
 * has its own coverage in tests/modules/overlay/test_overlayhelpers.cpp;
 * this suite exercises the higher-level state transitions and lifecycle
 * the helpers can't reach.
 */

#ifndef KARTEND_TESTS_TEST_SELECTIONOVERLAYMANAGER_H
#define KARTEND_TESTS_TEST_SELECTIONOVERLAYMANAGER_H

#include <QObject>

class TestSelectionOverlayManager : public QObject {
  Q_OBJECT

private slots:
  // Lifecycle / wiring
  void testConstructionWithoutParentWidget();
  void testSetParentWidgetReparentsExistingOverlay();
  void testDestructionStopsRunningAnimation();

  // Force-visible state
  void testSetForceVisibleTogglesShouldKeepVisible();
  void testSetForceVisibleNoChangeIsIdempotent();

  // Show / hide surface
  void testShowAtRectAfterParentWidgetSet();
  void testShowAtRectIgnoresInvalidRect();
  void testHideHidesOverlay();
  void testIsVisibleFalseBeforeFirstShow();

  // Geometry surface
  void testSetGeometryReflectsImmediately();
  void testGeometryReturnsEmptyBeforeOverlayCreated();

  // Animation surface
  void testAnimateToIgnoresInvalidTarget();
  void testStopAnimationIsSafeWhenIdle();
};

#endif // KARTEND_TESTS_TEST_SELECTIONOVERLAYMANAGER_H
