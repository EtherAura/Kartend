// Manages widget recycling pool for efficient virtual scrolling
#include "widgetpoolmanager.h"
#include "itemwidget.h"
#include "uiconstants.h"
#include <algorithm>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcWidgetPoolManager, "kartend.widgetpoolmanager")

namespace {

void pruneNullEntries(QList<QPointer<ItemWidget>> &list) {
  for (int i = list.size() - 1; i >= 0; --i) {
    if (list[i].isNull()) {
      list.removeAt(i);
    }
  }
}

auto takeLastValid(QList<QPointer<ItemWidget>> &list) -> ItemWidget * {
  while (!list.isEmpty()) {
    QPointer<ItemWidget> widget = list.takeLast();
    if (widget) {
      return widget.data();
    }
  }
  return nullptr;
}

} // namespace

WidgetPoolManager::WidgetPoolManager(QObject *parent) : QObject(parent) {}

WidgetPoolManager::~WidgetPoolManager() {
  clearAndDelete();
}

void WidgetPoolManager::setWidgetParent(QWidget *parent) {
  m_widgetParent = parent;
}

auto WidgetPoolManager::acquire() -> ItemWidget * {
  pruneNullEntries(m_pool);
  pruneNullEntries(m_stalePool);

  // First try the main pool (fresh widgets)
  if (ItemWidget *widget = takeLastValid(m_pool)) {
    ++m_metrics.hits;
    return widget;
  }

  // Fall back to stale pool if main pool is empty
  // This prevents unnecessary widget creation after collection switches
  // Note: Stale widgets have been reparented to a safe parent during
  // softClear() so they can be safely reparented to the new virtual container
  if (ItemWidget *widget = takeLastValid(m_stalePool)) {
    ++m_metrics.staleReused;
    ++m_metrics.hits;
    return widget;
  }

  ++m_metrics.misses;
  return new ItemWidget(m_widgetParent);
}

void WidgetPoolManager::disconnectAll(ItemWidget *widget) {
  // Only disconnect subcollectionDoubleClicked - other signals were removed
  // as EventManager intercepts all clicks before they reach ItemWidget
  QObject::disconnect(widget, &ItemWidget::subcollectionDoubleClicked, nullptr, nullptr);
}

void WidgetPoolManager::release(ItemWidget *widget) {
  if (!widget) {
    return;
  }

  pruneNullEntries(m_pool);
  pruneNullEntries(m_stalePool);

  // Disconnect signals before returning to pool
  disconnectAll(widget);

  // Only keep widget if pool isn't at capacity
  int optimalSize = calculateOptimalSize();
  if (m_pool.size() < optimalSize) {
    widget->hide();
    m_pool.append(widget);
    ++m_metrics.releases;
  } else {
    widget->deleteLater();
    ++m_metrics.discards;
  }
}

void WidgetPoolManager::clear() {
  logMetrics(); // Log final metrics before clearing
  // Don't delete widgets - just clear the pool list.
  // Caller is responsible for widget lifetime.
  m_pool.clear();
  m_stalePool.clear();
}

void WidgetPoolManager::softClear(QWidget *safeParent) {
  pruneNullEntries(m_pool);
  pruneNullEntries(m_stalePool);

  // Move main pool widgets to stale pool instead of deleting them
  // This allows reuse if the new collection needs widgets quickly
  // CRITICAL: Reparent widgets to safeParent before the old virtual container
  // is deleted, otherwise setParent() will crash accessing deleted memory
  if (safeParent) {
    for (const QPointer<ItemWidget> &widget : m_pool) {
      if (widget) {
        widget->setParent(safeParent);
        widget->hide();
      }
    }
  }

  m_stalePool.reserve(m_stalePool.size() + m_pool.size());
  m_stalePool.append(m_pool);
  m_pool.clear();

  // Limit stale pool size to prevent memory bloat during rapid collection
  // switching Excess widgets are deleted immediately rather than waiting for
  // pruneStaleWidgets()
  if (m_stalePool.size() > MAX_STALE_POOL_SIZE) {
    const int excessCount = m_stalePool.size() - MAX_STALE_POOL_SIZE;
    for (int i = 0; i < excessCount; ++i) {
      if (ItemWidget *excess = m_stalePool[i].data()) {
        excess->deleteLater();
        ++m_metrics.discards;
      }
    }
    m_stalePool.erase(m_stalePool.begin(), m_stalePool.begin() + excessCount);
  }
}

void WidgetPoolManager::pruneStaleWidgets() {
  pruneNullEntries(m_stalePool);

  // Delete stale widgets that weren't reused
  // Call this during idle time to reclaim memory
  for (const QPointer<ItemWidget> &widget : m_stalePool) {
    if (widget) {
      widget->deleteLater();
      ++m_metrics.discards;
    }
  }
  m_stalePool.clear();
}

void WidgetPoolManager::clearAndDelete() {
  logMetrics(); // Log final metrics before clearing
  // Schedule deletion of all pooled widgets via the event loop. Using
  // deleteLater() (rather than direct delete) is safer during shutdown
  // because pooled widgets may still be referenced by in-flight scroll /
  // animation callbacks. The event loop drains those callbacks before the
  // widgets are actually destroyed.
  pruneNullEntries(m_pool);
  pruneNullEntries(m_stalePool);

  for (const QPointer<ItemWidget> &widget : m_pool) {
    if (widget) {
      widget->deleteLater();
    }
  }
  m_pool.clear();

  // Also schedule deletion of stale widgets
  for (const QPointer<ItemWidget> &widget : m_stalePool) {
    if (widget) {
      widget->deleteLater();
    }
  }
  m_stalePool.clear();
}

void WidgetPoolManager::setVisibleMetrics(int visibleRows, int itemsPerRow) {
  m_visibleRows = visibleRows;
  m_itemsPerRow = itemsPerRow;
}

void WidgetPoolManager::prewarm() {
  if (!m_widgetParent) {
    return;
  }

  pruneNullEntries(m_pool);

  int targetSize = calculateOptimalSize();
  int toCreate = targetSize - m_pool.size();

  m_pool.reserve(targetSize);

  // Pre-allocate widgets up to optimal pool size
  for (int i = 0; i < toCreate; ++i) {
    auto *widget = new ItemWidget(m_widgetParent);
    widget->hide();
    m_pool.append(widget);
  }
}

void WidgetPoolManager::prewarmAsync() {
  if (!m_widgetParent) {
    return;
  }

  pruneNullEntries(m_pool);

  m_prewarmTargetSize = calculateOptimalSize();

  m_pool.reserve(m_prewarmTargetSize);

  // Already at target size
  if (m_pool.size() >= m_prewarmTargetSize) {
    return;
  }

  // Create timer if needed
  if (!m_prewarmTimer) {
    m_prewarmTimer = new QTimer(this);
    m_prewarmTimer->setInterval(0); // Next event loop iteration
    connect(m_prewarmTimer, &QTimer::timeout, this, [this]() {
      pruneNullEntries(m_pool);
      if (!m_widgetParent || m_pool.size() >= m_prewarmTargetSize) {
        m_prewarmTimer->stop();
        return;
      }

      // Create a small batch per tick to avoid blocking
      int remaining = m_prewarmTargetSize - m_pool.size();
      int toCreate = qMin(PREWARM_BATCH_SIZE, remaining);

      for (int i = 0; i < toCreate; ++i) {
        auto *widget = new ItemWidget(m_widgetParent);
        widget->hide();
        m_pool.append(widget);
      }

      // Stop when done
      if (m_pool.size() >= m_prewarmTargetSize) {
        m_prewarmTimer->stop();
      }
    });
  }

  if (!m_prewarmTimer->isActive()) {
    m_prewarmTimer->start();
  }
}

void WidgetPoolManager::stopPrewarm() {
  if (m_prewarmTimer && m_prewarmTimer->isActive()) {
    m_prewarmTimer->stop();
  }
}

auto WidgetPoolManager::calculateOptimalSize() const -> int {
  if (m_itemsPerRow <= 0) {
    return UIConstants::Widget::Pool::MIN_SIZE;
  }

  int bufferedRows = m_visibleRows + UIConstants::Grid::BUFFER_ROWS;
  int visibleWidgets = bufferedRows * m_itemsPerRow;
  int calculatedPoolSize = visibleWidgets * UIConstants::Widget::Pool::BUFFER_MULTIPLIER;

  return std::clamp(calculatedPoolSize, UIConstants::Widget::Pool::MIN_SIZE,
                    UIConstants::Widget::Pool::MAX_SIZE);
}

void WidgetPoolManager::logMetrics() const {
  if (!lcWidgetPoolManager().isDebugEnabled()) {
    return;
  }
  qCDebug(lcWidgetPoolManager) << "WidgetPool metrics:"
                               << "hits=" << m_metrics.hits << "misses=" << m_metrics.misses
                               << "releases=" << m_metrics.releases
                               << "discards=" << m_metrics.discards
                               << "staleReused=" << m_metrics.staleReused
                               << "hitRate=" << QString::number(m_metrics.hitRate() * 100, 'f', 1)
                               << "%"
                               << "poolSize=" << m_pool.size()
                               << "stalePoolSize=" << m_stalePool.size();
}
