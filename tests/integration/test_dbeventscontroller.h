/**
 * @file test_dbeventscontroller.h
 * @brief Unit tests for DbEventsController's scan-counter state machine.
 *
 * Kartend-sg1lf folded the two scan-completion slots into one ordered slot
 * (onCollectionScanCompleted) carrying an invariant, removing the prior
 * emit-order dependency between the startup-suppression counter and the
 * overlay counter. These tests drive the scan slots directly (the test is a
 * friend of DbEventsController) and assert that startup-overlay suppression
 * releases exactly when the last startup scan completes — never prematurely —
 * and that the counts never underflow. No DatabaseManager / MainWindow is
 * needed: every context callback the slots touch is null-guarded, so a
 * context-less controller is sufficient, and the assertions read the private
 * counters through the friend declaration.
 */
#ifndef KARTEND_TESTS_TEST_DBEVENTSCONTROLLER_H
#define KARTEND_TESTS_TEST_DBEVENTSCONTROLLER_H

#include <QObject>

class TestDbEventsController : public QObject {
  Q_OBJECT

private slots:
  void testStartupSuppressionReleasesOnlyAfterLastStartupScan();
  void testNonStartupScansLeaveSuppressionUntouched();
  void testExtraCompletionDoesNotUnderflowCounts();
  void testInterleavedStartupThenLaterScan();
};

#endif // KARTEND_TESTS_TEST_DBEVENTSCONTROLLER_H
