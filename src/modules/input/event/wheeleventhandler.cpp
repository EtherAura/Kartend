// Wheel-scrolling state machine extracted from EventManager. Coordinates
// selection-delta math, smooth-scroll animation handoff to AnimationManager,
// and the bookkeeping flags ScrollManager / ViewportManager rely on.
#include "wheeleventhandler.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include "animationmanager.h"
#include "collection/collectionconfig.h"
#include "collection/helpers.h"
#include "eventhelpers.h"
#include "gridlayoutcalculator.h"
#include "idetailspane.h"
#include "idetailspanemanager.h"
#include "interactionstateholder.h"
#include "iselectionmanager.h"
#include "iviewportmanager.h"
#include "mousemanager.h"
#include "scrollmanager.h"
#include "uiconstants/grid.h"
#include "uiconstants/mouse.h"

WheelEventHandler::WheelEventHandler(QObject *parent) : QObject(parent) {}
WheelEventHandler::~WheelEventHandler() = default;

void WheelEventHandler::setupReferences(const Setup &setup) {
  m_ctx = setup.ctx;
  m_itemScrollArea = setup.itemScrollArea;
  m_stackedWidget = setup.stackedWidget;
  m_itemsPage = setup.itemsPage;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_generalSettings = setup.generalSettings;
}

bool WheelEventHandler::eventBelongsToSidebar() const {
  // Wheel events over the sidebar should scroll the sidebar via Qt's normal
  // wheel routing rather than driving grid selection. The cursor position is
  // the source of truth — when the sidebar's QScrollArea has nothing to scroll,
  // Qt propagates the wheel up past it and `obj` would point at an ancestor
  // like the items page, so an obj-based check would miss it.
  if (!detailsPaneMgr()) {
    return false;
  }
  IDetailsPane *sidebar = detailsPaneMgr()->sidebarWidget();
  if (!sidebar) {
    return false;
  }
  const QWidget *widget = sidebar->asWidget();
  return widget && widget->isVisible() &&
         widget->rect().contains(widget->mapFromGlobal(QCursor::pos()));
}

bool WheelEventHandler::canProceed() const {
  return m_itemScrollArea && m_stackedWidget &&
         CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections) &&
         m_stackedWidget->currentWidget() == m_itemsPage;
}

int WheelEventHandler::computeTargetScroll(int selectedIndex, const CollectionConfig &collection,
                                           QScrollBar *axisScrollBar, bool horizontalView) const {
  // Calculate target scroll position so the new selection stays visible.
  // ScrollManager metrics override the per-collection defaults — both code
  // paths must agree on itemsPerRow / itemHeight / verticalSpacing.
  int gridWidth = CollectionUtils::effectiveGridWidth(
      collection, scrollMgr() ? scrollMgr()->sidebarShrinkingActive() : false);
  int itemHeight = collection.gridLayout.itemHeight;
  int vSpacing = collection.gridLayout.verticalSpacing;
  int headerOffset = 0;
  if (scrollMgr()) {
    const auto &metrics = scrollMgr()->getMetrics();
    gridWidth = metrics.itemsPerRow;
    itemHeight = metrics.itemHeight;
    vSpacing = metrics.verticalSpacing;
    headerOffset = metrics.headerOffset;
  }

  const int margins = UIConstants::Grid::MARGINS;
  const QRect viewport = m_itemScrollArea->viewport()->rect();
  int targetPos = 0;

  if (horizontalView && scrollMgr()) {
    // itemsPerRow means items-per-column in horizontal view; derive the
    // column index from selectedIndex / itemsPerCol and center it.
    const auto &metrics = scrollMgr()->getMetrics();
    targetPos = GridLayoutCalculator::calculateCenterScrollTarget(
        selectedIndex, viewport.width(), axisScrollBar->maximum(), metrics);
  } else {
    const int logicalTargetY = EventHelpers::computeLogicalCenteredScrollY(
        selectedIndex, gridWidth, itemHeight, vSpacing, margins, headerOffset, viewport.height());
    targetPos = logicalTargetY;
    if (viewportMgr() && viewportMgr()->getScrollScale() > 1.0) {
      targetPos = viewportMgr()->toWidgetScrollY(logicalTargetY);
    }
  }
  return qBound(0, targetPos, axisScrollBar->maximum());
}

void WheelEventHandler::onAnimationFinished() {
  if (mouseMgr()) {
    mouseMgr()->setWheelScrolling(false);
  }
  if (viewportMgr()) {
    viewportMgr()->setContinuousScrollActive(false);
  }
  if (state()) {
    state()->scroll().userScrollActive = false;
    state()->scroll().programmaticScroll = false;
    state()->clearArrowCenterSuppression();
    if (scrollMgr()) {
      scrollMgr()->refreshSelectionOverlayState();
    }
  }
  const int selectedIndex = selectionMgr() ? selectionMgr()->currentSelectedIndex() : -1;
  if (scrollMgr() && selectedIndex >= 0) {
    scrollMgr()->updateSelectionForIndex(selectedIndex);
  }
  // Selection might have scrolled outside the viewport during chained wheel
  // events; bring it back in before signaling the scroll-ended consumers.
  if (viewportMgr() && selectedIndex >= 0) {
    viewportMgr()->ensureItemVisible(selectedIndex, false);
  }
  emit scrollEnded();
}

bool WheelEventHandler::handleEvent(QObject * /*obj*/, QEvent *event) {
  // Reentrancy guard: animations + signal processing can re-enter wheel
  // handling. RAII-style reset on every early-return path.
  if (m_processing) {
    return true; // Accept to prevent default handling.
  }
  m_processing = true;
  auto resetGuard = qScopeGuard([this]() { m_processing = false; });

  if (QApplication::activeModalWidget()) {
    return false;
  }
  // When the expand-mode artwork preview overlay is visible, let the wheel
  // event propagate to it so it can cycle through the gallery entries
  // instead of moving the underlying grid selection.
  if (scrollMgr() && scrollMgr()->isArtworkPreviewVisible()) {
    return false;
  }
  if (eventBelongsToSidebar()) {
    return false;
  }
  if (!canProceed()) {
    return false;
  }

  auto *wheelEvent = static_cast<QWheelEvent *>(event);
  // canProceed() now also admits the synthetic home view (index -1), which
  // has no backing collection. Fall back to a default-constructed config
  // (ViewType::Grid) so the root tile grid still scrolls; a real collection
  // is read only when the index is valid.
  CollectionConfig fallbackCollection;
  const CollectionConfig &collection =
      CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)
          ? (*m_collections)[*m_currentCollectionIndex]
          : fallbackCollection;
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

  if (state()) {
    state()->scroll().userScrollActive = true;
    state()->scroll().programmaticScroll = true;
    state()->suppressArrowCenterFor(UIConstants::Mouse::WHEEL_SUPPRESS_ARROW_CENTER_MS);
    // refreshSelectionOverlayState is intentionally skipped here — too frequent
    // during rapid wheel events. Animation completion refreshes it.
  }

  if (applySelectionDelta(wheelSteps)) {
    // Wrap detected: bail out of animation and let updateVirtualView re-paint
    // the new visible page.
    if (mouseMgr()) {
      mouseMgr()->setWheelScrolling(false);
    }
    if (viewportMgr()) {
      viewportMgr()->setContinuousScrollActive(false);
    }
    if (animMgr() && animMgr()->isVerticalAnimRunning()) {
      animMgr()->verticalAnimation()->stop();
    }
    if (scrollMgr()) {
      scrollMgr()->updateVirtualView();
    }
    event->accept();
    return true;
  }

  const int selectedIndex = selectionMgr() ? selectionMgr()->currentSelectedIndex() : -1;
  if (selectedIndex < 0) {
    event->accept();
    return true;
  }

  const int targetPos =
      computeTargetScroll(selectedIndex, collection, axisScrollBar, horizontalView);

  if (mouseMgr()) {
    mouseMgr()->setWheelScrolling(true);
  }
  if (viewportMgr()) {
    viewportMgr()->setContinuousScrollActive(true);
  }

  if (animMgr()) {
    auto onFinished = [this]() { onAnimationFinished(); };
    if (horizontalView) {
      animMgr()->startWheelScrollAnimationHorizontal(axisScrollBar, currentPos, targetPos,
                                                     onFinished);
    } else {
      animMgr()->startWheelScrollAnimation(axisScrollBar, currentPos, targetPos, onFinished);
    }
  }

  if (scrollMgr()) {
    // Defer the virtual-view update one event-loop tick so the scrollbar's
    // position (mutated synchronously above by startWheelScrollAnimation /
    // the manual setValue path) settles before we recompute which items are
    // visible. Running it inline samples a transient mid-animation value.
    QTimer::singleShot(0, this, [this]() {
      if (scrollMgr()) {
        scrollMgr()->updateVirtualView();
      }
    });
  }

  emit scrollStarted();
  event->accept();
  return true;
}

QList<int> WheelEventHandler::getSubcollections(int parentIndex) const {
  if (selectionMgr()) {
    return selectionMgr()->getSubcollections(parentIndex);
  }
  if (!m_collections) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}

bool WheelEventHandler::applySelectionDelta(int wheelSteps) {
  if (wheelSteps == 0 || !scrollMgr() ||
      !CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }

  // The home view (index -1) has no backing collection; fall back to a
  // default-constructed config (ViewType::Grid) so the tile grid still
  // scrolls. A real collection is read only when the index is valid.
  CollectionConfig fallbackCollection;
  const CollectionConfig &collection =
      CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)
          ? (*m_collections)[*m_currentCollectionIndex]
          : fallbackCollection;
  // The per-column step in Horizontal mode is the *effective*
  // items-per-column from the live metrics (which already honor
  // horizontalGridHeight's fallback to gridWidth). Reading
  // collection.gridLayout.gridWidth directly was wrong when the user configured a
  // separate horizontal height — the wheel would jump by gridWidth items
  // even though each column actually contained horizontalGridHeight items,
  // drifting the row index across columns and looking like a random
  // vertical jitter. Route both gridWidth and horizontalGridHeight reads
  // through the effective-value helpers so the alt fields kick in when
  // sidebar is hidden in Expand mode.
  const bool shrink = scrollMgr()->sidebarShrinkingActive();
  int gridWidth = CollectionUtils::effectiveGridWidth(collection, shrink);
  if (collection.viewType == ViewType::Horizontal) {
    const auto &metrics = scrollMgr()->getMetrics();
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

  int totalItems = scrollMgr()->getTotalItems();
  if (totalItems <= 0) {
    return false;
  }

  int currentSelection = selectionMgr() ? selectionMgr()->currentSelectedIndex() : -1;
  if (currentSelection < 0) {
    currentSelection = 0;
  }

  // In Horizontal mode the wheel always advances exactly one column per
  // discrete event — direction-only, magnitude clamped to ±1. Without the
  // clamp, fast wheel motion or trackpad scrolling can return wheelSteps > 1,
  // which (multiplied by gridWidth below) would jump several columns per
  // tick and feel jittery. The global mouseWheelRows /
  // scrollVelocityMultiplier knobs tune vertical scroll feel and should not
  // compound on top of the already-coarse "skip a full gridHeight of items"
  // step. List / CoverFlow keep their existing single-item step.
  const bool isHorizontalView = (collection.viewType == ViewType::Horizontal);
  int rowDelta;
  if (isHorizontalView) {
    rowDelta = (wheelSteps > 0) ? -1 : 1;
  } else {
    int rowMultiplier = m_generalSettings ? m_generalSettings->mouseWheelRows : 1;
    // Scale wheel step by the global scroll-velocity multiplier. Done on the
    // (row * wheelSteps) product so single-notch motion at 1.5× yields a
    // perceivable 1.5-row step rather than rounding down to 1.
    const double velocityMult =
        m_generalSettings ? m_generalSettings->scrollVelocityMultiplier : 1.0;
    rowDelta = -wheelSteps * rowMultiplier;
    if (velocityMult != 1.0) {
      // Round toward the direction of travel so tiny multipliers still move
      // at least 1 row per notch in the intended direction.
      const double scaled = static_cast<double>(rowDelta) * velocityMult;
      rowDelta = scaled >= 0 ? static_cast<int>(scaled + 0.5) : -static_cast<int>(-scaled + 0.5);
      if (rowDelta == 0 && wheelSteps != 0) {
        rowDelta = (-wheelSteps > 0) ? 1 : -1;
      }
    }
  }

  // List and CoverFlow move by 1 item per step instead of gridWidth.
  // Horizontal jumps one full column per tick — selectionDelta = rowDelta *
  // gridWidth where rowDelta is forced to ±1 above.
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

  if (viewportMgr()) {
    if (wrapTriggered) {
      viewportMgr()->setForceImmediateCenter(true);
      viewportMgr()->setWrapSequenceActive(true);
      viewportMgr()->setContinuousScrollActive(false);
    } else {
      viewportMgr()->setContinuousScrollActive(true);
    }
  }

  if (selectionMgr()) {
    selectionMgr()->setSelectedIndex(newSelection);
    QList<int> subs = getSubcollections(*m_currentCollectionIndex);
    selectionMgr()->updateFilePathForSelection(newSelection, subs);
  }
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(newSelection);
  }

  if (wrapTriggered && viewportMgr()) {
    viewportMgr()->applyImmediateViewportPositioningForSelection(newSelection);
    if (scrollMgr()) {
      scrollMgr()->updateVirtualView();
    }
  }

  // Wheel scroll uses the bare setSelectedIndex setter, which does not emit
  // selectionChanged. Without this notify, listeners wired to
  // SelectionManager::selectionChanged (e.g. the toolbar's itemPositionLabel
  // via InteractionManager forwarding) stay frozen during wheel navigation.
  if (selectionMgr()) {
    selectionMgr()->notifySelectionChanged();
  }

  return wrapTriggered;
}
