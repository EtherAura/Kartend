/**
 * @file test_virtualcontainermanager.h
 * @brief Integration tests for VirtualContainerManager's QWidget lifecycle.
 *
 * VirtualContainerManager owns a child QWidget that hosts the virtualised
 * scrollable item grid plus the QScrollArea / QScrollBar queries derived
 * from it. The createContainer / cleanupContainer cycle and the scroll-
 * area accessors are the public surface tested here; the
 * positionContainer math has its own coverage via behaviour-driven
 * scenarios in tests/integration/test_scrollmanager.cpp.
 */

#ifndef KARTEND_TESTS_TEST_VIRTUALCONTAINERMANAGER_H
#define KARTEND_TESTS_TEST_VIRTUALCONTAINERMANAGER_H

#include <QObject>

class TestVirtualContainerManager : public QObject {
  Q_OBJECT

private slots:
  // Horizontal alignment must actually move the container — in BOTH the
  // fits-in-viewport case and the overflowing one (field report
  // 2026-08-18: "left right center alignment options inoperable").
  void alignmentMovesContainerWhenContentFits();
  void alignmentMovesContainerWhenContentOverflows();

  void testConstructionWithoutWiring();
  void testCreateContainerRequiresGridContainer();
  void testCreateContainerProducesChildOfGrid();
  void testCleanupContainerDropsTheContainerPointer();
  void testCleanupIsIdempotent();
  void testEffectiveViewportWidthWithoutScrollAreaIsZero();
  void testScrollbarWidthWithoutScrollAreaIsZero();
  void testWillNeedVerticalScrollbarFalseWithoutScrollArea();
};

#endif // KARTEND_TESTS_TEST_VIRTUALCONTAINERMANAGER_H
