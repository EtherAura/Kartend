#include <QtTest/QtTest>

#include "artworkmanager.h"
#include "uiconstants/artwork.h"

// Direct tests for the silent-load gating math extracted from
// ArtworkManager's silent-loading partial (artworksilentloading.cpp):
// the per-tick cooldown gate and the idle-aware batch-size pickers that
// processContinuousSilentLoad / processPersistentSilentLoad call. These are
// pure statics (clock + idle-state injected) so the decisions are testable
// without driving the widget-graph-coupled async precache pipeline.
class TestSilentLoadGating : public QObject {
  Q_OBJECT
private slots:
  void inCooldown_neverWhenNoBatchCompleted();
  void inCooldown_trueInsideWindowFalseAtAndAfterBoundary();
  void continuousBatchSize_fullWhenIdleThrottledWhenActive();
  void continuousBatchSize_neverDropsBelowOne();
  void persistentBatchSize_idleVsActiveConstants();
};

void TestSilentLoadGating::inCooldown_neverWhenNoBatchCompleted() {
  using A = ArtworkManager;
  // A non-positive last-completion stamp means "no batch has finished yet" —
  // there is nothing to cool down from, so the gate never trips regardless of
  // the current clock. Guards the early-return that lets the very first tick
  // dispatch immediately.
  QVERIFY(!A::silentLoadInCooldown(0, 0));
  QVERIFY(!A::silentLoadInCooldown(0, 1'000'000));
  QVERIFY(!A::silentLoadInCooldown(-5, 1'000'000));
}

void TestSilentLoadGating::inCooldown_trueInsideWindowFalseAtAndAfterBoundary() {
  using A = ArtworkManager;
  constexpr qint64 cd = UIConstants::Artwork::SILENT_LOAD_COOLDOWN_MS;
  constexpr qint64 completed = 1'000'000;

  // Still inside the cooldown window → bail this tick.
  QVERIFY(A::silentLoadInCooldown(completed, completed));          // 0ms elapsed
  QVERIFY(A::silentLoadInCooldown(completed, completed + 1));      // 1ms elapsed
  QVERIFY(A::silentLoadInCooldown(completed, completed + cd - 1)); // boundary-1

  // At and past the boundary the comparison is a strict `<`, so the gate opens.
  QVERIFY(!A::silentLoadInCooldown(completed, completed + cd));     // exactly cd
  QVERIFY(!A::silentLoadInCooldown(completed, completed + cd + 1)); // past cd
}

void TestSilentLoadGating::continuousBatchSize_fullWhenIdleThrottledWhenActive() {
  using A = ArtworkManager;
  constexpr int divisor = UIConstants::Artwork::SILENT_LOAD_THROTTLE_DIVISOR;
  const int base = UIConstants::Artwork::SILENT_LOAD_BATCH_SIZE_DEFAULT; // 20

  // Idle: the loader gets the full configured batch.
  QCOMPARE(A::continuousSilentBatchSize(/*userIdle=*/true, base), base);
  // Active: throttle to base/divisor so foreground work keeps CPU headroom.
  QCOMPARE(A::continuousSilentBatchSize(/*userIdle=*/false, base), qMax(1, base / divisor));
  // Sanity: with the real defaults the active path is meaningfully smaller.
  QVERIFY(A::continuousSilentBatchSize(false, base) < A::continuousSilentBatchSize(true, base));
}

void TestSilentLoadGating::continuousBatchSize_neverDropsBelowOne() {
  using A = ArtworkManager;
  constexpr int divisor = UIConstants::Artwork::SILENT_LOAD_THROTTLE_DIVISOR;
  // A base smaller than the divisor would integer-divide to 0; the qMax(1, …)
  // floor must keep the active batch at >=1 so the loader never stalls on a
  // zero-sized take. (divisor is 8, so base=1 exercises 1/8 == 0.)
  QCOMPARE(A::continuousSilentBatchSize(/*userIdle=*/false, 1), 1);
  QCOMPARE(A::continuousSilentBatchSize(/*userIdle=*/false, divisor - 1), 1);
  // Idle path passes the base through verbatim, including the degenerate 0/1.
  QCOMPARE(A::continuousSilentBatchSize(/*userIdle=*/true, 1), 1);
}

void TestSilentLoadGating::persistentBatchSize_idleVsActiveConstants() {
  using A = ArtworkManager;
  QCOMPARE(A::persistentSilentBatchSize(/*userIdle=*/true),
           UIConstants::Artwork::PERSISTENT_SILENT_BATCH_IDLE);
  QCOMPARE(A::persistentSilentBatchSize(/*userIdle=*/false),
           UIConstants::Artwork::PERSISTENT_SILENT_BATCH_ACTIVE);
  // The idle drip is the larger of the two (more headroom when the user's away).
  QVERIFY(A::persistentSilentBatchSize(true) > A::persistentSilentBatchSize(false));
}

QTEST_MAIN(TestSilentLoadGating)
#include "test_silentloadgating.moc"
