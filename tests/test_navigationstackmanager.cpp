/**
 * @file test_navigationstackmanager.cpp
 * @brief Unit tests for NavigationStackManager
 *
 * Covers the navigation stack used by NavigationManager to traverse
 * the collection hierarchy (parent → child → ...). The class is
 * header-only with no external dependencies, making it a clean unit
 * test target.
 *
 * Note: NavigationManager itself depends on DatabaseManager, ScrollManager,
 * DetailsPaneManager, and ApplicationContext, so it requires integration-style
 * tests. Those are tracked separately. This file covers the stack primitive
 * that the NavigationManager built upon during the al0 refactor.
 */

#include "navigationstackmanager.h"
#include <QTest>

class TestNavigationStackManager : public QObject {
  Q_OBJECT

private slots:
  // Construction
  void testInitialState();

  // Push / pop / top
  void testPushIncrementsDepth();
  void testPopReturnsLastPushedAndDecrements();
  void testPopOnEmptyReturnsMinusOne();
  void testTopPeeksWithoutModifying();
  void testTopOnEmptyReturnsMinusOne();
  void testPushPopRoundTrip();
  void testPopMultipleTimes();

  // Depth invariants
  void testDepthNeverGoesNegative();
  void testDepthMatchesSizeAfterMixedOps();

  // Clear
  void testClearResetsStackAndDepth();
  void testClearOnEmptyIsNoop();

  // State queries
  void testIsEmptyTrueInitiallyAndAfterClear();
  void testSizeMatchesPushCount();

  // In-progress flag
  void testInProgressDefaultFalse();
  void testInProgressSetterRoundTrip();

  // Direct access
  void testStackReturnsConstReference();
  void testStackRefAllowsMutation();

  // Stress
  void testManyPushesPreservesOrder();
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testInitialState() {
  NavigationStackManager mgr;
  QVERIFY(mgr.isEmpty());
  QCOMPARE(mgr.depth(), 0);
  QCOMPARE(mgr.size(), 0);
  QCOMPARE(mgr.top(), -1);
  QVERIFY(!mgr.isInProgress());
}

// ─────────────────────────────────────────────────────────────────────────────
// Push / pop / top
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testPushIncrementsDepth() {
  NavigationStackManager mgr;
  mgr.push(5);
  QCOMPARE(mgr.depth(), 1);
  QCOMPARE(mgr.size(), 1);
  QCOMPARE(mgr.top(), 5);
  QVERIFY(!mgr.isEmpty());
}

void TestNavigationStackManager::testPopReturnsLastPushedAndDecrements() {
  NavigationStackManager mgr;
  mgr.push(7);
  mgr.push(11);
  QCOMPARE(mgr.pop(), 11);
  QCOMPARE(mgr.depth(), 1);
  QCOMPARE(mgr.size(), 1);
  QCOMPARE(mgr.top(), 7);
}

void TestNavigationStackManager::testPopOnEmptyReturnsMinusOne() {
  NavigationStackManager mgr;
  QCOMPARE(mgr.pop(), -1);
  QCOMPARE(mgr.depth(), 0);
  QVERIFY(mgr.isEmpty());
}

void TestNavigationStackManager::testTopPeeksWithoutModifying() {
  NavigationStackManager mgr;
  mgr.push(42);
  QCOMPARE(mgr.top(), 42);
  QCOMPARE(mgr.top(), 42); // idempotent
  QCOMPARE(mgr.size(), 1);
  QCOMPARE(mgr.depth(), 1);
}

void TestNavigationStackManager::testTopOnEmptyReturnsMinusOne() {
  NavigationStackManager mgr;
  QCOMPARE(mgr.top(), -1);
}

void TestNavigationStackManager::testPushPopRoundTrip() {
  NavigationStackManager mgr;
  mgr.push(1);
  mgr.push(2);
  mgr.push(3);
  QCOMPARE(mgr.pop(), 3);
  QCOMPARE(mgr.pop(), 2);
  QCOMPARE(mgr.pop(), 1);
  QVERIFY(mgr.isEmpty());
  QCOMPARE(mgr.depth(), 0);
}

void TestNavigationStackManager::testPopMultipleTimes() {
  NavigationStackManager mgr;
  mgr.push(1);
  QCOMPARE(mgr.pop(), 1);
  // Subsequent pops on empty should return -1, not crash
  QCOMPARE(mgr.pop(), -1);
  QCOMPARE(mgr.pop(), -1);
  QCOMPARE(mgr.depth(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth invariants
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testDepthNeverGoesNegative() {
  NavigationStackManager mgr;
  // Pop on empty repeatedly — depth must stay clamped at 0
  for (int i = 0; i < 10; ++i) {
    (void)mgr.pop();
  }
  QCOMPARE(mgr.depth(), 0);
}

void TestNavigationStackManager::testDepthMatchesSizeAfterMixedOps() {
  NavigationStackManager mgr;
  mgr.push(10);
  mgr.push(20);
  mgr.push(30);
  (void)mgr.pop();
  mgr.push(40);
  QCOMPARE(mgr.depth(), mgr.size());
  QCOMPARE(mgr.depth(), 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testClearResetsStackAndDepth() {
  NavigationStackManager mgr;
  mgr.push(1);
  mgr.push(2);
  mgr.push(3);
  mgr.clear();
  QVERIFY(mgr.isEmpty());
  QCOMPARE(mgr.depth(), 0);
  QCOMPARE(mgr.size(), 0);
  QCOMPARE(mgr.top(), -1);
}

void TestNavigationStackManager::testClearOnEmptyIsNoop() {
  NavigationStackManager mgr;
  mgr.clear();
  QVERIFY(mgr.isEmpty());
  QCOMPARE(mgr.depth(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// State queries
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testIsEmptyTrueInitiallyAndAfterClear() {
  NavigationStackManager mgr;
  QVERIFY(mgr.isEmpty());
  mgr.push(99);
  QVERIFY(!mgr.isEmpty());
  mgr.clear();
  QVERIFY(mgr.isEmpty());
}

void TestNavigationStackManager::testSizeMatchesPushCount() {
  NavigationStackManager mgr;
  for (int i = 0; i < 5; ++i) {
    mgr.push(i);
  }
  QCOMPARE(mgr.size(), 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// In-progress flag
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testInProgressDefaultFalse() {
  NavigationStackManager mgr;
  QVERIFY(!mgr.isInProgress());
}

void TestNavigationStackManager::testInProgressSetterRoundTrip() {
  NavigationStackManager mgr;
  mgr.setInProgress(true);
  QVERIFY(mgr.isInProgress());
  mgr.setInProgress(false);
  QVERIFY(!mgr.isInProgress());
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct access
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testStackReturnsConstReference() {
  NavigationStackManager mgr;
  mgr.push(11);
  mgr.push(22);
  const QList<int> &snapshot = mgr.stack();
  QCOMPARE(snapshot.size(), 2);
  QCOMPARE(snapshot.at(0), 11);
  QCOMPARE(snapshot.at(1), 22);
}

void TestNavigationStackManager::testStackRefAllowsMutation() {
  NavigationStackManager mgr;
  mgr.push(1);
  // stackRef() returns mutable reference (used sparingly per docs).
  // Verify it actually exposes mutation.
  QList<int> &ref = mgr.stackRef();
  ref.append(2);
  QCOMPARE(mgr.size(), 2);
  QCOMPARE(mgr.top(), 2);
  // Note: depth tracking is *not* synced when bypassing push/pop.
  // This test documents that limitation; callers must use push/pop
  // for depth correctness.
}

// ─────────────────────────────────────────────────────────────────────────────
// Stress
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationStackManager::testManyPushesPreservesOrder() {
  NavigationStackManager mgr;
  constexpr int N = 1000;
  for (int i = 0; i < N; ++i) {
    mgr.push(i);
  }
  QCOMPARE(mgr.size(), N);
  QCOMPARE(mgr.depth(), N);
  // Pop them all, verify LIFO order
  for (int i = N - 1; i >= 0; --i) {
    QCOMPARE(mgr.pop(), i);
  }
  QVERIFY(mgr.isEmpty());
}

QTEST_MAIN(TestNavigationStackManager)
#include "test_navigationstackmanager.moc"
