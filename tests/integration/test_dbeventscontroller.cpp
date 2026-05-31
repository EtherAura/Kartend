#include "test_dbeventscontroller.h"

#include <QTest>

#include "dbeventscontroller.h"

void TestDbEventsController::testStartupSuppressionReleasesOnlyAfterLastStartupScan() {
  DbEventsController c;
  c.setSuppressStartupScanOverlays(true);
  QVERIFY(c.suppressStartupScanOverlays());

  // Two startup scans begin while suppression is on.
  c.onScanStarting("a", -1);
  c.onScanStarting("b", -1);
  QCOMPARE(c.m_activeScanCount, 2);
  QCOMPARE(c.m_startupActiveScanCount, 2);

  // First of the two completes. Suppression MUST stay on — releasing it here
  // is exactly the premature release the old emit-order-dependent two-slot
  // split could trip (overlay slot running before the startup slot decremented).
  c.onCollectionScanCompleted("a");
  QCOMPARE(c.m_activeScanCount, 1);
  QCOMPARE(c.m_startupActiveScanCount, 1);
  QVERIFY2(c.suppressStartupScanOverlays(),
           "startup overlay suppression released before the last startup scan finished");

  // Last startup scan completes — now suppression releases.
  c.onCollectionScanCompleted("b");
  QCOMPARE(c.m_activeScanCount, 0);
  QCOMPARE(c.m_startupActiveScanCount, 0);
  QVERIFY(!c.suppressStartupScanOverlays());
}

void TestDbEventsController::testNonStartupScansLeaveSuppressionUntouched() {
  DbEventsController c; // no suppression set: steady-state launch path
  QVERIFY(!c.suppressStartupScanOverlays());

  c.onScanStarting("x", -1);
  QCOMPARE(c.m_activeScanCount, 1);
  QCOMPARE(c.m_startupActiveScanCount, 0); // not counted as a startup scan

  c.onCollectionScanCompleted("x");
  QCOMPARE(c.m_activeScanCount, 0);
  QCOMPARE(c.m_startupActiveScanCount, 0);
  QVERIFY(!c.suppressStartupScanOverlays());
}

void TestDbEventsController::testExtraCompletionDoesNotUnderflowCounts() {
  DbEventsController c;
  // A completion with nothing in flight must clamp at zero (the > 0 guards),
  // and must not violate the count invariant the merged slot asserts.
  c.onCollectionScanCompleted("orphan");
  QCOMPARE(c.m_activeScanCount, 0);
  QCOMPARE(c.m_startupActiveScanCount, 0);
}

void TestDbEventsController::testInterleavedStartupThenLaterScan() {
  DbEventsController c;
  c.setSuppressStartupScanOverlays(true);

  c.onScanStarting("s1", -1);        // active=1 startup=1
  c.onCollectionScanCompleted("s1"); // active=0 startup=0 -> suppression releases
  QVERIFY(!c.suppressStartupScanOverlays());

  // A scan that begins after release is tracked as active only, never as a
  // startup scan, so it can't re-engage suppression.
  c.onScanStarting("s2", -1);
  QCOMPARE(c.m_activeScanCount, 1);
  QCOMPARE(c.m_startupActiveScanCount, 0);
  c.onCollectionScanCompleted("s2");
  QCOMPARE(c.m_activeScanCount, 0);
  QVERIFY(!c.suppressStartupScanOverlays());
}
