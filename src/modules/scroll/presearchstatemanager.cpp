#include "presearchstatemanager.h"
#include "artworkmanager.h"
#include "itemwidget.h"
#include "widgetpoolmanager.h"
#include <QScrollArea>
#include <QScrollBar>

PreSearchStateManager::PreSearchStateManager(QObject *parent) : QObject(parent) {}

void PreSearchStateManager::setReferences(QScrollArea *scrollArea,
                                          QWidget *gridContainer) {
  m_scrollArea = scrollArea;
  m_gridContainer = gridContainer;
}

bool PreSearchStateManager::saveState(QHash<int, ItemWidget *> &activeWidgets) {
  // Only save if not already saved and we have widgets to save
  if (!m_savedWidgets.isEmpty() || activeWidgets.isEmpty()) {
    return false;
  }
  
  // Save current scroll position
  if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
    m_savedScrollPosition = m_scrollArea->verticalScrollBar()->value();
  }
  
  // Move active widgets to saved state
  // Reparent to gridContainer so they survive cleanup() destroying virtual container
  m_savedWidgets = activeWidgets;
  for (auto it = m_savedWidgets.begin(); it != m_savedWidgets.end(); ++it) {
    if (it.value() && m_gridContainer) {
      it.value()->setParent(m_gridContainer);
      it.value()->hide();
    }
  }
  activeWidgets.clear();
  return true;
}

bool PreSearchStateManager::restoreState(
    QHash<int, ItemWidget *> &activeWidgets,
    QWidget *virtualContainer,
    WidgetPoolManager *widgetPool,
    ArtworkManager *artworkManager,
    std::function<QPoint(int)> getPositionFunc,
    int itemWidth, int itemHeight) {
  
  if (m_savedWidgets.isEmpty()) {
    return false;
  }
  
  // Clear artwork references from search result widgets before releasing them
  // This prevents stale widget pointers from causing incorrect artwork
  if (artworkManager) {
    artworkManager->clearWidgetReferences();
  }
  
  // Clear the widget pool first - we're going to delete all widgets in
  // the virtual container, which may include pool widgets. This prevents
  // double-delete crashes during shutdown.
  if (widgetPool) {
    widgetPool->clear();
  }
  
  // Delete search result widgets immediately - don't return to pool
  // Pool widgets might still be visible if they haven't been hidden yet
  for (auto it = activeWidgets.begin(); it != activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      widget->hide();
      widget->setParent(nullptr);  // Orphan before delete to ensure immediate removal
      delete widget;
    }
  }
  activeWidgets.clear();
  
  // Also delete any ItemWidget children of the virtual container that might
  // not be tracked in activeWidgets (e.g., orphaned widgets from pool)
  if (virtualContainer) {
    QList<ItemWidget *> orphanedWidgets = virtualContainer->findChildren<ItemWidget *>();
    for (ItemWidget *widget : orphanedWidgets) {
      widget->hide();
      widget->setParent(nullptr);
      delete widget;
    }
  }
  
  // Restore scroll position BEFORE showing widgets to avoid visual jump
  // from beginning of list to remembered position
  if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
    m_scrollArea->verticalScrollBar()->setValue(m_savedScrollPosition);
  }
  
  // Restore saved widgets - reparent back to virtual container
  activeWidgets = m_savedWidgets;
  m_savedWidgets.clear();
  
  // Show, reparent and reposition widgets - their artwork is already loaded
  for (auto it = activeWidgets.begin(); it != activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      // Reparent back to virtual container
      if (virtualContainer) {
        widget->setParent(virtualContainer);
      }
      QPoint position = getPositionFunc(it.key());
      widget->setGeometry(position.x(), position.y(), itemWidth, itemHeight);
      widget->show();
    }
  }
  
  // Don't call updateViewportArtwork() - restored widgets already have their
  // artwork loaded and displayed. Calling it could cause incorrect artwork
  // due to stale mappings.
  return true;
}

void PreSearchStateManager::discardSavedState() {
  // Delete any saved widgets
  for (auto it = m_savedWidgets.begin(); it != m_savedWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      widget->setParent(nullptr);
      delete widget;
    }
  }
  m_savedWidgets.clear();
  m_savedScrollPosition = 0;
}
