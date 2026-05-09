// Sibling TU: mouse + wheel event handlers for EventManager.
#include "eventmanager.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include "animationmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspane.h"
#include "detailspanemanager.h"
#include "eventhelpers.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "mousemanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcEventManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcEventManager().isDebugEnabled()) {                                                       \
      qCDebug(lcEventManager) << msg;                                                              \
    }                                                                                              \
  } while (0)

bool EventManager::wheelEventBelongsToSidebar() const {
  // Wheel events over the sidebar should scroll the sidebar via Qt's normal
  // wheel routing rather than driving grid selection. The cursor position is
  // the source of truth — when the sidebar's QScrollArea has nothing to scroll,
  // Qt propagates the wheel up past it and `obj` would point at an ancestor
  // like the items page, so an obj-based check would miss it.
  if (!m_detailsPaneManager) {
    return false;
  }
  QWidget *sidebar = m_detailsPaneManager->sidebarWidget();
  return sidebar && sidebar->isVisible() &&
         sidebar->rect().contains(sidebar->mapFromGlobal(QCursor::pos()));
}

bool EventManager::wheelEventCanProceed() const {
  return m_itemScrollArea && m_stackedWidget &&
         CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections) &&
         m_stackedWidget->currentWidget() == m_itemsPage;
}

int EventManager::computeWheelTargetScroll(int selectedIndex, const CollectionConfig &collection,
                                           QScrollBar *axisScrollBar, bool horizontalView) const {
  // Calculate target scroll position so the new selection stays visible.
  // ScrollManager metrics override the per-collection defaults — both code
  // paths must agree on itemsPerRow / itemHeight / verticalSpacing.
  int gridWidth = CollectionUtils::effectiveGridWidth(
      collection, m_scrollManager ? m_scrollManager->sidebarShrinkingActive() : false);
  int itemHeight = collection.itemHeight;
  int vSpacing = collection.verticalSpacing;
  int headerOffset = 0;
  if (m_scrollManager) {
    const auto &metrics = m_scrollManager->getMetrics();
    gridWidth = metrics.itemsPerRow;
    itemHeight = metrics.itemHeight;
    vSpacing = metrics.verticalSpacing;
    headerOffset = metrics.headerOffset;
  }

  const int margins = UIConstants::Grid::MARGINS;
  const QRect viewport = m_itemScrollArea->viewport()->rect();
  int targetPos = 0;

  if (horizontalView && m_scrollManager) {
    // itemsPerRow means items-per-column in horizontal view; derive the
    // column index from selectedIndex / itemsPerCol and center it.
    const auto &metrics = m_scrollManager->getMetrics();
    targetPos = GridLayoutCalculator::calculateCenterScrollTarget(
        selectedIndex, viewport.width(), axisScrollBar->maximum(), metrics);
  } else {
    const int logicalTargetY = EventHelpers::computeLogicalCenteredScrollY(
        selectedIndex, gridWidth, itemHeight, vSpacing, margins, headerOffset, viewport.height());
    targetPos = logicalTargetY;
    if (m_viewportManager && m_viewportManager->getScrollScale() > 1.0) {
      targetPos = m_viewportManager->toWidgetScrollY(logicalTargetY);
    }
  }
  return qBound(0, targetPos, axisScrollBar->maximum());
}

void EventManager::onWheelAnimationFinished() {
  if (m_mouseManager) {
    m_mouseManager->setWheelScrolling(false);
  }
  if (m_viewportManager) {
    m_viewportManager->setContinuousScrollActive(false);
  }
  if (m_state) {
    m_state->scroll().userScrollActive = false;
    m_state->scroll().programmaticScroll = false;
    m_state->clearArrowCenterSuppression();
    if (m_scrollManager) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  const int selectedIndex = m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
  if (m_scrollManager && selectedIndex >= 0) {
    m_scrollManager->updateSelectionForIndex(selectedIndex);
  }
  // Selection might have scrolled outside the viewport during chained wheel
  // events; bring it back in before signaling the scroll-ended consumers.
  if (m_viewportManager && selectedIndex >= 0) {
    m_viewportManager->ensureItemVisible(selectedIndex, false);
  }
  emit wheelScrollEnded();
}

bool EventManager::handleWheelEvent(QObject * /*obj*/, QEvent *event) {
  // Reentrancy guard: animations + signal processing can re-enter wheel
  // handling. RAII-style reset on every early-return path.
  if (m_processingWheelEvent) {
    return true; // Accept to prevent default handling.
  }
  m_processingWheelEvent = true;
  auto resetGuard = qScopeGuard([this]() { m_processingWheelEvent = false; });

  if (QApplication::activeModalWidget()) {
    return false;
  }
  if (wheelEventBelongsToSidebar()) {
    return false;
  }
  if (!wheelEventCanProceed()) {
    return false;
  }

  auto *wheelEvent = static_cast<QWheelEvent *>(event);
  const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];
  // Horizontal view flips the scroll axis. The "wheel notch advances by one
  // column" UX is preserved — vertical wheel motion drives column-major
  // selection movement.
  const bool horizontalView = (collection.viewType == ViewType::Horizontal);
  QScrollBar *axisScrollBar = horizontalView ? m_itemScrollArea->horizontalScrollBar()
                                             : m_itemScrollArea->verticalScrollBar();
  if (!axisScrollBar) {
    return false;
  }

  // Stop arrow-key animations only. Wheel animations are chained smoothly by
  // startWheelScrollAnimation.
  if (!horizontalView) {
    AnimationManager::stopArrowKeyAnimationIfRunning(axisScrollBar);
  }

  const int wheelSteps = MouseManager::computeWheelSteps(wheelEvent);
  if (wheelSteps == 0) {
    return false;
  }

  const int currentPos = axisScrollBar->value();

  if (m_state) {
    m_state->scroll().userScrollActive = true;
    m_state->scroll().programmaticScroll = true;
    m_state->suppressArrowCenterFor(UIConstants::Mouse::WHEEL_SUPPRESS_ARROW_CENTER_MS);
    // refreshSelectionOverlayState is intentionally skipped here — too frequent
    // during rapid wheel events. Animation completion refreshes it.
  }

  if (applyWheelSelectionDelta(wheelSteps)) {
    // Wrap detected: bail out of animation and let updateVirtualView re-paint
    // the new visible page.
    if (m_mouseManager) {
      m_mouseManager->setWheelScrolling(false);
    }
    if (m_viewportManager) {
      m_viewportManager->setContinuousScrollActive(false);
    }
    if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
      m_animationManager->verticalAnimation()->stop();
    }
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
    event->accept();
    return true;
  }

  const int selectedIndex = m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
  if (selectedIndex < 0) {
    event->accept();
    return true;
  }

  const int targetPos =
      computeWheelTargetScroll(selectedIndex, collection, axisScrollBar, horizontalView);

  if (m_mouseManager) {
    m_mouseManager->setWheelScrolling(true);
  }
  if (m_viewportManager) {
    m_viewportManager->setContinuousScrollActive(true);
  }

  if (m_animationManager) {
    auto onFinished = [this]() { onWheelAnimationFinished(); };
    if (horizontalView) {
      m_animationManager->startWheelScrollAnimationHorizontal(axisScrollBar, currentPos, targetPos,
                                                              onFinished);
    } else {
      m_animationManager->startWheelScrollAnimation(axisScrollBar, currentPos, targetPos,
                                                    onFinished);
    }
  }

  // Defer virtual view update one event-loop tick so the scroll position
  // settles before recalculating visible items.
  if (m_scrollManager) {
    QTimer::singleShot(0, this, [this]() {
      if (m_scrollManager) {
        m_scrollManager->updateVirtualView();
      }
    });
  }

  emit wheelScrollStarted();
  event->accept();
  return true;
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
  if (m_scrollManager && m_currentCollectionIndex && *m_currentCollectionIndex >= 0) {
    int visualIndex = -1;
    const auto &active = m_scrollManager->getActiveWidgets();
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
      if (it.value() == widget) {
        visualIndex = it.key();
        break;
      }
    }
    if (visualIndex >= 0) {
      // Convert visual index to actual index (accounts for filtering)
      int actualIndex = m_scrollManager->getFilteredIndex(visualIndex);
      // Use the *rendered* prefix counts: during search, the visible
      // subcollection list is filtered down (often to zero) and virtual
      // folders are suppressed. The hierarchy-cache subs list is the wrong
      // source of truth and would mis-classify media items as
      // subcollections, causing double-click to silently no-op.
      int subCount = m_scrollManager->getSubcollectionCount();
      int folderCount = m_scrollManager->getVirtualFolderCount();
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
  if (m_databaseManager) {
    collIdx = m_databaseManager->getCollectionIndexForFile(path);
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
  if (!widget || !m_scrollManager) {
    return -1;
  }

  const auto &active = m_scrollManager->getActiveWidgets();
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() == widget) {
      return it.key();
    }
  }
  return -1;
}

bool EventManager::handleHoverSelection(QObject *obj, QEvent *event) {
  if (!m_generalSettings || !m_generalSettings->selectItemOnHover || isRestoringSelection()) {
    clearPendingHoverScroll();
    return false;
  }
  if (QApplication::activeModalWidget() || !m_stackedWidget || !m_itemsPage ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    clearPendingHoverScroll();
    return false;
  }
  if (!m_scrollManager || !m_selectionManager || !m_itemScrollArea || !m_gridContainer) {
    clearPendingHoverScroll();
    return false;
  }
  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    clearPendingHoverScroll();
    return false;
  }

  QPoint currentGlobalPos = QCursor::pos();
  const bool isMouseMove = event && event->type() == QEvent::MouseMove;
  if (isMouseMove) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent) {
      currentGlobalPos = mouseEvent->globalPosition().toPoint();
    }
    if (mouseEvent && mouseEvent->buttons() != Qt::NoButton) {
      return false;
    }
  }

  ItemWidget *widget = itemWidgetForObject(obj);
  if (!widget || !widget->isVisible()) {
    // Don't cancel an in-flight hover scroll for events from non-ItemWidget
    // objects (overlay, viewport, virtual container, etc.). The commit
    // handler already validates cursor-over-widget before scrolling.
    if (!m_pendingHoverScrollWidget) {
      clearPendingHoverScroll();
    }
    return false;
  }

  const int visualIndex = visualIndexForWidget(widget);
  if (visualIndex < 0) {
    if (!m_pendingHoverScrollWidget) {
      clearPendingHoverScroll();
    }
    return false;
  }

  const bool samePendingHover =
      m_pendingHoverScrollWidget == widget && m_pendingHoverScrollIndex == visualIndex;
  const bool alreadySelected = visualIndex == m_selectionManager->currentSelectedIndex();
  if (alreadySelected && !samePendingHover) {
    clearPendingHoverScroll();
    return false;
  }

  if (!alreadySelected) {
    if (m_state) {
      const qint64 hoverScrollSuppressedUntil =
          QDateTime::currentMSecsSinceEpoch() + UIConstants::Mouse::HOVER_SCROLL_DELAY_MS;
      m_state->arrow().suppressArrowCenterUntilMs =
          qMax(m_state->arrow().suppressArrowCenterUntilMs, hoverScrollSuppressedUntil);
    }

    m_selectionManager->selectItemByHover(visualIndex);
    if (m_detailsPaneManager && m_detailsPaneManager->isSidebarVisible()) {
      m_detailsPaneManager->updateSidebarMetadata(widget);
    }
  }

  if (samePendingHover) {
    const QPoint delta = currentGlobalPos - m_pendingHoverScrollGlobalPos;
    if (delta.manhattanLength() > UIConstants::Mouse::HOVER_SCROLL_STABILITY_RADIUS_PX) {
      m_pendingHoverScrollGlobalPos = currentGlobalPos;
      m_hoverScrollTimer.start(UIConstants::Mouse::HOVER_SCROLL_DELAY_MS);
    }
    return false;
  }

  m_pendingHoverScrollWidget = widget;
  m_pendingHoverScrollGlobalPos = currentGlobalPos;
  m_pendingHoverScrollIndex = visualIndex;
  if (m_state) {
    m_state->scroll().hoverScrollPending = true;
  }
  m_hoverScrollTimer.start(UIConstants::Mouse::HOVER_SCROLL_DELAY_MS);
  return false;
}

void EventManager::clearPendingHoverScroll() {
  m_hoverScrollTimer.stop();
  m_pendingHoverScrollWidget.clear();
  m_pendingHoverScrollGlobalPos = {};
  m_pendingHoverScrollIndex = -1;
  if (m_state) {
    m_state->scroll().hoverScrollPending = false;
  }
}

void EventManager::commitPendingHoverScroll() {
  ItemWidget *widget = m_pendingHoverScrollWidget.data();
  const int visualIndex = m_pendingHoverScrollIndex;
  const QPoint stagedGlobalPos = m_pendingHoverScrollGlobalPos;

  if (!widget || visualIndex < 0 || !m_generalSettings || !m_generalSettings->selectItemOnHover ||
      !m_selectionManager || !m_scrollManager || !widget->isVisible()) {
    clearPendingHoverScroll();
    return;
  }
  if (visualIndexForWidget(widget) != visualIndex ||
      visualIndex != m_selectionManager->currentSelectedIndex()) {
    clearPendingHoverScroll();
    return;
  }
  const QPoint currentGlobalPos = QCursor::pos();
  if ((currentGlobalPos - stagedGlobalPos).manhattanLength() >
      UIConstants::Mouse::HOVER_SCROLL_STABILITY_RADIUS_PX) {
    clearPendingHoverScroll();
    return;
  }
  const QPoint cursorPos = widget->mapFromGlobal(currentGlobalPos);
  if (!widget->rect().contains(cursorPos)) {
    clearPendingHoverScroll();
    return;
  }

  if (m_state) {
    const qint64 remainingSuppressionMs =
        m_state->arrow().suppressArrowCenterUntilMs - QDateTime::currentMSecsSinceEpoch();
    if (remainingSuppressionMs > 0) {
      const int retryDelayMs =
          qMax(1, static_cast<int>(qMin<qint64>(remainingSuppressionMs + 1,
                                                UIConstants::Mouse::HOVER_SCROLL_DELAY_MS)));
      m_hoverScrollTimer.start(retryDelayMs);
      return;
    }
    // Hover-scroll's own arrow-center suppression has expired; force-clear so
    // the centering call below is not blocked by a stale flag (the bool half
    // of the suppression state can outlive its timestamp via earlier paths).
    m_state->clearArrowCenterSuppression();
  }

  clearPendingHoverScroll();

  // Drive the viewport centering directly via ViewportManager. The earlier
  // path through ScrollManager::updateSelectionForIndex routes a same-index
  // update through SelectionDisplayManager::handleSameSelectionUpdate, which
  // early-returns whenever the selection overlay is still animating from the
  // immediate hover-selection that ran HOVER_SCROLL_DELAY_MS ago. That race
  // (overlay anim outlasting the hover-scroll dwell) was why hover-induced
  // scrolling silently did nothing. Calling centerItemVertically
  // is the same canonical centering API arrow-key navigation uses, so it is
  // not gated on overlay animation state.
  if (m_viewportManager) {
    m_viewportManager->centerItemVertically(visualIndex, false);
  }
  if (m_scrollManager) {
    // Refresh selection overlay/widget state in case the viewport scroll
    // exposes/recycles widgets at the new selection's row.
    m_scrollManager->updateSelectionForIndex(visualIndex);
  }

  // After centering, schedule a deferred poll to check whether the viewport
  // scroll exposed a new item under the cursor. If so, we start a new
  // hover-scroll cycle for continuous scrolling without requiring mouse
  // movement.
  QTimer::singleShot(UIConstants::Mouse::HOVER_SCROLL_CONTINUE_DELAY_MS, this,
                     &EventManager::pollCursorForContinuousHoverScroll);
}

void EventManager::pollCursorForContinuousHoverScroll() {
  if (!m_generalSettings || !m_generalSettings->selectItemOnHover || !m_selectionManager ||
      !m_scrollManager || !m_itemScrollArea || !m_gridContainer) {
    return;
  }
  if (QApplication::activeModalWidget() || !m_stackedWidget || !m_itemsPage ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    return;
  }
  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  // Don't re-arm if a new hover scroll was already staged (e.g. by mouse move)
  if (m_pendingHoverScrollWidget) {
    return;
  }

  const QPoint globalPos = QCursor::pos();

  // Ensure cursor is still within the scroll area viewport
  QWidget *viewport = m_itemScrollArea->viewport();
  if (!viewport) {
    return;
  }
  const QPoint viewportPos = viewport->mapFromGlobal(globalPos);
  if (!viewport->rect().contains(viewportPos)) {
    return;
  }

  // Find the widget under the cursor
  QWidget *widgetAtPos = QApplication::widgetAt(globalPos);
  if (!widgetAtPos) {
    return;
  }

  // Walk up to find the ItemWidget (mirrors itemWidgetForObject)
  ItemWidget *widget = nullptr;
  for (QObject *candidate = widgetAtPos; candidate; candidate = candidate->parent()) {
    if (auto *iw = qobject_cast<ItemWidget *>(candidate)) {
      widget = iw;
      break;
    }
  }
  if (!widget || !widget->isVisible()) {
    return;
  }

  const int visualIndex = visualIndexForWidget(widget);
  if (visualIndex < 0) {
    return;
  }

  // Only continue if this is a different item from current selection -
  // if the viewport didn't expose a new row, stop continuous scrolling.
  const int currentSelection = m_selectionManager->currentSelectedIndex();
  if (visualIndex == currentSelection) {
    return;
  }

  // Select the new item immediately (same as handleHoverSelection)
  if (m_state) {
    const qint64 hoverScrollSuppressedUntil =
        QDateTime::currentMSecsSinceEpoch() + UIConstants::Mouse::HOVER_SCROLL_CONTINUE_DELAY_MS;
    m_state->arrow().suppressArrowCenterUntilMs =
        qMax(m_state->arrow().suppressArrowCenterUntilMs, hoverScrollSuppressedUntil);
  }
  m_selectionManager->selectItemByHover(visualIndex);
  if (m_detailsPaneManager && m_detailsPaneManager->isSidebarVisible()) {
    m_detailsPaneManager->updateSidebarMetadata(widget);
  }

  // Stage a new hover-scroll cycle with the shorter continue delay
  m_pendingHoverScrollWidget = widget;
  m_pendingHoverScrollGlobalPos = globalPos;
  m_pendingHoverScrollIndex = visualIndex;
  if (m_state) {
    m_state->scroll().hoverScrollPending = true;
  }
  m_hoverScrollTimer.start(UIConstants::Mouse::HOVER_SCROLL_CONTINUE_DELAY_MS);
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

  if (m_mouseManager) {
    m_mouseManager->setLeftMouseDown(true);
    m_mouseManager->clearHorizontalCandidate();
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

  if (!m_scrollManager) {
    emit clearSelectionRequested();
    event->accept();
    return true;
  }

  auto [chosen, visualIndex] =
      MouseManager::findBestWidgetForClick(clickPos, m_scrollManager, m_gridContainer);
  if (chosen && visualIndex >= 0) {
    emit widgetClicked(chosen, visualIndex, clickPos, mouseEvent);
    event->accept();
    return true;
  }
  emit clearSelectionRequested();
  event->accept();
  return true;
}

bool EventManager::applyWheelSelectionDelta(int wheelSteps) {
  if (wheelSteps == 0 || !m_scrollManager ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }

  const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];
  // the per-column step in Horizontal mode is the *effective*
  // items-per-column from the live metrics (which already honor
  // horizontalGridHeight's fallback to gridWidth). Reading collection.gridWidth
  // directly was wrong when the user configured a separate horizontal height —
  // the wheel would jump by gridWidth items even though each column actually
  // contained horizontalGridHeight items, drifting the row index across columns
  // and looking like a random vertical jitter.
  // route both gridWidth and horizontalGridHeight reads through
  // the effective-value helpers so the alt fields kick in when sidebar is
  // hidden in Expand mode.
  const bool shrink = m_scrollManager->sidebarShrinkingActive();
  int gridWidth = CollectionUtils::effectiveGridWidth(collection, shrink);
  if (collection.viewType == ViewType::Horizontal) {
    const auto &metrics = m_scrollManager->getMetrics();
    if (metrics.itemsPerRow > 0) {
      gridWidth = metrics.itemsPerRow;
    } else {
      const int hgh = CollectionUtils::effectiveHorizontalGridHeight(collection, shrink);
      if (hgh > 0) {
        gridWidth = hgh;
      }
    }
  }
  if (gridWidth <= 0) {
    return false;
  }

  int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    return false;
  }

  int currentSelection = m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
  if (currentSelection < 0) {
    currentSelection = 0;
  }

  // in Horizontal mode the wheel always advances exactly one
  // column per discrete event — direction-only, magnitude clamped to ±1.
  // Without the clamp, fast wheel motion or trackpad scrolling can return
  // wheelSteps > 1, which (multiplied by gridWidth below) would jump several
  // columns per tick and feel jittery. The global mouseWheelRows /
  // scrollVelocityMultiplier knobs tune vertical scroll feel and should not
  // compound on top of the already-coarse "skip a full gridHeight of items"
  // step. List / CoverFlow keep their existing single-item step.
  const bool isHorizontalView = (collection.viewType == ViewType::Horizontal);
  int rowDelta;
  if (isHorizontalView) {
    rowDelta = (wheelSteps > 0) ? -1 : 1;
  } else {
    int rowMultiplier = m_generalSettings ? m_generalSettings->mouseWheelRows : 1;
    // scale wheel step by the global scroll-velocity multiplier.
    // Done on the (row * wheelSteps) product so single-notch motion at 1.5×
    // yields a perceivable 1.5-row step rather than rounding down to 1.
    const double velocityMult =
        m_generalSettings ? m_generalSettings->scrollVelocityMultiplier : 1.0;
    rowDelta = -wheelSteps * rowMultiplier;
    if (velocityMult != 1.0) {
      // Round toward the direction of travel so tiny multipliers still move at
      // least 1 row per notch in the intended direction.
      const double scaled = static_cast<double>(rowDelta) * velocityMult;
      rowDelta = scaled >= 0 ? static_cast<int>(scaled + 0.5) : -static_cast<int>(-scaled + 0.5);
      if (rowDelta == 0 && wheelSteps != 0) {
        rowDelta = (-wheelSteps > 0) ? 1 : -1;
      }
    }
  }

  // List and CoverFlow move by 1 item per step instead of
  // gridWidth. Horizontal jumps one full column per tick —
  // selectionDelta = rowDelta * gridWidth where rowDelta is forced to ±1 above.
  const bool singleStep =
      (collection.viewType == ViewType::List) || (collection.viewType == ViewType::CoverFlow);
  int selectionDelta = singleStep ? rowDelta : (rowDelta * gridWidth);
  int newSelection = currentSelection + selectionDelta;

  bool wrap = m_generalSettings ? m_generalSettings->wrapNavigation : false;
  bool wrapTriggered = false;

  if (wrap) {
    if (newSelection < 0) {
      newSelection = (totalItems + (newSelection % totalItems)) % totalItems;
      wrapTriggered = true;
    } else if (newSelection >= totalItems) {
      newSelection = newSelection % totalItems;
      wrapTriggered = true;
    }
  } else {
    newSelection = qBound(0, newSelection, totalItems - 1);
  }

  if (newSelection == currentSelection) {
    return wrapTriggered;
  }

  if (m_viewportManager) {
    if (wrapTriggered) {
      m_viewportManager->setForceImmediateCenter(true);
      m_viewportManager->setWrapSequenceActive(true);
      m_viewportManager->setContinuousScrollActive(false);
    } else {
      m_viewportManager->setContinuousScrollActive(true);
    }
  }

  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(newSelection);
    QList<int> subs = getSubcollections(*m_currentCollectionIndex);
    m_selectionManager->updateFilePathForSelection(newSelection, subs);
  }
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(newSelection);
  }

  if (wrapTriggered && m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(newSelection);
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
  }

  // wheel scroll uses the bare setSelectedIndex setter, which
  // does not emit selectionChanged. Without this notify, listeners wired to
  // SelectionManager::selectionChanged (e.g. the toolbar's itemPositionLabel
  // via InteractionManager forwarding) stay frozen during wheel navigation.
  if (m_selectionManager) {
    m_selectionManager->notifySelectionChanged();
  }

  return wrapTriggered;
}
