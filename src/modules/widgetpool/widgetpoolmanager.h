#ifndef WIDGETPOOLMANAGER_H
#define WIDGETPOOLMANAGER_H

#include <QList>
#include <QObject>
#include <QPointer>

class ItemWidget;
class QWidget;
class QTimer;

// Metrics for monitoring widget pool performance
struct WidgetPoolMetrics {
  int hits = 0;       // Reused from pool
  int misses = 0;     // Created new
  int releases = 0;   // Returned to pool
  int discards = 0;   // Pool full, deleted
  int staleReused = 0; // Reused stale widget

  [[nodiscard]] double hitRate() const {
    int total = hits + misses;
    return total > 0 ? static_cast<double>(hits) / total : 0.0;
  }

  void reset() { hits = misses = releases = discards = staleReused = 0; }
};

// Manages a pool of recycled ItemWidget instances for virtual scrolling
class WidgetPoolManager : public QObject {
  Q_OBJECT
public:
  explicit WidgetPoolManager(QObject *parent = nullptr);
  ~WidgetPoolManager() override;

  // Set the parent widget for newly created widgets
  void setWidgetParent(QWidget *parent);

  // Acquire a widget from the pool or create a new one
  [[nodiscard]] ItemWidget *acquire();

  // Return a widget to the pool for reuse
  void release(ItemWidget *widget);

  // Clear all widgets from the pool (does not delete them)
  void clear();
  
  // Soft clear: marks widgets as stale but keeps them available
  // Stale widgets can still be reused if pool runs low
  // safeParent: Widgets are reparented to this widget before the old container
  // is deleted - prevents crash when old parent is destroyed
  void softClear(QWidget *safeParent = nullptr);
  
  // Remove stale widgets that haven't been reused (call during idle)
  void pruneStaleWidgets();

  // Clear all widgets and delete them immediately
  void clearAndDelete();

  // Set parameters for pool size calculation
  void setVisibleMetrics(int visibleRows, int itemsPerRow);

  // Pre-allocate widgets during idle time for smoother initial scroll.
  // Call after setWidgetParent and setVisibleMetrics are configured.
  void prewarm();
  
  // Async prewarm: creates widgets incrementally to avoid blocking UI.
  // Preferred over prewarm() during runtime to maintain responsiveness.
  void prewarmAsync();
  
  // Stop any in-progress async prewarm operation
  void stopPrewarm();

  // Get pool performance metrics
  [[nodiscard]] const WidgetPoolMetrics &metrics() const { return m_metrics; }
  void resetMetrics() { m_metrics.reset(); }

  // Get current pool size
  [[nodiscard]] int poolSize() const {
    int count = 0;
    for (const auto &widget : m_pool) {
      if (widget) {
        ++count;
      }
    }
    return count;
  }

  // Log pool metrics for debugging (call periodically or on cleanup)
  void logMetrics() const;

private:
  // Disconnect all signals from a widget before pooling
  void disconnectAll(ItemWidget *widget);

  [[nodiscard]] int calculateOptimalSize() const;

  QList<QPointer<ItemWidget>> m_pool;
  QList<QPointer<ItemWidget>> m_stalePool;  // Widgets marked stale but still available
  QWidget *m_widgetParent = nullptr;
  WidgetPoolMetrics m_metrics;

  // Metrics for calculating optimal pool size
  int m_visibleRows = 0;
  int m_itemsPerRow = 0;
  
  // Async prewarm state
  QTimer *m_prewarmTimer = nullptr;
  int m_prewarmTargetSize = 0;
  static constexpr int PREWARM_BATCH_SIZE = 3;  // Widgets per timer tick
  
  // Stale pool size limit to prevent memory bloat during rapid collection switching
  static constexpr int MAX_STALE_POOL_SIZE = 50;
};

#endif
