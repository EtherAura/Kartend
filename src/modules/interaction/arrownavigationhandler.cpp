#include "arrownavigationhandler.h"
#include "animationmanager.h"
#include "applicationcontext.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "keyboardmanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>

// ArrowNavigationHandlerSetup getter definitions
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, KeyboardManager *,
                      KeyboardManager, keyboardManager)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, ScrollManager *,
                      ScrollManager, scrollManager)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, AnimationManager *,
                      AnimationManager, animationManager)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, ViewportManager *,
                      ViewportManager, viewportManager)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, SelectionManager *,
                      SelectionManager, selectionManager)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, QScrollArea *,
                      ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, QWidget *, GridContainer,
                      gridContainer)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, QStackedWidget *,
                      StackedWidget, stackedWidget)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, QWidget *, ItemsPage,
                      itemsPage)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, QList<CollectionConfig> *,
                      Collections, collections)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, int *,
                      CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_SAME(ArrowNavigationHandlerSetup, GeneralSettings *,
                      GeneralSettings, generalSettings)
SETUP_GETTER_DEF_CTX_ONLY(ArrowNavigationHandlerSetup, InteractionStateHolder *,
                          InteractionState, interactionState)

ArrowNavigationHandler::ArrowNavigationHandler(QObject *parent)
    : QObject(parent) {}

ArrowNavigationHandler::~ArrowNavigationHandler() = default;

void ArrowNavigationHandler::setupReferences(
    const ArrowNavigationHandlerSetup &setup) {
  m_keyboardManager = setup.getKeyboardManager();
  m_scrollManager = setup.getScrollManager();
  m_animationManager = setup.getAnimationManager();
  m_viewportManager = setup.getViewportManager();
  m_selectionManager = setup.getSelectionManager();
  m_itemScrollArea = setup.getItemScrollArea();
  m_gridContainer = setup.getGridContainer();
  m_stackedWidget = setup.getStackedWidget();
  m_itemsPage = setup.getItemsPage();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_generalSettings = setup.getGeneralSettings();
  m_state = setup.getInteractionState();
}

void ArrowNavigationHandler::handleArrowKeyNavigation(int direction,
                                                      bool vertical) {
  if (!m_scrollManager ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }

  // User initiated navigation - cancel any pending automatic restore
  // to prevent it from overriding this explicit user choice
  if (m_selectionManager) {
    m_selectionManager->cancelPendingSelectionRestore();
  }

  if (m_keyboardManager) {
    m_keyboardManager->prepareKeyNavigationState();
  }

  const int totalItems = getTotalItems();
  if (totalItems == 0) {
    return;
  }

  // Clear user scroll state to ensure centering isn't blocked
  if (m_state) {
    m_state->scroll().userScrollActive = false;
    m_state->artwork().suppressArtwork = true;
    m_state->artwork().allowDuringSelection = true;
    m_state->scroll().userFreeScroll = false;
  }

  const int gridWidth = getCurrentGridWidth();
  const int currentSelection = std::max(0, getCurrentSelection());

  // Check if item was offscreen before movement
  const bool offscreenBefore =
      m_isItemOffscreen ? m_isItemOffscreen(currentSelection, gridWidth)
                        : false;

  const bool wrapEnabled = isWrapEnabled();
  if (m_viewportManager) {
    m_viewportManager->setIsWrappingNavigation(false);
  }
  if (m_keyboardManager) {
    m_keyboardManager->setWrapSequenceActive(false);
  }

  bool didWrap = false;
  const int newSelection = KeyboardManager::calculateNewSelection(
      totalItems, currentSelection, direction, wrapEnabled, vertical, gridWidth,
      didWrap);

  if (didWrap) {
    if (m_viewportManager) {
      m_viewportManager->setIsWrappingNavigation(true);
    }
    if (m_keyboardManager) {
      m_keyboardManager->setWrapSequenceActive(true);
    }
  }

  const bool isNewRow =
      SelectionManager::isNewRow(currentSelection, newSelection, gridWidth);

  const bool isWrapping =
      m_viewportManager ? m_viewportManager->isWrappingNavigation() : false;
  const bool forceImmediate = offscreenBefore || isWrapping;

  if (forceImmediate && m_viewportManager) {
    m_viewportManager->applyImmediateCenterSuppression();
  }

  if (!vertical && !isNewRow && m_itemScrollArea) {
    emit requestMinorHorizontalSuppress();
  }

  // Update selection state and notify managers
  if (m_state) {
    m_state->beginSelectionSuppression(newSelection);
  }

  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(newSelection);
  }
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(newSelection);
  }

  // Request full selection update from parent (includes file path update)
  emit requestFullSelectionUpdate(newSelection);

  performVisibilityForKeyMove(isNewRow, newSelection);

  if (m_keyboardManager) {
    Qt::Key physicalKey = Qt::Key_unknown;
    if (m_keyboardManager->consumePendingNavigationKey(physicalKey)) {
      m_keyboardManager->finalizeKeyRepeatForKey(physicalKey, direction,
                                                 vertical);
    } else {
      m_keyboardManager->finalizeKeyRepeat(nullptr, direction, vertical);
    }
  }

  emit requestFocusItemsPage();
}

void ArrowNavigationHandler::handleRepeatStep() {
  if (!m_keyboardManager || !m_keyboardManager->isRepeating() ||
      !m_keyboardManager->isPhysicalKeyDown() ||
      m_keyboardManager->repeatDelta() == 0) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }

  if (!m_scrollManager || !m_collections || !m_currentCollectionIndex) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }

  if (!m_stackedWidget || !m_itemsPage ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }

  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }

  const int totalItems = getTotalItems();
  if (totalItems <= 0) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }

  // Clear user scroll state to ensure centering isn't blocked
  if (m_state) {
    m_state->scroll().userScrollActive = false;
    m_state->scroll().userFreeScroll = false;
  }

  const int direction = m_keyboardManager->repeatDelta();
  const bool repeatVertical = m_keyboardManager->repeatVertical();
  const bool horizontal = !repeatVertical;

  const int currentSelection = std::max(0, getCurrentSelection());
  const bool wrapEnabled = isWrapEnabled();
  const int gridWidth = getCurrentGridWidth();

  bool didWrap = false;
  const int newSelection = KeyboardManager::calculateNewSelection(
      totalItems, currentSelection, direction, wrapEnabled, repeatVertical,
      gridWidth, didWrap);

  if (newSelection == currentSelection) {
    return;
  }

  if (didWrap || m_keyboardManager->isWrapSequenceActive()) {
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(true);
    }
    m_keyboardManager->setWrapSequenceActive(true);
    m_keyboardManager->setContinuousScrollActive(false);
  } else {
    m_keyboardManager->setContinuousScrollActive(true);
  }

  const bool rowChanged =
      (*m_currentCollectionIndex >= 0 &&
       *m_currentCollectionIndex < m_collections->size() && gridWidth > 0)
          ? KeyboardManager::hasRowChanged(gridWidth, currentSelection,
                                           newSelection)
          : false;

  // Set pending selection for suppressed updates
  if ((horizontal || rowChanged) && m_state) {
    m_state->beginSelectionSuppression(newSelection);
  }

  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(newSelection);
  }

  // Update scroll state
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(newSelection);
  }

  emit requestFullSelectionUpdate(newSelection);

  if (horizontal && !rowChanged) {
    if (m_viewportManager) {
      m_viewportManager->ensureHorizontallyVisible(newSelection);
    }
    return;
  }

  // In list mode, every move is a row change since there's 1 item per row
  if (m_viewportManager) {
    m_viewportManager->centerItemVertically(newSelection, false);
  }
}

void ArrowNavigationHandler::handleStopRepeat(bool suppressRecentering) {
  // Stop horizontal animation if running
  if (m_animationManager && m_animationManager->isHorizontalAnimRunning()) {
    m_animationManager->horizontalAnimation()->stop();
  }

  // Apply pending selection if suppressed
  if (m_state && m_state->isSelectionSuppressed()) {
    int pending = m_state->click().pendingSelectionIndex;
    if (pending >= 0) {
      emit requestFullSelectionUpdate(pending);
    }
    m_state->endSelectionSuppression();
  }

  // Update continuous scroll state
  if (m_keyboardManager && !m_keyboardManager->isPhysicalKeyDown()) {
    bool animRunning =
        (m_animationManager && m_animationManager->isVerticalAnimRunning());
    if (m_viewportManager) {
      m_viewportManager->setContinuousScrollActive(animRunning);
    }
  }

  // Schedule recenter if needed
  const int selected = getCurrentSelection();
  if (!QApplication::closingDown() && selected >= 0 && !suppressRecentering) {
    // Delay re-centering to allow scroll animations to settle after key repeat
    // stops
    QTimer::singleShot(
        UIConstants::Mouse::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
          bool stillActive =
              m_keyboardManager
                  ? m_keyboardManager->isContinuousScrollActive()
                  : (m_viewportManager
                         ? m_viewportManager->continuousScrollActive()
                         : false);
          const int sel = getCurrentSelection();
          if (!QApplication::closingDown() && sel >= 0 && !stillActive) {
            emit requestRecenter();
          }
        });
  }
}

void ArrowNavigationHandler::performVisibilityForKeyMove(bool isNewRow,
                                                         int newSelection) {
  if (!m_viewportManager) {
    return;
  }

  // In list mode, every move is a row change since there's 1 item per row
  bool isListMode = false;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    isListMode =
        (*m_collections)[*m_currentCollectionIndex].viewType == ViewType::List;
  }

  if (isListMode || isNewRow) {
    m_viewportManager->centerItemVertically(newSelection, false);
  } else {
    m_viewportManager->ensureHorizontallyVisible(newSelection);
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
  return m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
}

int ArrowNavigationHandler::getTotalItems() const {
  return m_scrollManager ? m_scrollManager->getTotalItems() : 0;
}

bool ArrowNavigationHandler::isWrapEnabled() const {
  return m_generalSettings ? m_generalSettings->wrapNavigation : false;
}
