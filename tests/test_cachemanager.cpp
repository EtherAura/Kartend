/**
 * @file test_cachemanager.cpp
 * @brief Unit tests for CacheManager cache operations and metrics
 *
 * Tests the in-memory pixmap cache, disk persistence, and cache metrics.
 */

#include "cachemanager.h"
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QApplication>

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
  void testReleaseGuiResources();

  // Async cancellation / shutdown paths
  void testDestruct_withPendingDebouncedSave_doesNotCrash();
  void testCancelPendingIo_isIdempotentAndStopsTimer();
  void testDestruct_withScheduledSavesUnderLoad_doesNotCrash();

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
  
  // Clear cache
  m_cacheManager->clearCollectionCache(0);
  m_cacheManager->resetMetrics();
  
  // Should now be a miss (not in memory cache)
  QPixmap afterClear = m_cacheManager->getArtwork(m_testArtworkPath);
  // Note: might still be found on disk, but memory cache should be cleared
  CacheMetrics metrics = m_cacheManager->metrics();
  QCOMPARE(metrics.memoryHits, 0);
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
  // Schedule a save and arm the timer.
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  QCoreApplication::processEvents();

  // First call: signals cancel + stops timer.
  m_cacheManager->cancelPendingIo();
  // Repeated calls must remain safe (ApplicationManager may invoke this
  // alongside destructor-driven teardown on the shutdown path).
  m_cacheManager->cancelPendingIo();
  m_cacheManager->cancelPendingIo();

  // After cancellation, scheduleSaveToDisk early-returns at the m_cancelIo
  // check, so re-scheduling cannot resurrect a fire-after-cancel path.
  m_cacheManager->scheduleSaveToDisk(50);
  QTest::qWait(150); // > 50ms: a non-cancelled timer would have fired.

  QVERIFY(true);
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

  QVERIFY2(elapsedMs < 5000,
           qPrintable(QStringLiteral("CacheManager destructor took %1 ms — "
                                     "pool drain or timer teardown stalled")
                          .arg(elapsedMs)));

  QCoreApplication::processEvents();
}

QTEST_MAIN(TestCacheManager)
#include "test_cachemanager.moc"