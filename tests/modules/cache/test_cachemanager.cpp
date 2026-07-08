/**
 * @file test_cachemanager.cpp
 * @brief Unit tests for CacheManager cache operations and metrics
 *
 * Tests the in-memory pixmap cache, disk persistence, and cache metrics.
 */

#include "cachediskstorage.h"
#include "cachemanager.h"
#include "uiconstants/cache.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QStandardPaths>
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
  void testSetArtworkDiskCacheBudgetMB_clampsAndKeepsUnlimitedSentinel();

  // Timestamp-store persistence + bookkeeping sweep (Kartend-0ldg2)
  void testShutdownSnapshotPersistsAllTimestamps();
  void testPruneSweepsDeadBookkeeping();

  // initialize() merge rule: in-memory entries are always fresher than the
  // store (Kartend-52b4j.2)
  void testInitializeMergeKeepsFresherInMemoryTimestamps();

  // Disk-budget eviction (piggybacked on the getCacheSize walk)
  void testEvictArtworkOverDiskBudget();

  // Invalidation propagation to the timestamp store (Kartend-9lm54)
  void testInvalidationDeletesStoreRowOnDebouncedSave();
  void testShutdownFlushDeletesInvalidatedRow();
  void testSnapshotCarriesPendingRemovalThroughShutdownWrite();

  // Retained decode images (flush without GUI-thread QPixmap::toImage)
  void testCacheArtworkWithSourceImage_flushWritesRetainedImage();
  void testDirtyImageStore_clearsWithDirtySet();

  // Missing-timestamp-row disk hits must not serve stale PNGs (and must not
  // mask the staleness by stamping the current source mtime)
  void testTryLoadDiskCache_missingRow_stalePngDeletedNotServed();
  void testTryLoadDiskCache_missingRow_freshPngServedAndRowRepersisted();
  void testTryLoadDiskCache_hitTouchesCachePngForEviction();
  void testGetArtwork_diskHitInvalidatesOnRecordedMismatch();
  void testGetArtwork_missingRow_stalePngDeletedNotServed();

  // Insert paths stat the source outside the cache mutex; semantics pin
  void testCacheArtwork_missingSourceAddsNoTimestampRow();

  // Failed removal batches must survive into the shutdown flush
  void testFailedRemovalWriteIsRetriedByShutdownFlush();

private:
  CacheManager *m_cacheManager;
  QTemporaryDir *m_tempDir;
  QString m_testArtworkPath;
  // Per-process HOME so the timestamp store these slots create (and the
  // legacy-JSON migration that renames files on first open) lands in a
  // private test-mode QStandardPaths tree, never the developer's real
  // ~/.cache/kartend (same pattern as test_cachediskstorage, Kartend-j1ahs).
  QTemporaryDir m_homeDir;
};

void TestCacheManager::initTestCase() {
  QVERIFY(m_homeDir.isValid());
  qputenv("HOME", m_homeDir.path().toUtf8());
  QStandardPaths::setTestModeEnabled(true);

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

void TestCacheManager::testSetArtworkDiskCacheBudgetMB_clampsAndKeepsUnlimitedSentinel() {
  // Construction default is the compile-time budget — the walk enforces it
  // even before the user setting is wired.
  QCOMPARE(m_cacheManager->artworkDiskCacheBudgetMBForTesting(),
           UIConstants::Cache::ARTWORK_DISK_CACHE_BUDGET_MB);

  // In-range values pass through.
  m_cacheManager->setArtworkDiskCacheBudgetMB(512);
  QCOMPARE(m_cacheManager->artworkDiskCacheBudgetMBForTesting(), 512);

  // 0 is the "unlimited" sentinel and must NOT be snapped up to the minimum —
  // evictArtworkOverDiskBudget treats a non-positive budget as eviction-off.
  m_cacheManager->setArtworkDiskCacheBudgetMB(0);
  QCOMPARE(m_cacheManager->artworkDiskCacheBudgetMBForTesting(), 0);

  // Everything else clamps into [MIN, MAX] — a sub-minimum typo or negative
  // hand-edit can't configure a permanent evict-everything churn.
  m_cacheManager->setArtworkDiskCacheBudgetMB(100);
  QCOMPARE(m_cacheManager->artworkDiskCacheBudgetMBForTesting(),
           UIConstants::Cache::MIN_ARTWORK_DISK_CACHE_MB);
  m_cacheManager->setArtworkDiskCacheBudgetMB(-5);
  QCOMPARE(m_cacheManager->artworkDiskCacheBudgetMBForTesting(),
           UIConstants::Cache::MIN_ARTWORK_DISK_CACHE_MB);
  m_cacheManager->setArtworkDiskCacheBudgetMB(1000000);
  QCOMPARE(m_cacheManager->artworkDiskCacheBudgetMBForTesting(),
           UIConstants::Cache::MAX_ARTWORK_DISK_CACHE_MB);
}

// ─── Timestamp-store persistence + bookkeeping sweep (Kartend-0ldg2) ─────────

void TestCacheManager::testShutdownSnapshotPersistsAllTimestamps() {
  // The shutdown path: snapshot the full timestamp map while the manager is
  // alive, then write it via the static helper (ApplicationManager calls it
  // after teardown has begun). The snapshot must land in the store and be
  // readable by the next session's initialize() path.
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);

  const CacheTimestampsSnapshot snapshot = m_cacheManager->snapshotTimestampsForShutdown();
  QVERIFY(snapshot.timestamps.contains(m_testArtworkPath));
  QVERIFY(snapshot.removedPaths.isEmpty());
  CacheManager::saveTimestampsSnapshotToDiskForShutdown(snapshot);

  CacheDiskStorage storage;
  QHash<QString, qint64> persisted;
  storage.readTimestampsInto(persisted);
  QVERIFY(persisted.contains(m_testArtworkPath));
  QCOMPARE(persisted.value(m_testArtworkPath), snapshot.timestamps.value(m_testArtworkPath));
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheManager::testPruneSweepsDeadBookkeeping() {
  // The opportunistic sweep must drop fileTimestamps entries that are
  // neither in artworkCache nor backed by a file on disk, and every
  // m_lastRevalidatedMs entry whose pixmap was evicted — while keeping
  // entries whose source file still exists (they remain useful for disk-
  // cache validation).
  const QString disposablePath = m_tempDir->path() + "/disposable.png";
  QPixmap onDisk(300, 300);
  onDisk.fill(Qt::green);
  QVERIFY(onDisk.save(disposablePath, "PNG"));

  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  m_cacheManager->cacheArtwork(disposablePath, pixmap);
  // Populate the revalidation map (a memory hit's first revalidation pass
  // inserts the stamp).
  (void)m_cacheManager->getArtwork(m_testArtworkPath);
  (void)m_cacheManager->getArtwork(disposablePath);
  QVERIFY(m_cacheManager->lastRevalidatedCountForTesting() > 0);

  const int countWithBoth = m_cacheManager->fileTimestampCountForTesting();
  QVERIFY(countWithBoth >= 2);

  // Kill the disposable source file and evict everything from the pixmap
  // cache so both entries become sweep candidates; only the one whose file
  // is gone may be dropped.
  QVERIFY(QFile::remove(disposablePath));
  m_cacheManager->releaseGuiResources();

  m_cacheManager->pruneStaleEntriesForTesting();

  QCOMPARE(m_cacheManager->fileTimestampCountForTesting(), countWithBoth - 1);
  QCOMPARE(m_cacheManager->lastRevalidatedCountForTesting(), 0);

  // The surviving entry must still validate the on-disk artwork: a second
  // sweep with nothing newly dead is a no-op.
  m_cacheManager->pruneStaleEntriesForTesting();
  QCOMPARE(m_cacheManager->fileTimestampCountForTesting(), countWithBoth - 1);
}

void TestCacheManager::testInitializeMergeKeepsFresherInMemoryTimestamps() {
  // Kartend-52b4j.2: initialize() runs on a background thread while early
  // GUI-thread cacheArtwork calls may already have recorded fresh timestamps.
  // The old clear+reload replaced those with stale store rows, so the next
  // revalidation spuriously invalidated a freshly-decoded pixmap. The merge
  // must keep in-memory entries (always fresher than the store) while still
  // seeding keys only the store knows about.
  const QString cachedPath = m_tempDir->path() + "/init_merge_cached.png";
  const QString storeOnlyPath = m_tempDir->path() + "/init_merge_store_only.png";
  QPixmap onDisk(300, 300);
  onDisk.fill(Qt::cyan);
  QVERIFY(onDisk.save(cachedPath, "PNG"));
  QVERIFY(onDisk.save(storeOnlyPath, "PNG"));

  // Seed the store with a deliberately STALE row for the soon-to-be-cached
  // key, plus a row for a key the manager has never seen in memory.
  const qint64 staleTs = QFileInfo(cachedPath).lastModified().toMSecsSinceEpoch() - 12345;
  const qint64 storeOnlyTs = QFileInfo(storeOnlyPath).lastModified().toMSecsSinceEpoch();
  QVERIFY(CacheDiskStorage::writeTimestamps(
      QHash<QString, qint64>{{cachedPath, staleTs}, {storeOnlyPath, storeOnlyTs}}));

  // Fresh manager; cache BEFORE initialize() — models the early insert that
  // races the background store load in production.
  CacheManager manager;
  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  manager.cacheArtwork(cachedPath, pixmap);
  QCOMPARE(manager.fileTimestampCountForTesting(), 1);

  manager.initialize();

  // Store-only keys were seeded in (>= because earlier slots in this process
  // may have left unrelated rows in the shared test-mode store)...
  QVERIFY2(manager.fileTimestampCountForTesting() >= 2,
           "initialize() must still seed keys only the store knows about");
  // ...and the in-memory row survived the merge: the first memory hit after
  // an insert always revalidates, so if the stale store row had clobbered
  // the fresher in-memory timestamp this hit would spuriously invalidate
  // and return a null pixmap.
  QVERIFY2(!manager.getArtworkFromMemoryOnly(cachedPath).isNull(),
           "a stale store row must not clobber the fresher in-memory timestamp");
  QCOMPARE(manager.metrics().invalidations, qint64(0));
}

// ─── Invalidation propagation to the timestamp store (Kartend-9lm54) ─────────

void TestCacheManager::testEvictArtworkOverDiskBudget() {
  // Three fabricated cache files with staggered (fabricated) mtimes and a
  // budget that forces evicting the two oldest: the pass must delete exactly
  // those, report the freed bytes, and leave the newest file alone.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto makeFile = [&dir](const QString &name) {
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(QByteArray(1000, 'x'));
    return path;
  };
  const QString oldest = makeFile(QStringLiteral("oldest.png"));
  const QString middle = makeFile(QStringLiteral("middle.png"));
  const QString newest = makeFile(QStringLiteral("newest.png"));
  QVERIFY(!oldest.isEmpty() && !middle.isEmpty() && !newest.isEmpty());

  // Deliberately unsorted input — the eviction must order by mtime itself.
  QList<CacheManager::DiskCacheFile> files{
      {newest, 1000, 3000},
      {oldest, 1000, 1000},
      {middle, 1000, 2000},
  };
  // Budget 1500 → hysteresis target 1350: 3000 total needs two evictions
  // (3000→2000→1000 ≤ 1350).
  const qint64 freed = m_cacheManager->evictArtworkOverDiskBudget(files, 3000, 1500);
  QCOMPARE(freed, qint64(2000));
  QVERIFY(!QFile::exists(oldest));
  QVERIFY(!QFile::exists(middle));
  QVERIFY(QFile::exists(newest));

  // Already within budget → strict no-op.
  QList<CacheManager::DiskCacheFile> remaining{{newest, 1000, 3000}};
  QCOMPARE(m_cacheManager->evictArtworkOverDiskBudget(remaining, 1000, 1500), qint64(0));
  QVERIFY(QFile::exists(newest));
}

void TestCacheManager::testInvalidationDeletesStoreRowOnDebouncedSave() {
  // tryLoadArtworkImageFromDiskCache invalidation flavor, end-to-end through
  // the debounced save path: a store row whose ts_ms no longer matches the
  // source file's mtime is invalidated on access, the invalidation schedules
  // a flush, and the flush DELETEs the row from the store — the old
  // behavior left the row behind until the bounded prune pass reached it.
  const QString path = m_tempDir->path() + "/invalidated_row.png";
  QPixmap onDisk(300, 300);
  onDisk.fill(Qt::green);
  QVERIFY(onDisk.save(path, "PNG"));

  // Seed the store with a deliberately stale ts_ms for the key.
  const qint64 staleTs = QFileInfo(path).lastModified().toMSecsSinceEpoch() - 12345;
  CacheDiskStorage::writeTimestamps(QHash<QString, qint64>{{path, staleTs}});

  // A fresh manager seeds fileTimestamps from the store on initialize().
  CacheManager manager;
  manager.initialize();

  // The mismatch invalidates: no image back, one invalidation recorded, and
  // the debounced save timer is armed (the queued timer-start post needs the
  // event loop, hence QTRY).
  QVERIFY(manager.tryLoadArtworkImageFromDiskCache(path).isNull());
  QCOMPARE(manager.metrics().invalidations, qint64(1));
  QTRY_VERIFY(manager.isSaveTimerActive());

  // Flush deterministically instead of waiting out the debounce.
  manager.saveToDisk();
  QVERIFY(manager.drainPendingIoForTesting(5000));

  CacheDiskStorage storage;
  QHash<QString, qint64> persisted;
  storage.readTimestampsInto(persisted);
  QVERIFY2(!persisted.contains(path),
           "the invalidated key's row must be deleted from the timestamp store");
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheManager::testShutdownFlushDeletesInvalidatedRow() {
  // getArtwork (memory-hit revalidation) invalidation flavor, propagated
  // through the synchronous shutdown flush: the full-map upsert must not
  // resurrect the key, and the pending removal must delete its store row.
  const QString path = m_tempDir->path() + "/shutdown_invalidated.png";
  QPixmap onDisk(300, 300);
  onDisk.fill(Qt::yellow);
  QVERIFY(onDisk.save(path, "PNG"));

  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(path, pixmap);

  // Materialize a store row for the key (the debounced async save has not
  // run — nothing here processes the event loop).
  CacheDiskStorage::writeTimestamps(QHash<QString, qint64>{{path, 1234}});

  // Bump the source file's mtime so the next memory hit revalidates (the
  // first hit after an insert always does) and detects the change.
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(10),
                             QFileDevice::FileModificationTime));
  }
  (void)m_cacheManager->getArtwork(path);
  QCOMPARE(m_cacheManager->metrics().invalidations, qint64(1));

  m_cacheManager->saveToDiskForShutdown();

  CacheDiskStorage storage;
  QHash<QString, qint64> persisted;
  storage.readTimestampsInto(persisted);
  QVERIFY2(!persisted.contains(path),
           "the shutdown flush must delete the invalidated key's store row");
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheManager::testSnapshotCarriesPendingRemovalThroughShutdownWrite() {
  // The ApplicationManager-style shutdown path (Kartend-2zkm8): snapshot via
  // snapshotTimestampsForShutdown() while the manager is alive, then write
  // via the static helper after teardown has begun. A key invalidated inside
  // the final debounce window (its deleting flush never fired) must travel
  // in the snapshot's removedPaths and have its store row DELETEd by the
  // shutdown write — the prior map-only snapshot let it survive a session.
  const QString stalePath = m_tempDir->path() + "/snapshot_invalidated.png";
  QPixmap onDisk(300, 300);
  onDisk.fill(Qt::yellow);
  QVERIFY(onDisk.save(stalePath, "PNG"));

  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(stalePath, pixmap);
  // A healthy second key proves the upsert half of the write still lands.
  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);

  // Materialize a store row for the key (the debounced async save has not
  // run — nothing here processes the event loop).
  CacheDiskStorage::writeTimestamps(QHash<QString, qint64>{{stalePath, 1234}});

  // Bump the source file's mtime so the next memory hit revalidates (the
  // first hit after an insert always does) and detects the change.
  {
    QFile file(stalePath);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(10),
                             QFileDevice::FileModificationTime));
  }
  (void)m_cacheManager->getArtwork(stalePath);
  QCOMPARE(m_cacheManager->metrics().invalidations, qint64(1));

  const CacheTimestampsSnapshot snapshot = m_cacheManager->snapshotTimestampsForShutdown();
  QVERIFY2(snapshot.removedPaths.contains(stalePath),
           "the snapshot must carry the invalidation still pending its deleting flush");
  QVERIFY(!snapshot.timestamps.contains(stalePath));
  QVERIFY(snapshot.timestamps.contains(m_testArtworkPath));

  CacheManager::saveTimestampsSnapshotToDiskForShutdown(snapshot);

  CacheDiskStorage storage;
  QHash<QString, qint64> persisted;
  storage.readTimestampsInto(persisted);
  QVERIFY2(!persisted.contains(stalePath),
           "the shutdown snapshot write must delete the invalidated key's store row");
  QVERIFY(persisted.contains(m_testArtworkPath));
  QVERIFY(storage.drainWithBudget(1000));
}

// ─── Retained decode images (flush without GUI-thread QPixmap::toImage) ──────

void TestCacheManager::testCacheArtworkWithSourceImage_flushWritesRetainedImage() {
  // The delivery paths hand cacheArtwork the worker-decoded QImage alongside
  // the pixmap; the flush must snapshot that retained image (implicitly
  // shared — the old path deep-copied every dirty pixmap back to a QImage on
  // the GUI thread) and produce the same cache PNG as before.
  const QString path = m_tempDir->path() + "/image_passthrough.png";
  QImage image(300, 300, QImage::Format_ARGB32);
  image.fill(Qt::darkCyan);
  QVERIFY(image.save(path, "PNG"));
  // Clean slate in case an earlier slot's flush already cached this key.
  const QString cachePath = CacheDiskStorage::artworkCachePath(path);
  QFile::remove(cachePath);

  m_cacheManager->cacheArtwork(path, QPixmap::fromImage(image), image);
  QCOMPARE(m_cacheManager->dirtyImageCountForTesting(), 1);

  // Flush deterministically instead of waiting out the debounce. The
  // snapshot must empty the retained-image store (the flush batch owns
  // shared refs from here on), so nothing pins decodes past their dirty
  // window.
  m_cacheManager->saveToDisk();
  QCOMPARE(m_cacheManager->dirtyImageCountForTesting(), 0);
  QVERIFY(m_cacheManager->drainPendingIoForTesting(5000));

  QVERIFY2(QFile::exists(cachePath), "flush must write the cache PNG for the dirty entry");
  const QImage written(cachePath);
  QCOMPARE(written.convertToFormat(QImage::Format_ARGB32),
           image.convertToFormat(QImage::Format_ARGB32));
}

void TestCacheManager::testDirtyImageStore_clearsWithDirtySet() {
  // The retained-image store must track dirtyArtwork's lifecycle exactly:
  // pixmap-only inserts never populate it, clearCollectionCache drops the
  // cleared prefix's entries, and releaseGuiResources empties it wholesale.
  QImage image(300, 300, QImage::Format_ARGB32);
  image.fill(Qt::magenta);
  const QPixmap pixmap = QPixmap::fromImage(image);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap);
  QCOMPARE(m_cacheManager->dirtyImageCountForTesting(), 0);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap, image);
  QCOMPARE(m_cacheManager->dirtyImageCountForTesting(), 1);
  m_cacheManager->clearCollectionCache(m_tempDir->path());
  QCOMPARE(m_cacheManager->dirtyImageCountForTesting(), 0);

  m_cacheManager->cacheArtwork(m_testArtworkPath, pixmap, image);
  QCOMPARE(m_cacheManager->dirtyImageCountForTesting(), 1);
  m_cacheManager->releaseGuiResources();
  QCOMPARE(m_cacheManager->dirtyImageCountForTesting(), 0);
}

// ─── Missing-timestamp-row disk hits (stale-PNG masking fix) ─────────────────

void TestCacheManager::testTryLoadDiskCache_missingRow_stalePngDeletedNotServed() {
  // A cached PNG whose timestamp row is missing (lost dirty batch, or a
  // store wipe on schema mismatch) and whose own mtime is OLDER than the
  // source is provably stale. The old path served it anyway and stamped the
  // CURRENT source mtime, erasing the mismatch forever. It must now be
  // deleted and reported as a miss so the re-encode path repopulates both
  // the PNG and the store row.
  const QString path = m_tempDir->path() + "/missing_row_stale.png";
  QImage source(300, 300, QImage::Format_ARGB32);
  source.fill(Qt::red);
  QVERIFY(source.save(path, "PNG"));

  const QString cachePath = CacheDiskStorage::artworkCachePath(path);
  QImage staleCached(300, 300, QImage::Format_ARGB32);
  staleCached.fill(Qt::blue);
  QVERIFY(staleCached.save(cachePath, "PNG"));
  // Backdate the cached PNG well past the source's mtime.
  {
    QFile file(cachePath);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                             QFileDevice::FileModificationTime));
  }

  // No row was ever recorded for this key — the manager was initialized
  // before the key existed and never cached it.
  QVERIFY2(m_cacheManager->tryLoadArtworkImageFromDiskCache(path).isNull(),
           "an unvalidatable stale PNG must not be served");
  QVERIFY2(!QFile::exists(cachePath), "the stale PNG must be deleted so the re-encode "
                                      "path repopulates it");
  QCOMPARE(m_cacheManager->metrics().invalidations, qint64(1));

  // End-to-end healing: a re-cache + flush restores a servable disk entry.
  QPixmap fresh(300, 300);
  fresh.fill(Qt::red);
  m_cacheManager->cacheArtwork(path, fresh);
  m_cacheManager->saveToDisk();
  QVERIFY(m_cacheManager->drainPendingIoForTesting(5000));
  QVERIFY2(!m_cacheManager->tryLoadArtworkImageFromDiskCache(path).isNull(),
           "the re-encode path must repopulate the disk entry");
}

void TestCacheManager::testTryLoadDiskCache_missingRow_freshPngServedAndRowRepersisted() {
  // The benign flavor: row missing but the cached PNG is NEWER than the
  // source, so the mtime heuristic validates it. It must be served, the
  // recovered row must land in the in-memory map, and — unlike the old
  // silent stamp — it must be marked dirty so the store row is repopulated
  // and the next session validates by exact row match again.
  const QString path = m_tempDir->path() + "/missing_row_fresh.png";
  QImage source(300, 300, QImage::Format_ARGB32);
  source.fill(Qt::red);
  QVERIFY(source.save(path, "PNG"));
  // Backdate the SOURCE so the cache PNG (written now) is newer.
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                             QFileDevice::FileModificationTime));
  }
  const QString cachePath = CacheDiskStorage::artworkCachePath(path);
  QImage cached(300, 300, QImage::Format_ARGB32);
  cached.fill(Qt::blue);
  QVERIFY(cached.save(cachePath, "PNG"));

  const int rowsBefore = m_cacheManager->fileTimestampCountForTesting();
  const QImage served = m_cacheManager->tryLoadArtworkImageFromDiskCache(path);
  QVERIFY2(!served.isNull(), "a heuristically-validated PNG must be served");
  QCOMPARE(served.convertToFormat(QImage::Format_ARGB32).pixelColor(10, 10), QColor(Qt::blue));
  QCOMPARE(m_cacheManager->metrics().invalidations, qint64(0));
  QCOMPARE(m_cacheManager->fileTimestampCountForTesting(), rowsBefore + 1);

  // The recovered row must reach the store on the next flush.
  m_cacheManager->saveToDisk();
  QVERIFY(m_cacheManager->drainPendingIoForTesting(5000));
  CacheDiskStorage storage;
  QHash<QString, qint64> persisted;
  storage.readTimestampsInto(persisted);
  QVERIFY2(persisted.contains(path), "the recovered timestamp row must be re-persisted");
  QCOMPARE(persisted.value(path), QFileInfo(path).lastModified().toMSecsSinceEpoch());
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheManager::testTryLoadDiskCache_hitTouchesCachePngForEviction() {
  // The worker read path serves most viewport loads, so a served hit must
  // refresh the cache PNG's mtime the same way getArtwork does — otherwise
  // the mtime-ordered disk-budget eviction ranks worker-served entries as
  // cold and drops hot artwork first.
  const QString path = m_tempDir->path() + "/touch_on_worker_read.png";
  QImage source(300, 300, QImage::Format_ARGB32);
  source.fill(Qt::red);
  QVERIFY(source.save(path, "PNG"));
  // Backdate the source below the cache PNG so the missing-row heuristic
  // validates the entry, then backdate the cache PNG too — still newer than
  // the source, but old enough that only an explicit touch can explain a
  // fresh mtime afterwards.
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                             QFileDevice::FileModificationTime));
  }
  const QString cachePath = CacheDiskStorage::artworkCachePath(path);
  QImage cached(300, 300, QImage::Format_ARGB32);
  cached.fill(Qt::blue);
  QVERIFY(cached.save(cachePath, "PNG"));
  {
    QFile file(cachePath);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-1800),
                             QFileDevice::FileModificationTime));
  }

  const QDateTime beforeRead = QDateTime::currentDateTime().addSecs(-60);
  QVERIFY(!m_cacheManager->tryLoadArtworkImageFromDiskCache(path).isNull());
  QVERIFY2(QFileInfo(cachePath).lastModified() >= beforeRead,
           "a worker-path disk hit must touch the cache PNG so eviction sees it as hot");
}

void TestCacheManager::testGetArtwork_diskHitInvalidatesOnRecordedMismatch() {
  // getArtwork's disk-hit path (memory miss, cached PNG present) previously
  // served without consulting the recorded timestamp at all and then
  // overwrote the row with the current source mtime — masking a stale PNG
  // exactly like the missing-row case. With a recorded row that mismatches
  // the source, the disk hit must invalidate instead of serving.
  const QString path = m_tempDir->path() + "/disk_mismatch.png";
  QImage source(300, 300, QImage::Format_ARGB32);
  source.fill(Qt::red);
  QVERIFY(source.save(path, "PNG"));

  const QString cachePath = CacheDiskStorage::artworkCachePath(path);
  QImage cached(300, 300, QImage::Format_ARGB32);
  cached.fill(Qt::blue);
  QVERIFY(cached.save(cachePath, "PNG"));

  // Seed a deliberately mismatching store row, then initialize a fresh
  // manager so it reads the row into fileTimestamps.
  const qint64 staleTs = QFileInfo(path).lastModified().toMSecsSinceEpoch() - 12345;
  CacheDiskStorage::writeTimestamps(QHash<QString, qint64>{{path, staleTs}});
  CacheManager manager;
  manager.initialize();

  QVERIFY2(manager.getArtwork(path).isNull(),
           "a disk hit with a mismatching recorded timestamp must not be served");
  QVERIFY2(!QFile::exists(cachePath), "the stale PNG must be deleted");
  QCOMPARE(manager.metrics().invalidations, qint64(1));
  QCOMPARE(manager.metrics().diskHits, qint64(0));
}

void TestCacheManager::testGetArtwork_missingRow_stalePngDeletedNotServed() {
  // Same missing-row staleness as the tryLoad flavor, through getArtwork's
  // disk-hit path (the QPixmap-returning cold reader).
  const QString path = m_tempDir->path() + "/getartwork_missing_row.png";
  QImage source(300, 300, QImage::Format_ARGB32);
  source.fill(Qt::red);
  QVERIFY(source.save(path, "PNG"));

  const QString cachePath = CacheDiskStorage::artworkCachePath(path);
  QImage staleCached(300, 300, QImage::Format_ARGB32);
  staleCached.fill(Qt::blue);
  QVERIFY(staleCached.save(cachePath, "PNG"));
  {
    QFile file(cachePath);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                             QFileDevice::FileModificationTime));
  }

  QVERIFY(m_cacheManager->getArtwork(path).isNull());
  QVERIFY(!QFile::exists(cachePath));
  QCOMPARE(m_cacheManager->metrics().invalidations, qint64(1));
  QCOMPARE(m_cacheManager->metrics().misses, qint64(1));
}

// ─── Insert-path stat semantics (stat taken outside the cache mutex) ─────────

void TestCacheManager::testCacheArtwork_missingSourceAddsNoTimestampRow() {
  // The source stat now happens before m_mutex is taken; the recorded
  // semantics must be unchanged — a nonexistent source file still gets its
  // pixmap cached but must not grow the timestamp map (nothing to
  // revalidate against).
  const QString ghostPath = m_tempDir->path() + "/ghost_source.png"; // never created
  const int rowsBefore = m_cacheManager->fileTimestampCountForTesting();

  QPixmap pixmap(300, 300);
  pixmap.fill(Qt::red);
  m_cacheManager->cacheArtwork(ghostPath, pixmap);
  QVERIFY(!m_cacheManager->getArtworkFromMemoryOnly(ghostPath).isNull());
  QCOMPARE(m_cacheManager->fileTimestampCountForTesting(), rowsBefore);

  const QString ghostPath2 = m_tempDir->path() + "/ghost_source2.png";
  m_cacheManager->cacheArtworkInMemoryOnly(ghostPath2, pixmap);
  QVERIFY(!m_cacheManager->getArtworkFromMemoryOnly(ghostPath2).isNull());
  QCOMPARE(m_cacheManager->fileTimestampCountForTesting(), rowsBefore);
}

// ─── Failed removal batches reach the store via the shutdown flush ──────────

void TestCacheManager::testFailedRemovalWriteIsRetriedByShutdownFlush() {
  // The debounced flush snapshots-and-clears dirtyRemovals before the async
  // batch outcome is known. A batch whose store write fails used to be gone
  // for good — the invalidated key kept its stale store row into the next
  // session. The failed batch must now be re-staged and folded into the
  // shutdown flush.
  const QString path = m_tempDir->path() + "/failed_removal.png";
  QPixmap onDisk(300, 300);
  onDisk.fill(Qt::green);
  QVERIFY(onDisk.save(path, "PNG"));

  // Seed a mismatching store row and initialize a fresh manager from it.
  const qint64 staleTs = QFileInfo(path).lastModified().toMSecsSinceEpoch() - 12345;
  QVERIFY(CacheDiskStorage::writeTimestamps(QHash<QString, qint64>{{path, staleTs}}));
  CacheManager manager;
  manager.initialize();

  // Invalidate: the mismatch queues the key into dirtyRemovals.
  QVERIFY(manager.tryLoadArtworkImageFromDiskCache(path).isNull());
  QCOMPARE(manager.metrics().invalidations, qint64(1));

  // Block the store: replace the SQLite file with a DIRECTORY of the same
  // name so the async write's open fails deterministically (works even when
  // the test runs as root, unlike permission tricks).
  const QString dbPath = CacheDiskStorage::databasePath();
  QVERIFY(QFile::remove(dbPath));
  QFile::remove(dbPath + QStringLiteral("-wal"));
  QFile::remove(dbPath + QStringLiteral("-shm"));
  QVERIFY(QDir().mkpath(dbPath));

  manager.saveToDisk(); // snapshot cleared dirtyRemovals; async write fails
  QVERIFY(manager.drainPendingIoForTesting(5000));

  // Unblock the store and re-materialize the stale row the failed batch was
  // supposed to delete.
  QVERIFY(QDir().rmdir(dbPath));
  QVERIFY(CacheDiskStorage::writeTimestamps(QHash<QString, qint64>{{path, staleTs}}));

  // The shutdown flush must fold the re-staged batch back in.
  manager.saveToDiskForShutdown();

  CacheDiskStorage storage;
  QHash<QString, qint64> persisted;
  storage.readTimestampsInto(persisted);
  QVERIFY2(!persisted.contains(path),
           "a removal whose async write failed must still reach the store by shutdown");
  QVERIFY(storage.drainWithBudget(1000));
}

QTEST_MAIN(TestCacheManager)
#include "test_cachemanager.moc"