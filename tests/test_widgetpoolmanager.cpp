#include <QTest>
#include <QWidget>
#include "widgetpoolmanager.h"
#include "itemwidget.h"

/**
 * @brief Unit tests for WidgetPoolManager.
 * 
 * Tests widget pool acquisition, release, soft clear, stale pruning,
 * and metrics tracking.
 */
class TestWidgetPoolManager : public QObject {
  Q_OBJECT

private:
  QWidget *m_parentWidget = nullptr;
  WidgetPoolManager *m_pool = nullptr;

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Basic pool operations
  void testAcquire_createsNewWidget();
  void testAcquire_reusesReleasedWidget();
  void testRelease_returnsWidgetToPool();
  void testClear_emptiesPool();
  void testClearAndDelete_deletesWidgets();

  // Soft clear and stale widgets
  void testSoftClear_marksWidgetsStale();
  void testAcquire_reusesStaleWidget();
  void testPruneStaleWidgets_removesStaleWidgets();
  void testPruneStaleWidgets_keepsNormalPool();

  // Metrics tracking
  void testMetrics_tracksHits();
  void testMetrics_tracksMisses();
  void testMetrics_tracksStaleReuse();
  void testMetrics_hitRateCalculation();
  void testMetrics_reset();

  // Pool sizing
  void testSetVisibleMetrics_affectsOptimalSize();
  void testPrewarm_createsWidgets();
};

void TestWidgetPoolManager::initTestCase() {
  // Create a parent widget for all test widgets
  m_parentWidget = new QWidget();
}

void TestWidgetPoolManager::cleanupTestCase() {
  delete m_parentWidget;
  m_parentWidget = nullptr;
}

void TestWidgetPoolManager::init() {
  m_pool = new WidgetPoolManager();
  m_pool->setWidgetParent(m_parentWidget);
}

void TestWidgetPoolManager::cleanup() {
  m_pool->clearAndDelete();
  delete m_pool;
  m_pool = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Basic pool operations
// ─────────────────────────────────────────────────────────────────────────────

void TestWidgetPoolManager::testAcquire_createsNewWidget() {
  QCOMPARE(m_pool->poolSize(), 0);
  
  ItemWidget *widget = m_pool->acquire();
  QVERIFY(widget != nullptr);
  QCOMPARE(widget->parent(), m_parentWidget);
  
  // Clean up
  delete widget;
}

void TestWidgetPoolManager::testAcquire_reusesReleasedWidget() {
  // Create and release a widget
  ItemWidget *widget1 = m_pool->acquire();
  m_pool->release(widget1);
  
  QCOMPARE(m_pool->poolSize(), 1);
  
  // Acquire should return the same widget
  ItemWidget *widget2 = m_pool->acquire();
  QCOMPARE(widget1, widget2);
  QCOMPARE(m_pool->poolSize(), 0);
  
  // Clean up
  delete widget2;
}

void TestWidgetPoolManager::testRelease_returnsWidgetToPool() {
  ItemWidget *widget = m_pool->acquire();
  QCOMPARE(m_pool->poolSize(), 0);
  
  m_pool->release(widget);
  QCOMPARE(m_pool->poolSize(), 1);
  
  // Acquire it back so cleanup deletes it properly
  ItemWidget *reacquired = m_pool->acquire();
  Q_UNUSED(reacquired);
  delete widget;
}

void TestWidgetPoolManager::testClear_emptiesPool() {
  // Add some widgets to pool
  ItemWidget *w1 = m_pool->acquire();
  ItemWidget *w2 = m_pool->acquire();
  m_pool->release(w1);
  m_pool->release(w2);
  
  QCOMPARE(m_pool->poolSize(), 2);
  
  m_pool->clear();
  QCOMPARE(m_pool->poolSize(), 0);
  
  // Note: clear() doesn't delete widgets, they're orphaned
  // In real usage, the widgets would be reparented or deleted elsewhere
}

void TestWidgetPoolManager::testClearAndDelete_deletesWidgets() {
  // Add some widgets to pool
  ItemWidget *w1 = m_pool->acquire();
  ItemWidget *w2 = m_pool->acquire();
  m_pool->release(w1);
  m_pool->release(w2);
  
  QCOMPARE(m_pool->poolSize(), 2);
  
  m_pool->clearAndDelete();
  QCOMPARE(m_pool->poolSize(), 0);
  
  // Widgets are deleted, no cleanup needed
}

// ─────────────────────────────────────────────────────────────────────────────
// Soft clear and stale widgets
// ─────────────────────────────────────────────────────────────────────────────

void TestWidgetPoolManager::testSoftClear_marksWidgetsStale() {
  // Add widgets to pool
  ItemWidget *w1 = m_pool->acquire();
  ItemWidget *w2 = m_pool->acquire();
  m_pool->release(w1);
  m_pool->release(w2);
  
  QCOMPARE(m_pool->poolSize(), 2);
  
  // Soft clear moves to stale pool
  m_pool->softClear();
  
  // Pool size should be 0 (stale widgets aren't counted in poolSize())
  QCOMPARE(m_pool->poolSize(), 0);
}

void TestWidgetPoolManager::testAcquire_reusesStaleWidget() {
  // Add widget to pool and mark stale
  ItemWidget *w1 = m_pool->acquire();
  m_pool->release(w1);
  m_pool->softClear();
  
  // Reset metrics to track stale reuse
  m_pool->resetMetrics();
  
  // Acquire should reuse the stale widget
  ItemWidget *w2 = m_pool->acquire();
  QCOMPARE(w1, w2);
  QCOMPARE(m_pool->metrics().staleReused, 1);
  
  delete w2;
}

void TestWidgetPoolManager::testPruneStaleWidgets_removesStaleWidgets() {
  // Add widgets and mark stale
  ItemWidget *w1 = m_pool->acquire();
  ItemWidget *w2 = m_pool->acquire();
  m_pool->release(w1);
  m_pool->release(w2);
  m_pool->softClear();
  
  // Prune should delete stale widgets
  m_pool->pruneStaleWidgets();
  
  // Pool and stale pool should both be empty
  QCOMPARE(m_pool->poolSize(), 0);
  
  // Acquiring should create new widget, not reuse
  m_pool->resetMetrics();
  ItemWidget *w3 = m_pool->acquire();
  QCOMPARE(m_pool->metrics().misses, 1);
  QCOMPARE(m_pool->metrics().staleReused, 0);
  
  delete w3;
}

void TestWidgetPoolManager::testPruneStaleWidgets_keepsNormalPool() {
  // Add widget to normal pool (not stale)
  ItemWidget *w1 = m_pool->acquire();
  m_pool->release(w1);
  
  QCOMPARE(m_pool->poolSize(), 1);
  
  // Prune should not affect normal pool
  m_pool->pruneStaleWidgets();
  
  QCOMPARE(m_pool->poolSize(), 1);
  
  // Clean up
  m_pool->clearAndDelete();
}

// ─────────────────────────────────────────────────────────────────────────────
// Metrics tracking
// ─────────────────────────────────────────────────────────────────────────────

void TestWidgetPoolManager::testMetrics_tracksHits() {
  m_pool->resetMetrics();
  
  // Create and release a widget
  ItemWidget *widget = m_pool->acquire();
  QCOMPARE(m_pool->metrics().misses, 1);  // First acquire is a miss
  
  m_pool->release(widget);
  
  // Acquire from pool is a hit
  widget = m_pool->acquire();
  QCOMPARE(m_pool->metrics().hits, 1);
  
  delete widget;
}

void TestWidgetPoolManager::testMetrics_tracksMisses() {
  m_pool->resetMetrics();
  
  // Empty pool, so all acquires are misses
  ItemWidget *w1 = m_pool->acquire();
  ItemWidget *w2 = m_pool->acquire();
  
  QCOMPARE(m_pool->metrics().misses, 2);
  QCOMPARE(m_pool->metrics().hits, 0);
  
  delete w1;
  delete w2;
}

void TestWidgetPoolManager::testMetrics_tracksStaleReuse() {
  m_pool->resetMetrics();
  
  ItemWidget *widget = m_pool->acquire();
  m_pool->release(widget);
  m_pool->softClear();
  
  // Acquire from stale pool
  widget = m_pool->acquire();
  
  QCOMPARE(m_pool->metrics().staleReused, 1);
  
  delete widget;
}

void TestWidgetPoolManager::testMetrics_hitRateCalculation() {
  m_pool->resetMetrics();
  
  // 2 misses (creates), then 2 hits (reuses)
  ItemWidget *w1 = m_pool->acquire();
  ItemWidget *w2 = m_pool->acquire();
  m_pool->release(w1);
  m_pool->release(w2);
  w1 = m_pool->acquire();
  w2 = m_pool->acquire();
  
  // 2 hits / 4 total = 0.5
  QCOMPARE(m_pool->metrics().hits, 2);
  QCOMPARE(m_pool->metrics().misses, 2);
  QCOMPARE(m_pool->metrics().hitRate(), 0.5);
  
  delete w1;
  delete w2;
}

void TestWidgetPoolManager::testMetrics_reset() {
  // Generate some metrics
  ItemWidget *widget = m_pool->acquire();
  m_pool->release(widget);
  widget = m_pool->acquire();
  
  QVERIFY(m_pool->metrics().hits > 0 || m_pool->metrics().misses > 0);
  
  m_pool->resetMetrics();
  
  QCOMPARE(m_pool->metrics().hits, 0);
  QCOMPARE(m_pool->metrics().misses, 0);
  QCOMPARE(m_pool->metrics().releases, 0);
  QCOMPARE(m_pool->metrics().discards, 0);
  QCOMPARE(m_pool->metrics().staleReused, 0);
  
  delete widget;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pool sizing
// ─────────────────────────────────────────────────────────────────────────────

void TestWidgetPoolManager::testSetVisibleMetrics_affectsOptimalSize() {
  // Set metrics for a 5x4 grid (20 visible items)
  m_pool->setVisibleMetrics(5, 4);
  
  // Prewarm should create widgets based on these metrics
  m_pool->prewarm();
  
  // Pool should have some widgets (at least buffer rows)
  QVERIFY(m_pool->poolSize() > 0);
  
  m_pool->clearAndDelete();
}

void TestWidgetPoolManager::testPrewarm_createsWidgets() {
  QCOMPARE(m_pool->poolSize(), 0);
  
  m_pool->setVisibleMetrics(3, 4);  // 12 visible items
  m_pool->prewarm();
  
  // Should have created some buffer widgets
  QVERIFY(m_pool->poolSize() > 0);
  
  m_pool->clearAndDelete();
}

QTEST_MAIN(TestWidgetPoolManager)
#include "test_widgetpoolmanager.moc"
