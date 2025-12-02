#ifndef WIDGETPOOLMANAGER_H
#define WIDGETPOOLMANAGER_H

#include <QList>
#include <QObject>

class ItemWidget;
class QWidget;

// Metrics for monitoring widget pool performance
struct WidgetPoolMetrics {
  int hits = 0;      // Reused from pool
  int misses = 0;    // Created new
  int releases = 0;  // Returned to pool
  int discards = 0;  // Pool full, deleted

  [[nodiscard]] double hitRate() const {
    int total = hits + misses;
    return total > 0 ? static_cast<double>(hits) / total : 0.0;
  }

  void reset() { hits = misses = releases = discards = 0; }
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

  // Clear all widgets from the pool
  void clear();

  // Set parameters for pool size calculation
  void setVisibleMetrics(int visibleRows, int itemsPerRow);

  // Get pool performance metrics
  [[nodiscard]] const WidgetPoolMetrics &metrics() const { return m_metrics; }
  void resetMetrics() { m_metrics.reset(); }

  // Get current pool size
  [[nodiscard]] int poolSize() const { return m_pool.size(); }

  // Log pool metrics for debugging (call periodically or on cleanup)
  void logMetrics() const;

private:
  // Disconnect all signals from a widget before pooling
  void disconnectAll(ItemWidget *widget);

  [[nodiscard]] int calculateOptimalSize() const;

  QList<ItemWidget *> m_pool;
  QWidget *m_widgetParent = nullptr;
  WidgetPoolMetrics m_metrics;

  // Metrics for calculating optimal pool size
  int m_visibleRows = 0;
  int m_itemsPerRow = 0;
};

#endif
