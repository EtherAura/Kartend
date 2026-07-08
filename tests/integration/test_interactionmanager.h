/**
 * @file test_interactionmanager.h
 * @brief Integration tests for InteractionManager's coordinator surface
 *        (Kartend-fgyrq).
 *
 * InteractionManager pulls in the full MainWindow widget graph + cross-manager
 * wiring, so it can't be unit-tested in isolation. These run inside the shared
 * integration binary against a real MainWindowFixture. They cover the
 * construction/wiring contract (every sub-manager is created) and the
 * no-selection invariants the rest of the input stack relies on, which were
 * previously exercised only indirectly through ScrollManager/SelectionManager
 * tests.
 */
#ifndef KARTEND_TESTS_TEST_INTERACTIONMANAGER_H
#define KARTEND_TESTS_TEST_INTERACTIONMANAGER_H

#include <QObject>

class TestInteractionManager : public QObject {
  Q_OBJECT

private slots:
  void testSubManagersAreWired();
  void testClearSelectionYieldsNoSelection();
  void testClearSelectionAndFocusYieldsNoSelection();
  void testIsWheelScrollingInitiallyFalse();
};

#endif // KARTEND_TESTS_TEST_INTERACTIONMANAGER_H
