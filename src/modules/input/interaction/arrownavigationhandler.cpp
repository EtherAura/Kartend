#include "arrownavigationhandler.h"
#include "animationmanager.h"
#include "applicationcontext.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "iviewportmanager.h"
#include "keyboardmanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"

#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>

// ArrowNavigationHandlerSetup getter definitions (non-manager fields only).
SETUP_GETTER_DEF_UI_SAME(ArrowNavigationHandlerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_UI_SAME(ArrowNavigationHandlerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_UI_SAME(ArrowNavigationHandlerSetup, QStackedWidget *, StackedWidget,
                         stackedWidget)
SETUP_GETTER_DEF_UI_SAME(ArrowNavigationHandlerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_COL_SAME(ArrowNavigationHandlerSetup, QList<CollectionConfig> *, Collections,
                          collections)
SETUP_GETTER_DEF_COL_SAME(ArrowNavigationHandlerSetup, int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_COL_SAME(ArrowNavigationHandlerSetup, GeneralSettings *, GeneralSettings,
                          generalSettings)

ArrowNavigationHandler::ArrowNavigationHandler(QObject *parent) : QObject(parent) {}

ArrowNavigationHandler::~ArrowNavigationHandler() = default;

void ArrowNavigationHandler::setupReferences(const ArrowNavigationHandlerSetup &setup) {
  m_ctx = setup.ctx;
  m_itemScrollArea = setup.getItemScrollArea();
  m_gridContainer = setup.getGridContainer();
  m_stackedWidget = setup.getStackedWidget();
  m_itemsPage = setup.getItemsPage();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_generalSettings = setup.getGeneralSettings();
}

void ArrowNavigationHandler::handleArrowKeyNavigation(int direction, bool vertical) {
  if (!scrollMgr() ||
      !CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }

  // User initiated navigation - cancel any pending automatic restore
  // to prevent it from overriding this explicit user choice
  if (selectionMgr()) {
    selectionMgr()->cancelPendingSelectionRestore();
  }

  if (keyboardMgr()) {
    keyboardMgr()->prepareKeyNavigationState();
  }

  const int totalItems = getTotalItems();
  if (totalItems == 0) {
    return;
  }

  // Clear user scroll state to ensure centering isn't blocked
  if (state()) {
    state()->scroll().userScrollActive = false;
    state()->artwork().suppressArtwork = true;
    state()->artwork().allowDuringSelection = true;
    state()->scroll().userFreeScroll = false;
  }

  const int gridWidth = getCurrentGridWidth();
  const int currentSelection = std::max(0, getCurrentSelection());

  // Check if item was offscreen before movement
  const bool offscreenBefore =
      m_isItemOffscreen ? m_isItemOffscreen(currentSelection, gridWidth) : false;

  const bool wrapEnabled = isWrapEnabled();
  if (viewportMgr()) {
    viewportMgr()->setIsWrappingNavigation(false);
  }
  if (keyboardMgr()) {
    keyboardMgr()->setWrapSequenceActive(false);
  }

  bool didWrap = false;
  bool horizontalLayout = false;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    horizontalLayout =
        ((*m_collections)[*m_currentCollectionIndex].viewType == ViewType::Horizontal);
  }
  const int newSelection =
      KeyboardManager::calculateNewSelection(totalItems, currentSelection, direction, wrapEnabled,
                                             vertical, gridWidth, didWrap, horizontalLayout);

  if (didWrap) {
    if (viewportMgr()) {
      viewportMgr()->setIsWrappingNavigation(true);
    }
    if (keyboardMgr()) {
      keyboardMgr()->setWrapSequenceActive(true);
    }
  }

  const bool isNewRow = SelectionManager::isNewRow(currentSelection, newSelection, gridWidth);

  const bool isWrapping = viewportMgr() ? viewportMgr()->isWrappingNavigation() : false;
  const bool forceImmediate = offscreenBefore || isWrapping;

  if (forceImmediate && viewportMgr()) {
    viewportMgr()->applyImmediateCenterSuppression();
  }

  if (!vertical && !isNewRow && m_itemScrollArea) {
    emit requestMinorHorizontalSuppress();
  }

  // Update selection state and notify managers
  if (state()) {
    state()->beginSelectionSuppression(newSelection);
  }

  if (selectionMgr()) {
    selectionMgr()->setSelectedIndex(newSelection);
  }
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(newSelection);
  }

  // Request full selection update from parent (includes file path update)
  emit requestFullSelectionUpdate(newSelection);

  performVisibilityForKeyMove(isNewRow, newSelection);

  if (keyboardMgr()) {
    Qt::Key physicalKey = Qt::Key_unknown;
    if (keyboardMgr()->consumePendingNavigationKey(physicalKey)) {
      keyboardMgr()->finalizeKeyRepeatForKey(physicalKey, direction, vertical);
    } else {
      keyboardMgr()->finalizeKeyRepeat(nullptr, direction, vertical);
    }
  }

  emit requestFocusItemsPage();
}

void ArrowNavigationHandler::handleRepeatStep() {
  if (!keyboardMgr() || !keyboardMgr()->isRepeating() || !keyboardMgr()->isPhysicalKeyDown() ||
      keyboardMgr()->repeatDelta() == 0) {
    if (keyboardMgr()) {
      keyboardMgr()->stopRepeat();
    }
    return;
  }

  if (!scrollMgr() || !m_collections || !m_currentCollectionIndex) {
    if (keyboardMgr()) {
      keyboardMgr()->stopRepeat();
    }
    return;
  }

  if (!m_stackedWidget || !m_itemsPage || m_stackedWidget->currentWidget() != m_itemsPage) {
    if (keyboardMgr()) {
      keyboardMgr()->stopRepeat();
    }
    return;
  }

  if (!CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    if (keyboardMgr()) {
      keyboardMgr()->stopRepeat();
    }
    return;
  }

  const int totalItems = getTotalItems();
  if (totalItems <= 0) {
    if (keyboardMgr()) {
      keyboardMgr()->stopRepeat();
    }
    return;
  }

  // Clear user scroll state to ensure centering isn't blocked
  if (state()) {
    state()->scroll().userScrollActive = false;
    state()->scroll().userFreeScroll = false;
  }

  const int direction = keyboardMgr()->repeatDelta();
  const bool repeatVertical = keyboardMgr()->repeatVertical();
  const bool horizontal = !repeatVertical;

  const int currentSelection = std::max(0, getCurrentSelection());
  const bool wrapEnabled = isWrapEnabled();
  const int gridWidth = getCurrentGridWidth();

  bool didWrap = false;
  bool horizontalLayout = false;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    horizontalLayout =
        ((*m_collections)[*m_currentCollectionIndex].viewType == ViewType::Horizontal);
  }
  const int newSelection =
      KeyboardManager::calculateNewSelection(totalItems, currentSelection, direction, wrapEnabled,
                                             repeatVertical, gridWidth, didWrap, horizontalLayout);

  if (newSelection == currentSelection) {
    return;
  }

  if (didWrap || keyboardMgr()->isWrapSequenceActive()) {
    if (viewportMgr()) {
      viewportMgr()->setForceImmediateCenter(true);
    }
    keyboardMgr()->setWrapSequenceActive(true);
    keyboardMgr()->setContinuousScrollActive(false);
  } else {
    keyboardMgr()->setContinuousScrollActive(true);
  }

  const bool rowChanged =
      (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < m_collections->size() &&
       gridWidth > 0)
          ? KeyboardManager::hasRowChanged(gridWidth, currentSelection, newSelection)
          : false;

  // Set pending selection for suppressed updates
  if ((horizontal || rowChanged) && state()) {
    state()->beginSelectionSuppression(newSelection);
  }

  if (selectionMgr()) {
    selectionMgr()->setSelectedIndex(newSelection);
  }

  // Update scroll state
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(newSelection);
  }

  emit requestFullSelectionUpdate(newSelection);

  if (horizontal && !rowChanged) {
    if (viewportMgr()) {
      viewportMgr()->ensureHorizontallyVisible(newSelection);
    }
    return;
  }

  // In list mode, every move is a row change since there's 1 item per row
  if (viewportMgr()) {
    viewportMgr()->centerItemVertically(newSelection, false);
  }
}

void ArrowNavigationHandler::handleStopRepeat(bool suppressRecentering) {
  // Stop horizontal animation if running
  if (animMgr() && animMgr()->isHorizontalAnimRunning()) {
    animMgr()->horizontalAnimation()->stop();
  }

  // Apply pending selection if suppressed
  if (state() && state()->isSelectionSuppressed()) {
    int pending = state()->click().pendingSelectionIndex;
    if (pending >= 0) {
      emit requestFullSelectionUpdate(pending);
    }
    state()->endSelectionSuppression();
  }

  // Update continuous scroll state
  if (keyboardMgr() && !keyboardMgr()->isPhysicalKeyDown()) {
    bool animRunning = (animMgr() && animMgr()->isVerticalAnimRunning());
    if (viewportMgr()) {
      viewportMgr()->setContinuousScrollActive(animRunning);
    }
  }

  // Schedule recenter if needed
  const int selected = getCurrentSelection();
  if (!QApplication::closingDown() && selected >= 0 && !suppressRecentering) {
    // Delay re-centering to allow scroll animations to settle after key repeat
    // stops
    QTimer::singleShot(UIConstants::Mouse::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
      bool stillActive = keyboardMgr()
                             ? keyboardMgr()->isContinuousScrollActive()
                             : (viewportMgr() ? viewportMgr()->continuousScrollActive() : false);
      const int sel = getCurrentSelection();
      if (!QApplication::closingDown() && sel >= 0 && !stillActive) {
        emit requestRecenter();
      }
    });
  }
}

void ArrowNavigationHandler::performVisibilityForKeyMove(bool isNewRow, int newSelection) {
  if (!viewportMgr()) {
    return;
  }

  // In list mode, every move is a row change since there's 1 item per row.
  // In cover flow the carousel widget centers the selection
  // itself, so we skip viewport scrolling entirely.
  bool isListMode = false;
  bool isCoverFlow = false;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    const ViewType vt = (*m_collections)[*m_currentCollectionIndex].viewType;
    isListMode = (vt == ViewType::List);
    isCoverFlow = (vt == ViewType::CoverFlow);
  }

  if (isCoverFlow) {
    return;
  }
  if (isListMode || isNewRow) {
    viewportMgr()->centerItemVertically(newSelection, false);
  } else {
    viewportMgr()->ensureHorizontallyVisible(newSelection);
  }
}

int ArrowNavigationHandler::getCurrentGridWidth() const {
  if (m_getCurrentGridWidth) {
    return m_getCurrentGridWidth();
  }
  return CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
}

int ArrowNavigationHandler::getCurrentSelection() const {
  if (m_getCurrentSelection) {
    return m_getCurrentSelection();
  }
  return selectionMgr() ? selectionMgr()->currentSelectedIndex() : -1;
}

int ArrowNavigationHandler::getTotalItems() const {
  return scrollMgr() ? scrollMgr()->getTotalItems() : 0;
}

bool ArrowNavigationHandler::isWrapEnabled() const {
  return m_generalSettings ? m_generalSettings->wrapNavigation : false;
}
