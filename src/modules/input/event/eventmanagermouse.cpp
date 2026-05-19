// Sibling TU: mouse-press / double-click / hover dispatch for EventManager.
// Wheel handling lives in WheelEventHandler; hover dwell lives in
// HoverScrollHandler. This file only orchestrates and emits signals.
#include "eventmanager.h"

#include <QApplication>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QWidget>

#include "applicationcontext.h"
#include "collectionutils.h"
#include "hoverscrollhandler.h"
#include "idatabasemanager.h"
#include "iselectionmanager.h"
#include "itemwidget.h"
#include "mousemanager.h"
#include "scrollmanager.h"
#include "wheeleventhandler.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcEventManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcEventManager().isDebugEnabled()) {                                                       \
      qCDebug(lcEventManager) << msg;                                                              \
    }                                                                                              \
  } while (0)

bool EventManager::handleWheelEvent(QObject *obj, QEvent *event) {
  // WheelEventHandler treats every wheel event as a grid-selection
  // scroll regardless of target. While the scraper dialog is up we
  // skip that path so wheel ticks in the scraper (or anywhere else)
  // stop moving the main-window selection.
  if (modalScrapeDialogVisible()) {
    return false;
  }
  return m_wheelHandler && m_wheelHandler->handleEvent(obj, event);
}

bool EventManager::handleMouseDoubleClick(QObject *obj, QEvent *event) {
  auto *mouseEvent = static_cast<QMouseEvent *>(event);
  if (!mouseEvent || mouseEvent->button() != Qt::LeftButton) {
    return false;
  }

  auto *widget = itemWidgetForObject(obj);
  if (!widget) {
    return false;
  }

  // If the double-clicked widget represents a subcollection or virtual folder,
  // allow the widget to handle the event so its signal is emitted.
  if (scrollMgr() &&
      CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    int visualIndex = -1;
    const auto &active = scrollMgr()->getActiveWidgets();
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
      if (it.value() == widget) {
        visualIndex = it.key();
        break;
      }
    }
    if (visualIndex >= 0) {
      // Convert visual index to actual index (accounts for filtering)
      int actualIndex = scrollMgr()->getFilteredIndex(visualIndex);
      // Use the *rendered* prefix counts: during search, the visible
      // subcollection list is filtered down (often to zero) and virtual
      // folders are suppressed. The hierarchy-cache subs list is the wrong
      // source of truth and would mis-classify media items as
      // subcollections, causing double-click to silently no-op.
      int subCount = scrollMgr()->getSubcollectionCount();
      int folderCount = scrollMgr()->getVirtualFolderCount();
      // Pass through if it's a subcollection or virtual folder
      if (actualIndex >= 0 && actualIndex < subCount + folderCount) {
        return false;
      }
    }
  }

  QString path = widget->getFilePath();
  if (path.isEmpty()) {
    event->accept();
    return true;
  }

  int collIdx = -1;
  if (databaseMgr()) {
    collIdx = databaseMgr()->getCollectionIndexForFile(path);
  } else if (m_currentCollectionIndex) {
    collIdx = *m_currentCollectionIndex;
  }
  emit widgetDoubleClicked(path, collIdx);
  event->accept();
  return true;
}

ItemWidget *EventManager::itemWidgetForObject(QObject *obj) const {
  for (QObject *candidate = obj; candidate; candidate = candidate->parent()) {
    if (auto *widget = qobject_cast<ItemWidget *>(candidate)) {
      return widget;
    }
  }
  return nullptr;
}

int EventManager::visualIndexForWidget(ItemWidget *widget) const {
  if (!widget || !scrollMgr()) {
    return -1;
  }

  const auto &active = scrollMgr()->getActiveWidgets();
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() == widget) {
      return it.key();
    }
  }
  return -1;
}

bool EventManager::handleHoverSelection(QObject *obj, QEvent *event) {
  return m_hoverScroll && m_hoverScroll->handleEvent(obj, event, isRestoringSelection());
}

bool EventManager::handleMousePress(QObject *obj, QEvent *event) {
  if (isRestoringSelection()) {
    event->accept();
    return true;
  }
  auto *mouseEvent = static_cast<QMouseEvent *>(event);
  if (!mouseEvent) {
    return false;
  }

  // Right-click context menu handling
  if (mouseEvent->button() == Qt::RightButton) {
    auto *widget = itemWidgetForObject(obj);
    if (widget) {
      int visualIndex = visualIndexForWidget(widget);
      if (visualIndex >= 0) {
        emit contextMenuRequested(widget, visualIndex, mouseEvent->globalPosition().toPoint());
        event->accept();
        return true;
      }
    }
    return false;
  }

  // Middle-click media preview. Opens a video-first preview
  // overlay for the clicked item without changing selection or launching.
  // when the configured artwork-cycle modifier (default Shift)
  // is held, the same button instead cycles the displayed artwork through
  // the item's available types. Setting this modifier to one of Ctrl/Alt/
  // Meta lets the user pick the chord that doesn't collide with their WM.
  if (mouseEvent->button() == Qt::MiddleButton) {
    auto *widget = itemWidgetForObject(obj);
    if (widget) {
      int visualIndex = visualIndexForWidget(widget);
      if (visualIndex >= 0) {
        const Qt::KeyboardModifiers mods =
            mouseEvent->modifiers() &
            (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        const Qt::KeyboardModifier cycleMod =
            m_generalSettings
                ? static_cast<Qt::KeyboardModifier>(m_generalSettings->artworkCycleModifier)
                : Qt::ShiftModifier;
        if (cycleMod != Qt::NoModifier && mods == cycleMod) {
          emit artworkTypeCycleRequested(widget, visualIndex);
        } else {
          emit mediaPreviewRequested(widget, visualIndex);
        }
        event->accept();
        return true;
      }
    }
    return false;
  }

  if (mouseEvent->button() != Qt::LeftButton) {
    return false;
  }

  if (!m_itemScrollArea || (!m_gridContainer) || (!m_stackedWidget) || (!m_itemsPage)) {
    return false;
  }
  if (m_stackedWidget->currentWidget() != m_itemsPage) {
    return false;
  }

  // Block clicks that originate from or are inside the items top bar -
  // this prevents toolbar clicks from affecting grid navigation/selection
  if (m_itemsTopBar && m_itemsTopBar->isVisible()) {
    auto *widget = qobject_cast<QWidget *>(obj);
    if (widget) {
      // Check if the clicked widget is the toolbar or a child of it
      for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (w == m_itemsTopBar) {
          return false; // Let Qt handle toolbar interactions normally
        }
      }
      // Check if click position falls within the toolbar geometry
      QPoint globalPos = mouseEvent->globalPosition().toPoint();
      QRect toolbarRect = m_itemsTopBar->geometry();
      QPoint toolbarTopLeft = m_itemsTopBar->parentWidget()->mapToGlobal(toolbarRect.topLeft());
      QRect globalToolbarRect(toolbarTopLeft, toolbarRect.size());
      if (globalToolbarRect.contains(globalPos)) {
        return false; // Click is over the toolbar, ignore
      }
    }
  }

  if (mouseMgr()) {
    mouseMgr()->setLeftMouseDown(true);
    mouseMgr()->clearHorizontalCandidate();
  }

  bool target = (obj == m_itemScrollArea || obj == m_itemScrollArea->viewport() ||
                 obj == m_gridContainer || obj == m_itemsPage || itemWidgetForObject(obj));
  if (!target) {
    return false;
  }

  QPoint clickPos = mouseEvent->pos();
  if (obj != m_gridContainer) {
    if (auto *w = qobject_cast<QWidget *>(obj)) {
      clickPos = m_gridContainer->mapFromGlobal(w->mapToGlobal(clickPos));
    }
  }

  if (!scrollMgr()) {
    emit clearSelectionRequested();
    event->accept();
    return true;
  }

  auto [chosen, visualIndex] =
      MouseManager::findBestWidgetForClick(clickPos, scrollMgr(), m_gridContainer);
  if (chosen && visualIndex >= 0) {
    emit widgetClicked(chosen, visualIndex, clickPos, mouseEvent);
    event->accept();
    return true;
  }
  emit clearSelectionRequested();
  event->accept();
  return true;
}
