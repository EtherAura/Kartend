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
#include "collection/validationhelpers.h"
#include "hoverscrollhandler.h"
#include "ifilecollectionlookup.h"
#include "igridlayoutscroll.h"
#include "imouseholdcontrol.h"
#include "iscrolldatasource.h"
#include "isearchstatescroll.h"
#include "itemwidget.h"
#include "mousemanager.h"
#include "wheeleventhandler.h"

#include <QLoggingCategory>
#include <QtCore/Qt>
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
  // Only claim wheel events whose target lives in the MAIN window. A wheel tick
  // over any other top-level window — a dialog such as the DAT Manager, with its
  // own lists/tables — must reach that window's widgets via Qt's normal delivery
  // and scroll them, not the item grid behind it.
  QWidget *ourWindow =
      m_itemsPage ? m_itemsPage->window() : (m_gridContainer ? m_gridContainer->window() : nullptr);
  auto *targetWidget = qobject_cast<QWidget *>(obj);
  QWidget *targetWindow = targetWidget ? targetWidget->window() : nullptr;
  if (ourWindow == nullptr || targetWindow != ourWindow) {
    return false;
  }
  // A wheel tick over the collection tree scrolls the TREE, not the grid
  // selection (user request 2026-08-17). Target-based rather than
  // focus-based — hovering is enough, matching every other scroll area.
  if (m_ctx && m_ctx->ui.collectionTreeWidget && targetWidget &&
      (targetWidget == m_ctx->ui.collectionTreeWidget ||
       m_ctx->ui.collectionTreeWidget->isAncestorOf(targetWidget))) {
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
  if (scrollData() && scrollSearch() &&
      CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    int visualIndex = -1;
    const auto &active = scrollData()->getActiveWidgets();
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
      if (it.value() == widget) {
        visualIndex = it.key();
        break;
      }
    }
    if (visualIndex >= 0) {
      // Convert visual index to actual index (accounts for filtering)
      int actualIndex = scrollSearch()->getFilteredIndex(visualIndex);
      // Use the *rendered* prefix counts: during search, the visible
      // subcollection list is filtered down (often to zero) and virtual
      // folders are suppressed. The hierarchy-cache subs list is the wrong
      // source of truth and would mis-classify media items as
      // subcollections, causing double-click to silently no-op.
      int subCount = scrollData()->getSubcollectionCount();
      int folderCount = scrollData()->getVirtualFolderCount();
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
  if (fileCollectionLookup()) {
    collIdx = fileCollectionLookup()->getCollectionIndexForFile(path);
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
  if (!widget || !scrollData()) {
    return -1;
  }

  // O(1) reverse lookup instead of scanning every active widget on each
  // hover / mouse-move (Kartend-th8z).
  return scrollData()->indexForWidget(widget);
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
                ? static_cast<Qt::KeyboardModifier>(m_generalSettings->input.artworkCycleModifier)
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

  // Cover flow owns its left-clicks (Kartend-g7hbx): CoverFlowWidget
  // hit-tests its own cards and gallery strip, and a click that misses both
  // is a no-op — there is no "click empty space to deselect" in a carousel
  // that always has a centered card. Falling through to the grid path here
  // mapped the click into the HIDDEN gridContainer, found no widget, and
  // emitted clearSelectionRequested — the carousel then rendered the cleared
  // selection (-1) clamped to item 0 while the toolbar counter and gallery
  // strip kept the old selection. Bailing before mouseHold() also keeps
  // click-hold scrolling from arming against the hidden grid.
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections) &&
      (*m_collections)[*m_currentCollectionIndex].viewType == ViewType::CoverFlow) {
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

  if (mouseHold()) {
    mouseHold()->setLeftMouseDown(true);
    mouseHold()->clearHorizontalCandidate();
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

  // scrollData / scrollGrid alias the same ScrollManager and are seeded in
  // lockstep; both are checked because findBestWidgetForClick reads both roles.
  if (!scrollData() || !scrollGrid()) {
    emit clearSelectionRequested();
    event->accept();
    return true;
  }

  auto [chosen, visualIndex] =
      MouseManager::findBestWidgetForClick(clickPos, scrollData(), scrollGrid(), m_gridContainer);
  if (chosen && visualIndex >= 0) {
    emit widgetClicked(chosen, visualIndex, clickPos, mouseEvent);
    event->accept();
    return true;
  }
  emit clearSelectionRequested();
  event->accept();
  return true;
}
