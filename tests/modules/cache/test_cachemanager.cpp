/**
 * @file test_cachemanager.cpp
 * @brief Unit tests for CacheManager cache operations and metrics
 *
 * Tests the in-memory pixmap cache, disk persistence, and cache metrics.
 */

#include "cachemanager.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTest>

class TestCacheManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Basic cache operations
  void testCacheArtwork_validPixmap();
  void testCacheArtwork_emptyPath();
  void testCacheArtwork_nullPixmap();
  void testCacheArtwork_tooSmallPixmap();
  void testGetArtwork_cacheHit();
  void testGetArtwork_cacheMiss();
  void testGetArtwork_emptyPath();

  // Cache metrics
  void testMetrics_initialState();
  void testMetrics_memoryHit();
  void testMetrics_cacheMiss();
  void testMetrics_insert();
  void testMetrics_hitRate();
  void testMetrics_reset();

  // Cache management
  void testClearCollectionCache();
  void testClearCollectionCacheIsScoped();
  void testReleaseGuiResources();

  // Async cancellation / shutdown paths
  void testDestruct_withPendingDebouncedSave_doesNotCrash();
  void testCancelPendingIo_isIdempotentAndStopsTimer();
  void testDestruct_withScheduledSavesUnderLoad_doesNotCrash();

  // Budget resize (Kartend-c7mb regression coverage)
  void testSetArtworkCacheBudgetMB_resizesCeiling();
  void testSetArtworkCacheBudgetMB_clampsBelowOneMB();

private:
  CacheManager *m_cacheManager;
  QTemporaryDir *m_tempDir;
  QString m_testArtworkPath;
};

void TestCacheManager::initTestCase() {
  // Create a temporary directory for test artwork files
  m_tempDir = new QTemporaryDir();
  QVERIFY(m_tempDir->isValid());

  // Create a test artwork file
  m_testArtworkPath = m_tempDir->path() + "/test_artwork.png";
  QPixmap testPixmap(300, 300);
  testPixmap.fill(Qt::blue);
  QVERIFY(testPixmap.save(m_testArtworkPath, "PNG"));
}

void TestCacheManager::cleanupTestCase() {
  delete m_tempDir;
  m_tempDir = nullptr;
}

void TestCacheManager::init() {
  m_cacheManager = new CacheManager();
  m_cacheManager->initialize();
}

void TestCacheManager::cleanup() {
  delete m_cacheManager;
  m_cacheManager = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Basic cache operations
// ─────────────────────────────────────────────────────────────────────────────

void TestCacheManager::testCacheArtwork_validPixmap() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  // Should not throw and should be retrievable
  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);

  QPixmap retrieved = m_cacheManager->getArtwork(m_testArtworkPath);
  QVERIFY2(!retrieved.isNull(), "Cached pixmap should be retrievable");
  QCOMPARE(retrieved.width(), 300);
  QCOMPARE(retrieved.height(), 300);
}

void TestCacheManager::testCacheArtwork_emptyPath() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  // Should not crash with empty path
  m_cacheManager->cacheArtwork("", pixmap);

  // Empty path should return null pixmap
  QPixmap retrieved = m_cacheManager->getArtwork("");
  QVERIFY(retrieved.isNull());
}

void TestCacheManager::testCacheArtwork_nullPixmap() {
  QPixmap nullPixmap;

  // Should not crash with null pixmap
  m_cacheManager->cacheArtwork(m_testArtworkPath, nullPixmap);

  // Null pixmap should not be cached
  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.inserts, 0);
}

void TestCacheManager::testCacheArtwork_tooSmallPixmap() {
  // Pixmaps smaller than MIN_PIXMAP_SIZE (200) should not be cached
  QPixmap smallPixmap(100, 100);
  smallPixmap.fill(Qt::green);

  QString smallPath = m_tempDir->path() + "/small.png";
  m_cacheManager->cacheArtwork(smallPath, smallPixmap);

  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.inserts, 0);
}

void TestCacheManager::testGetArtwork_cacheHit() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  m_cacheManager->resetMetrics();

  // First retrieval - should be memory hit
  QPixmap retrieved = m_cacheManager->getArtwork(m_testArtworkPath);
  QVERIFY(!retrieved.isNull());

  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.memoryHits, 1);
  QCOMPARE(metrics.misses, 0);
}

void TestCacheManager::testGetArtwork_cacheMiss() {
  QString nonExistentPath = "/nonexistent/artwork/path.png";

  QPixmap retrieved = m_cacheManager->getArtwork(nonExistentPath);
  QVERIFY(retrieved.isNull());

  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.misses, 1);
  QCOMPARE(metrics.memoryHits, 0);
}

void TestCacheManager::testGetArtwork_emptyPath() {
  QPixmap retrieved = m_cacheManager->getArtwork("");
  QVERIFY(retrieved.isNull());
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache metrics
// ─────────────────────────────────────────────────────────────────────────────

void TestCacheManager::testMetrics_initialState() {
  CacheMetrics metrics = m_cacheManager->metrics();

  QCOMPARE(metrics.memoryHits, 0);
  QCOMPARE(metrics.diskHits, 0);
  QCOMPARE(metrics.misses, 0);
  QCOMPARE(metrics.inserts, 0);
  QCOMPARE(metrics.evictions, 0);
  QCOMPARE(metrics.invalidations, 0);
}

void TestCacheManager::testMetrics_memoryHit() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  m_cacheManager->resetMetrics();

  QPixmap retrieved = m_cacheManager->getArtwork(m_testArtworkPath);
  Q_UNUSED(retrieved);

  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.memoryHits, 1);
}

void TestCacheManager::testMetrics_cacheMiss() {
  QPixmap retrieved = m_cacheManager->getArtwork("/nonexistent/path.png");
  Q_UNUSED(retrieved);

  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.misses, 1);
}

void TestCacheManager::testMetrics_insert() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);

  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.inserts, 1);
}

void TestCacheManager::testMetrics_hitRate() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  // Insert one item
  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  m_cacheManager->resetMetrics();

  // 2 hits
  QPixmap hit1 = m_cacheManager->getArtwork(m_testArtworkPath);
  QPixmap hit2 = m_cacheManager->getArtwork(m_testArtworkPath);
  Q_UNUSED(hit1);
  Q_UNUSED(hit2);

  // 1 miss
  QPixmap miss = m_cacheManager->getArtwork("/nonexistent.png");
  Q_UNUSED(miss);

  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.memoryHits, 2);
  QCOMPARE(metrics.misses, 1);

  // Hit rate should be 2/3 = 0.666...
  double expectedRate = 2.0 / 3.0;
  QVERIFY(qAbs(metrics.memoryHitRate() - expectedRate) < 0.01);
}

void TestCacheManager::testMetrics_reset() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  QPixmap retrieved = m_cacheManager->getArtwork(m_testArtworkPath);
  Q_UNUSED(retrieved);

  CacheMetrics before = m_cacheManager->metrics();
  QVERIFY(before.inserts > 0 || before.memoryHits > 0);

  m_cacheManager->resetMetrics();

  CacheMetrics after = m_cacheManager->metrics();
  QCOMPARE(after.memoryHits, 0);
  QCOMPARE(after.diskHits, 0);
  QCOMPARE(after.misses, 0);
  QCOMPARE(after.inserts, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache management
// ─────────────────────────────────────────────────────────────────────────────

void TestCacheManager::testClearCollectionCache() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);

  // Verify it's cached
  QPixmap retrieved = m_cacheManager->getArtwork(m_testArtworkPath);
  QVERIFY(!retrieved.isNull());

  // Clear cache for the artwork directory holding m_testArtworkPath. The
  // signature is now prefix-scoped (Kartend cache-scoping change), so the
  // caller passes the collection's artworkDirectory; m_tempDir->path()
  // matches the prefix the path was stored under above.
  m_cacheManager->clearCollectionCache(m_tempDir->path());
  m_cacheManager->resetMetrics();

  // Should now be a miss (not in memory cache)
  QPixmap afterClear = m_cacheManager->getArtwork(m_testArtworkPath);
  // Note: might still be found on disk, but memory cache should be cleared
  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.memoryHits, 0);
}

void TestCacheManager::testClearCollectionCacheIsScoped() {
  // Two artwork paths under DIFFERENT prefixes. clearCollectionCache for
  // one prefix must NOT evict entries belonging to the other — the whole
  // point of the prefix-scoped signature.
  const QString otherDir = m_tempDir->path() + "/sibling_collection";
  QVERIFY(QDir().mkpath(otherDir));
  const QString otherPath = otherDir + "/sibling_artwork.png";
  // Pixmaps below UIConstants::Cache::MIN_PIXMAP_SIZE (200) are silently
  // rejected by cacheArtwork. Use 300x300 to match the rest of the suite.
  QPixmap otherPixmap(300, 300);
  otherPixmap.fill(Qt::blue);
  QVERIFY(otherPixmap.save(otherPath, "PNG"));

  QPixmap firstPixmap(300, 300);
  firstPixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(m_testArtworkPath, firstPixmap);
  m_cacheManager->cacheArtwork(otherPath, otherPixmap);

  // Evict only the m_testArtworkPath collection by scoping to a non-
  // matching prefix… first prove the sibling survives an unrelated clear.
  m_cacheManager->clearCollectionCache(m_tempDir->path() + "/nonexistent");
  QVERIFY(!m_cacheManager->getArtworkFromMemoryOnly(m_testArtworkPath).isNull());
  QVERIFY(!m_cacheManager->getArtworkFromMemoryOnly(otherPath).isNull());

  // Now clear under the sibling's prefix and verify the m_testArtworkPath
  // entry survives intact.
  m_cacheManager->clearCollectionCache(otherDir);
  QVERIFY(!m_cacheManager->getArtworkFromMemoryOnly(m_testArtworkPath).isNull());
  QVERIFY(m_cacheManager->getArtworkFromMemoryOnly(otherPath).isNull());
}

void TestCacheManager::testReleaseGuiResources() {
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);

  // Release resources
  m_cacheManager->releaseGuiResources();
  m_cacheManager->resetMetrics();

  // Should be empty now
  QPixmap retrieved = m_cacheManager->getArtwork(m_testArtworkPath);
  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.memoryHits, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Async cancellation / shutdown paths
// ─────────────────────────────────────────────────────────────────────────────

void TestCacheManager::testDestruct_withPendingDebouncedSave_doesNotCrash() {
  // Regression guard for Kartend-acal: the debounced-save timer's timeout
  // lambda captures 'this' and calls saveToDisk(). If the destructor doesn't
  // sever the timer connection FIRST, a fired timer can run the lambda
  // against a partially destructed CacheManager.
  //
  // We schedule a save (cacheArtwork → scheduleSaveToDisk → queued
  // invokeMethod that calls timer->start()), spin the event loop once so the
  // timer is armed, then destruct *before* the timer fires. The destructor
  // must stop the timer and delete its parent context before any other
  // teardown — verified by the absence of UAF/sanitizer reports.
  auto *manager = new CacheManager();
  manager->initialize();

  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  manager->cacheArtwork(m_testArtworkPath, pixmap);

  // Drain the queued invokeMethod that arms the timer.
  QCoreApplication::processEvents();

  // Destruct before the debounce delay (FLUSH_DEBOUNCE_MS = 1000ms) elapses.
  delete manager;

  // Drain any leftover events; deleting m_timerContext should have already
  // discarded any pending timeout deliveries, but spin once for good measure.
  QCoreApplication::processEvents();

  QVERIFY(true);
}

void TestCacheManager::testCancelPendingIo_isIdempotentAndStopsTimer() {
  // Schedule a save and arm the timer (drain the queued timer-start post so
  // the pre-cancel schedule is fully applied before cancelling).
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  QCoreApplication::processEvents();
  QVERIFY(m_cacheManager->isSaveTimerActive());

  // First call: signals cancel + stops timer.
  m_cacheManager->cancelPendingIo();
  // Repeated calls must remain safe (ApplicationManager may invoke this
  // alongside destructor-driven teardown on the shutdown path).
  m_cacheManager->cancelPendingIo();
  m_cacheManager->cancelPendingIo();

  // Kartend-yjklc: assert the guard state synchronously instead of racing the
  // clock (the old form waited 500ms for a timer NOT to fire — a negative
  // that waiting can only make less flaky, and the slowest single test).
  // cancelPendingIo() latches the cancellation flag and stops the timer...
  QVERIFY(m_cacheManager->isPendingIoCancelled());
  QVERIFY(!m_cacheManager->isSaveTimerActive());

  // ...and re-scheduling cannot resurrect it: scheduleSaveToDisk()
  // early-returns at the cancellation guard before posting a timer start, and
  // the queued timer-start lambda re-checks cancellation for posts that were
  // already in flight. Drive the event loop once to flush any such post.
  m_cacheManager->scheduleSaveToDisk(50);
  QCoreApplication::processEvents();
  QVERIFY(m_cacheManager->isPendingIoCancelled());
  QVERIFY(!m_cacheManager->isSaveTimerActive());
}

void TestCacheManager::testDestruct_withScheduledSavesUnderLoad_doesNotCrash() {
  // Exercises the destructor's I/O pool drain path (waitForDone with a
  // bounded budget, falling through to the abandon-pool intentional-leak
  // fallback if the budget expires). Caches several artworks so multiple
  // save schedules race with destruction; the destructor must return within
  // a bounded time without crashing regardless of which branch is taken.
  auto *manager = new CacheManager();
  manager->initialize();

  // Insert several distinct items so dirty sets are non-empty and a real
  // disk save would occur.
  for (int i = 0; i < 8; ++i) {
    QPixmap pixmap(300, 300);
    pixmap.fill(Qt::GlobalColor(Qt::red + (i % 6)));
    const QString path = m_tempDir->path() + QStringLiteral("/load_%1.png").arg(i);
    QPixmap onDisk(300, 300);
    onDisk.fill(Qt::blue);
    QVERIFY(onDisk.save(path, "PNG"));
    manager->cacheArtwork(path, pixmap);
  }
  // Force an immediate save schedule alongside the debounced ones.
  manager->scheduleSaveToDisk(0);
  QCoreApplication::processEvents();

  // The destructor's drain budget is 2000ms; allow generous slack for slow
  // CI without making this test silently always-pass.
  QElapsedTimer timer;
  timer.start();
  delete manager;
  const qint64 elapsedMs = timer.elapsed();

  QVERIFY2(elapsedMs < 5000, qPrintable(QStringLiteral("CacheManager destructor took %1 ms — "
                                                       "pool drain or timer teardown stalled")
                                            .arg(elapsedMs)));

  QCoreApplication::processEvents();
}

// ─── Budget resize (Kartend-c7mb) ───────────────────────────────────────────

void TestCacheManager::testSetArtworkCacheBudgetMB_resizesCeiling() {
  // setArtworkCacheBudgetMB takes a megabytes value and pushes it into the
  // underlying QCache::setMaxCost in bytes. The internal accounting unit is
  // bytes, so a 64 MB budget should produce a maxCost of 64*1024*1024.
  m_cacheManager->setArtworkCacheBudgetMB(64);
  QCOMPARE(m_cacheManager->artworkCacheMaxCostForTesting(), 64 * 1024 * 1024);

  // Re-resizing replaces the prior ceiling, not adds to it.
  m_cacheManager->setArtworkCacheBudgetMB(128);
  QCOMPARE(m_cacheManager->artworkCacheMaxCostForTesting(), 128 * 1024 * 1024);
}

void TestCacheManager::testSetArtworkCacheBudgetMB_clampsBelowOneMB() {
  // 0 and negative values must not collapse the cache to an
  // immediate-eviction state (QCache treats maxCost <= 0 specially); the
  // implementation enforces a 1 MB floor. Both 0 and -5 should land at the
  // same floor.
  m_cacheManager->setArtworkCacheBudgetMB(0);
  QCOMPARE(m_cacheManager->artworkCacheMaxCostForTesting(), 1 * 1024 * 1024);

  m_cacheManager->setArtworkCacheBudgetMB(-5);
  QCOMPARE(m_cacheManager->artworkCacheMaxCostForTesting(), 1 * 1024 * 1024);
}

QTEST_MAIN(TestCacheManager)
#include "test_cachemanager.moc"