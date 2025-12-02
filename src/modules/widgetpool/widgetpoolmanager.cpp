// Manages widget recycling pool for efficient virtual scrolling
#include "widgetpoolmanager.h"
#include "itemwidget.h"
#include "uiconstants.h"
#include <algorithm>
#include <QDebug>

WidgetPoolManager::WidgetPoolManager(QObject *parent) : QObject(parent) {}

WidgetPoolManager::~WidgetPoolManager() { clear(); }

void WidgetPoolManager::setWidgetParent(QWidget *parent) {
  m_widgetParent = parent;
}

auto WidgetPoolManager::acquire() -> ItemWidget * {
  if (!m_pool.isEmpty()) {
    ItemWidget *widget = m_pool.takeLast();
    ++m_metrics.hits;
    return widget;
  }
  ++m_metrics.misses;
  return new ItemWidget(m_widgetParent);
}

void WidgetPoolManager::disconnectAll(ItemWidget *widget) {
  // Only disconnect subcollectionDoubleClicked - other signals were removed
  // as EventManager intercepts all clicks before they reach ItemWidget
  QObject::disconnect(widget, &ItemWidget::subcollectionDoubleClicked, nullptr,
                      nullptr);
}

void WidgetPoolManager::release(ItemWidget *widget) {
  if (!widget) {
    return;
  }

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
  logMetrics();  // Log final metrics before clearing
  for (ItemWidget *widget : m_pool) {
    if (widget) {
      widget->deleteLater();
    }
  }
  m_pool.clear();
}

void WidgetPoolManager::setVisibleMetrics(int visibleRows, int itemsPerRow) {
  m_visibleRows = visibleRows;
  m_itemsPerRow = itemsPerRow;
}

auto WidgetPoolManager::calculateOptimalSize() const -> int {
  if (m_itemsPerRow <= 0) {
    return UIConstants::Widget::Pool::MIN_SIZE;
  }

  int bufferedRows = m_visibleRows + UIConstants::Grid::BUFFER_ROWS;
  int visibleWidgets = bufferedRows * m_itemsPerRow;
  int poolSize = visibleWidgets * UIConstants::Widget::Pool::BUFFER_MULTIPLIER;

  return std::clamp(poolSize, UIConstants::Widget::Pool::MIN_SIZE,
                    UIConstants::Widget::Pool::MAX_SIZE);
}

void WidgetPoolManager::logMetrics() const {
#ifdef KARTEND_DEBUG_LOGGING
  qDebug() << "WidgetPool metrics:"
           << "hits=" << m_metrics.hits
           << "misses=" << m_metrics.misses
           << "releases=" << m_metrics.releases
           << "discards=" << m_metrics.discards
           << "hitRate=" << QString::number(m_metrics.hitRate() * 100, 'f', 1) << "%"
           << "poolSize=" << m_pool.size();
#endif
}
