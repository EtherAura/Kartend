// Mouse hold-scroll + key-repeat stop logic extracted from
// interactionmanager.cpp:
//   - stopRepeat (~80 LOC)
//   - isWheelScrolling
//   - onMouseHoldScrollStep (~160 LOC)
// All three remain InteractionManager members and access existing class state.
#include "interactionmanager.h"

#include <algorithm>
#include <QApplication>
#include <QTimer>

#include "animationmanager.h"
#include "interactionhelpers.h"
#include "keyboardmanager.h"
#include "mousemanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "collection/validationhelpers.h"
#include "collectiontypes.h"
#include "isettingsmanager.h"
#include "uiconstants/mouse.h"

// Stops key/mouse repeat navigation and restores artwork / centering properties
void InteractionManager::stopRepeat(bool suppressRecentering) {
  if (shuttingDown()) {
    if (m_viewportManager) {
      m_viewportManager->setRepeating(false);
      m_viewportManager->setWrapSequenceActive(false);
    }
    m_state.scroll().keyContinuous = false;
    return;
  }

  // Delegate to KeyboardManager for timer/state cleanup
  if (m_keyboardManager) {
    m_keyboardManager->stopRepeat(suppressRecentering);
  }

  // Stop mouse hold scrolling if active
  if (m_mouseManager && m_mouseManager->isMouseHoldScrolling()) {
    m_mouseManager->stopMouseHoldScrolling();
  }

  if (m_viewportManager) {
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  m_state.scroll().horizHoldActive = false;
  m_state.scroll().keyContinuous = false;
  m_state.click().armFirstClickDelay = false;
  m_state.click().pendingInitialCenter = false;

  if (m_gridContainer) {
    m_state.arrow().arrowKeyScrolling = false;
    m_state.setGlideAnimating(false);
    if (scrollMgr()) {
      scrollMgr()->refreshSelectionOverlayState();
    }
  }
  if (scrollMgr()) {
    scrollMgr()->setForceSelectionOverlayVisible(false);
  }

  if (m_itemScrollArea) {
    m_state.artwork().suppressArtwork = false;
    m_state.artwork().allowDuringSelection = true;
    m_state.clearArrowCenterSuppression();
  }

  if (m_animationManager && m_animationManager->isHorizontalAnimRunning()) {
    m_animationManager->horizontalAnimation()->stop();
  }

  if (m_state.click().selectionSuppressed) {
    int pending = m_state.click().pendingSelectionIndex;
    if (pending >= 0) {
      selectItemByIndex(pending, true);
    }
    m_state.click().selectionSuppressed = false;
    m_state.click().pendingSelectionIndex = -1;
  }

  if (m_viewportManager && !m_viewportManager->physicalKeyDown()) {
    m_viewportManager->setContinuousScrollActive(m_animationManager &&
                                                 m_animationManager->isVerticalAnimRunning());
  }

  const int selected = currentSelectedIndex();
  if (!QApplication::closingDown() && selected >= 0 && !suppressRecentering) {
    // Delay re-centering to allow scroll animations to settle after key repeat
    // stops
    QTimer::singleShot(UIConstants::Mouse::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
      const int sel = currentSelectedIndex();
      if (!QApplication::closingDown() && sel >= 0 && m_viewportManager &&
          !m_viewportManager->continuousScrollActive()) {
        centerItemVertically(sel, false);
      }
    });
  }
}

auto InteractionManager::isWheelScrolling() const -> bool {
  return m_mouseManager ? m_mouseManager->isWheelScrolling() : false;
}

// Advances selection during mouse-hold scrolling (called via MouseManager
// signal)
void InteractionManager::onMouseHoldScrollStep(int direction, bool isHorizontal) {
  if (!scrollMgr() || !m_collections || !m_currentCollectionIndex) {
    if (m_mouseManager) {
      m_mouseManager->stopMouseHoldScrolling();
    }
    return;
  }

  int totalItems = scrollMgr()->getTotalItems();
  if (totalItems <= 0) {
    if (m_mouseManager) {
      m_mouseManager->stopMouseHoldScrolling();
    }
    return;
  }

  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    if (m_mouseManager) {
      m_mouseManager->stopMouseHoldScrolling();
    }
    return;
  }

  if (isHorizontal) {
    int currentIndex = std::max(0, currentSelectedIndex());
    bool wrap = m_generalSettings ? m_generalSettings->input.wrapNavigation : false;
    bool didWrap = false;
    int nextIndex = KeyboardManager::calculateHorizontalSelection(totalItems, currentIndex,
                                                                  direction, wrap, didWrap);

    if (nextIndex == currentIndex) {
      return;
    }

    if (m_viewportManager) {
      if (didWrap) {
        m_viewportManager->setForceImmediateCenter(true);
        m_viewportManager->setWrapSequenceActive(true);
        m_viewportManager->setContinuousScrollActive(false);
        // Clear any pending selection on wrap - the wrap jump is the final
        // position
        m_state.click().selectionSuppressed = false;
        m_state.click().pendingSelectionIndex = -1;
      } else {
        m_viewportManager->setContinuousScrollActive(true);
      }
    }

    bool rowChanged = KeyboardManager::hasRowChanged(gridWidth, currentIndex, nextIndex);
    // Only suppress selection for row changes if NOT wrapping
    // Wraps should immediately finalize at the target position
    if (rowChanged && !didWrap) {
      m_state.click().selectionSuppressed = true;
    }
    // Always update pending index to current position during horizontal hold
    // so releasing the mouse lands on the current item, not the first item
    // after row change
    if (m_state.click().selectionSuppressed) {
      m_state.click().pendingSelectionIndex = nextIndex;
    }

    if (m_selectionManager) {
      m_selectionManager->setSelectedIndex(nextIndex);
    }
    QList<int> subs = getSubcollections(*m_currentCollectionIndex);
    updateFilePathForSelection(nextIndex, subs);

    // Set properties before selectItemByIndex so the selection update knows
    // we're in hold mode
    m_state.scroll().clickScroll = true;
    m_state.scroll().clickHoldAdvancing = true;

    // selectItemByIndex will call updateSelectionForIndex internally
    selectItemByIndex(nextIndex, true);

    if (rowChanged) {
      centerItemVertically(nextIndex, false);
    } else {
      ensureHorizontallyVisible(nextIndex);
    }
    return;
  }

  // Vertical scrolling
  int currentIndex = std::max(0, currentSelectedIndex());

  ViewType viewType = ViewType::Grid;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    viewType = (*m_collections)[*m_currentCollectionIndex].viewType;
  }
  const int stepSize = InteractionHelpers::stepSizeForViewType(viewType, gridWidth);

  const bool wrap = m_generalSettings ? m_generalSettings->input.wrapNavigation : false;
  const auto step = InteractionHelpers::computeVerticalHoldStep(currentIndex, direction, stepSize,
                                                                totalItems, wrap);
  int nextIndex = step.nextIndex;
  const bool didWrap = step.didWrap;

  if (nextIndex < 0 || nextIndex == currentIndex) {
    return;
  }

  if (m_viewportManager) {
    if (didWrap) {
      m_viewportManager->setForceImmediateCenter(true);
      m_viewportManager->setWrapSequenceActive(true);
      m_viewportManager->setContinuousScrollActive(false);
    } else {
      m_viewportManager->setContinuousScrollActive(true);
    }
  }

  if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < m_collections->size() &&
      gridWidth > 0) {
    int currentRow = (gridWidth > 0 ? currentIndex / gridWidth : -1);
    int targetRow = nextIndex / gridWidth;
    if (currentRow != targetRow) {
      m_state.click().selectionSuppressed = true;
      m_state.click().pendingSelectionIndex = nextIndex;
    }
  }

  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(nextIndex);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(nextIndex, subs);

  // Set properties before selectItemByIndex so the selection update knows we're
  // in hold mode
  m_state.scroll().clickScroll = true;
  m_state.scroll().clickHoldAdvancing = true;

  // selectItemByIndex will call updateSelectionForIndex internally
  selectItemByIndex(nextIndex, true);

  centerItemVertically(nextIndex, false);
}
