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

QTEST_MAIN(TestCacheManager)
#include "test_cachemanager.moc"