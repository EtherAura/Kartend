// Hover-to-select state machine extracted from EventManager. Owns the dwell
// timer, the staged hover-target widget pointer, and the post-recenter
// continue-poll. EventManager forwards Enter / MouseMove events here and
// otherwise stays out of the way.
#include "hoverscrollhandler.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QWidget>

#include "collection/validationhelpers.h"
#include "idetailspanemanager.h"
#include "interactionstateholder.h"
#include "iscrolldatasource.h"
#include "iselectionmanager.h"
#include "iselectionoverlayscroll.h"
#include "itemwidget.h"
#include "iviewportmanager.h"
#include "uiconstants/mouse.h"

namespace {

ItemWidget *itemWidgetForObject(QObject *obj) {
  for (QObject *candidate = obj; candidate; candidate = candidate->parent()) {
    if (auto *widget = qobject_cast<ItemWidget *>(candidate)) {
      return widget;
    }
  }
  return nullptr;
}

int visualIndexForWidget(ItemWidget *widget, IScrollDataSource *scroll) {
  if (!widget || !scroll) {
    return -1;
  }
  const auto &active = scroll->getActiveWidgets();
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() == widget) {
      return it.key();
    }
  }
  return -1;
}

} // namespace

HoverScrollHandler::HoverScrollHandler(QObject *parent) : QObject(parent) {
  m_dwellTimer.setSingleShot(true);
  connect(&m_dwellTimer, &QTimer::timeout, this, &HoverScrollHandler::commitPendingScroll);
}

HoverScrollHandler::~HoverScrollHandler() = default;

void HoverScrollHandler::setupReferences(const Setup &setup) {
  m_ctx = setup.ctx;
  m_itemScrollArea = setup.itemScrollArea;
  m_gridContainer = setup.gridContainer;
  m_stackedWidget = setup.stackedWidget;
  m_itemsPage = setup.itemsPage;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_generalSettings = setup.generalSettings;
}

bool HoverScrollHandler::handleEvent(QObject *obj, QEvent *event, bool isRestoringSelection) {
  if (!m_generalSettings || !m_generalSettings->input.selectItemOnHover || isRestoringSelection) {
    clearPendingScroll();
    return false;
  }
  if (QApplication::activeModalWidget() || !m_stackedWidget || !m_itemsPage ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    clearPendingScroll();
    return false;
  }
  if (!scrollData() || !selectionMgr() || !m_itemScrollArea || !m_gridContainer) {
    clearPendingScroll();
    return false;
  }
  if (!CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    clearPendingScroll();
    return false;
  }

  QPoint currentGlobalPos = QCursor::pos();
  const bool isMouseMove = event && event->type() == QEvent::MouseMove;
  if (isMouseMove) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent) {
      currentGlobalPos = mouseEvent->globalPosition().toPoint();
      if (mouseEvent->buttons() != Qt::NoButton) {
        return false;
      }
    }
  }

  ItemWidget *widget = itemWidgetForObject(obj);
  if (!widget || !widget->isVisible()) {
    // Don't cancel an in-flight dwell for events from non-ItemWidget objects
    // (overlay, viewport, virtual container, etc.). The commit handler
    // already validates cursor-over-widget before scrolling.
    if (!m_pendingWidget) {
      clearPendingScroll();
    }
    return false;
  }

  const int visualIndex = visualIndexForWidget(widget, scrollData());
  if (visualIndex < 0) {
    if (!m_pendingWidget) {
      clearPendingScroll();
    }
    return false;
  }

  const bool samePending = m_pendingWidget == widget && m_pendingIndex == visualIndex;
  const bool alreadySelected = visualIndex == selectionMgr()->currentSelectedIndex();
  if (alreadySelected && !samePending) {
    clearPendingScroll();
    return false;
  }

  if (!alreadySelected) {
    if (state()) {
      const qint64 hoverScrollSuppressedUntil =
          QDateTime::currentMSecsSinceEpoch() + UIConstants::Mouse::HOVER_SCROLL_DELAY_MS;
      state()->arrow().suppressArrowCenterUntilMs =
          qMax(state()->arrow().suppressArrowCenterUntilMs, hoverScrollSuppressedUntil);
    }
    selectionMgr()->selectItemByHover(visualIndex);
    if (detailsPaneMgr() && detailsPaneMgr()->isSidebarVisible()) {
      detailsPaneMgr()->updateSidebarMetadata(widget);
    }
  }

  if (samePending) {
    const QPoint delta = currentGlobalPos - m_pendingGlobalPos;
    if (delta.manhattanLength() > UIConstants::Mouse::HOVER_SCROLL_STABILITY_RADIUS_PX) {
      m_pendingGlobalPos = currentGlobalPos;
      m_dwellTimer.start(UIConstants::Mouse::HOVER_SCROLL_DELAY_MS);
    }
    return false;
  }

  m_pendingWidget = widget;
  m_pendingGlobalPos = currentGlobalPos;
  m_pendingIndex = visualIndex;
  if (state()) {
    state()->scroll().hoverScrollPending = true;
  }
  m_dwellTimer.start(UIConstants::Mouse::HOVER_SCROLL_DELAY_MS);
  return false;
}

void HoverScrollHandler::clearPendingScroll() {
  m_dwellTimer.stop();
  m_pendingWidget.clear();
  m_pendingGlobalPos = {};
  m_pendingIndex = -1;
  if (state()) {
    state()->scroll().hoverScrollPending = false;
  }
}

void HoverScrollHandler::commitPendingScroll() {
  ItemWidget *widget = m_pendingWidget.data();
  const int visualIndex = m_pendingIndex;
  const QPoint stagedGlobalPos = m_pendingGlobalPos;

  if (!widget || visualIndex < 0 || !m_generalSettings ||
      !m_generalSettings->input.selectItemOnHover || !selectionMgr() || !scrollData() ||
      !widget->isVisible()) {
    clearPendingScroll();
    return;
  }
  if (visualIndexForWidget(widget, scrollData()) != visualIndex ||
      visualIndex != selectionMgr()->currentSelectedIndex()) {
    clearPendingScroll();
    return;
  }
  const QPoint currentGlobalPos = QCursor::pos();
  if ((currentGlobalPos - stagedGlobalPos).manhattanLength() >
      UIConstants::Mouse::HOVER_SCROLL_STABILITY_RADIUS_PX) {
    clearPendingScroll();
    return;
  }
  const QPoint cursorPos = widget->mapFromGlobal(currentGlobalPos);
  if (!widget->rect().contains(cursorPos)) {
    clearPendingScroll();
    return;
  }

  if (state()) {
    const qint64 remainingSuppressionMs =
        state()->arrow().suppressArrowCenterUntilMs - QDateTime::currentMSecsSinceEpoch();
    if (remainingSuppressionMs > 0) {
      const int retryDelayMs =
          qMax(1, static_cast<int>(qMin<qint64>(remainingSuppressionMs + 1,
                                                UIConstants::Mouse::HOVER_SCROLL_DELAY_MS)));
      m_dwellTimer.start(retryDelayMs);
      return;
    }
    // Hover-scroll's own arrow-center suppression has expired; force-clear so
    // the centering call below is not blocked by a stale flag (the bool half
    // of the suppression state can outlive its timestamp via earlier paths).
    state()->clearArrowCenterSuppression();
  }

  clearPendingScroll();

  // Drive the viewport centering directly via ViewportManager. Routing
  // through ScrollManager::updateSelectionForIndex would early-return because
  // the selection overlay animation from the immediate hover-selection a
  // moment ago is still running (handleSameSelectionUpdate guards on that).
  // centerItemVertically is the same canonical centering API arrow-key
  // navigation uses, so it's not gated on overlay animation state.
  if (viewportMgr()) {
    viewportMgr()->centerItemVertically(visualIndex, false);
  }
  if (scrollOverlay()) {
    scrollOverlay()->updateSelectionForIndex(visualIndex);
  }

  // After centering, schedule a deferred poll to check whether the viewport
  // scroll exposed a new item under the cursor. If so, we start a new
  // hover-scroll cycle for continuous scrolling without requiring mouse
  // movement.
  QTimer::singleShot(UIConstants::Mouse::HOVER_SCROLL_CONTINUE_DELAY_MS, this,
                     &HoverScrollHandler::pollCursorForContinue);
}

void HoverScrollHandler::pollCursorForContinue() {
  if (!m_generalSettings || !m_generalSettings->input.selectItemOnHover || !selectionMgr() ||
      !scrollData() || !m_itemScrollArea || !m_gridContainer) {
    return;
  }
  if (QApplication::activeModalWidget() || !m_stackedWidget || !m_itemsPage ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    return;
  }
  if (!CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  // Don't re-arm if a new hover scroll was already staged (e.g. by mouse move)
  if (m_pendingWidget) {
    return;
  }

  const QPoint globalPos = QCursor::pos();

  QWidget *viewport = m_itemScrollArea->viewport();
  if (!viewport) {
    return;
  }
  const QPoint viewportPos = viewport->mapFromGlobal(globalPos);
  if (!viewport->rect().contains(viewportPos)) {
    return;
  }

  QWidget *widgetAtPos = QApplication::widgetAt(globalPos);
  if (!widgetAtPos) {
    return;
  }

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

  const int visualIndex = visualIndexForWidget(widget, scrollData());
  if (visualIndex < 0) {
    return;
  }

  // Only continue if this is a different item from current selection — if
  // the viewport didn't expose a new row, stop continuous scrolling.
  if (visualIndex == selectionMgr()->currentSelectedIndex()) {
    return;
  }

  if (state()) {
    const qint64 hoverScrollSuppressedUntil =
        QDateTime::currentMSecsSinceEpoch() + UIConstants::Mouse::HOVER_SCROLL_CONTINUE_DELAY_MS;
    state()->arrow().suppressArrowCenterUntilMs =
        qMax(state()->arrow().suppressArrowCenterUntilMs, hoverScrollSuppressedUntil);
  }
  selectionMgr()->selectItemByHover(visualIndex);
  if (detailsPaneMgr() && detailsPaneMgr()->isSidebarVisible()) {
    detailsPaneMgr()->updateSidebarMetadata(widget);
  }

  m_pendingWidget = widget;
  m_pendingGlobalPos = globalPos;
  m_pendingIndex = visualIndex;
  if (state()) {
    state()->scroll().hoverScrollPending = true;
  }
  m_dwellTimer.start(UIConstants::Mouse::HOVER_SCROLL_CONTINUE_DELAY_MS);
}
