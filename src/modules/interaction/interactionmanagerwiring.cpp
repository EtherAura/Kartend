// Signal/slot wiring between InteractionManager and its owned sub-managers.
// Extracted from interactionmanager.cpp to keep that file focused on input
// handling and selection orchestration. All functions here remain
// InteractionManager members.
#include "interactionmanager.h"

#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollArea>
#include <QVariant>

#include "alphabeticnavigationhandler.h"
#include "animationmanager.h"
#include "arrownavigationhandler.h"
#include "attractmanager.h"
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "mousemanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "collectionutils.h"
#include "itemwidget.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sidebarmanager.h"
#include "uiconstants.h"

void InteractionManager::connectSearchManagerSignals() {
  connect(m_searchManager.get(), &SearchManager::requestClearSelection, this,
          &InteractionManager::clearSelection);
  connect(m_searchManager.get(), &SearchManager::requestSelectionRestore, this, [this](int index) {
    if (m_navigationManager) {
      m_navigationManager->scheduleSelectionRestore(index, UIConstants::Selection::RESTORE_STEPS,
                                                    UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                                                    UIConstants::Selection::RESTORE_MAX_DELAY_MS);
    }
  });
  connect(m_searchManager.get(), &SearchManager::requestScrollbarRecovery, this,
          &InteractionManager::scheduleScrollbarRecovery);
}

void InteractionManager::connectSelectionManagerSignals() {
  connect(m_selectionManager.get(), &SelectionManager::selectionChanged, this, [this](int index) {
    // Selection moved → reset expand-mode state so the next
    // activation expands first, not launches.
    m_state.clearExpandedItem();
    emit selectionChanged(index);
  });
  connect(m_selectionManager.get(), &SelectionManager::requestFocusItemsPage, this, [this]() {
    if (m_itemsPage) {
      m_itemsPage->setFocus();
    }
  });
  connect(m_selectionManager.get(), &SelectionManager::requestStopScrollAnimations, this, [this]() {
    if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
      m_animationManager->verticalAnimation()->stop();
    }
  });
  connect(m_selectionManager.get(), &SelectionManager::requestCenterVertically, this,
          &InteractionManager::centerItemVertically);
  connect(m_selectionManager.get(), &SelectionManager::requestEnsureHorizontallyVisible, this,
          &InteractionManager::ensureHorizontallyVisible);
  connect(m_selectionManager.get(), &SelectionManager::requestStopRepeat, this,
          [this]() { stopRepeat(); });
}

void InteractionManager::connectKeyboardManagerSignals() {
  connect(m_keyboardManager.get(), &KeyboardManager::requestSelectionMove, this,
          &InteractionManager::handleArrowKeyNavigation);
  connect(m_keyboardManager.get(), &KeyboardManager::requestAlphabeticNavigation, this,
          &InteractionManager::handleAlphabeticNavigation);
  connect(m_keyboardManager.get(), &KeyboardManager::requestJumpToEdge, this,
          &InteractionManager::handleJumpToEdge);
  connect(m_keyboardManager.get(), &KeyboardManager::requestEnterAction, this, [this]() {
    if (m_scrollManager) {
      const int totalItems = m_scrollManager->getTotalItems();
      processEnterOrReturnKey(totalItems);
    }
  });
  connect(m_keyboardManager.get(), &KeyboardManager::requestSearchModeToggle, this,
          &InteractionManager::toggleSearchMode);
  connect(m_keyboardManager.get(), &KeyboardManager::requestSearchBarFocus, this,
          [this]() { (void)handleSlashKey(); });
  connect(m_keyboardManager.get(), &KeyboardManager::requestEscapeAction, this,
          [this]() { (void)handleEscapeKey(); });
  connect(m_keyboardManager.get(), &KeyboardManager::requestClearSearchBar, this, [this]() {
    if (m_searchBar) {
      m_state.search().clearedByEscape = true;
      m_searchBar->clear();
    }
  });
  connect(m_keyboardManager.get(), &KeyboardManager::requestFocusGrid, this, [this]() {
    if (m_gridContainer) {
      m_gridContainer->setFocus(Qt::OtherFocusReason);
    }
  });
  connect(m_keyboardManager.get(), &KeyboardManager::requestScrollAnimationStop, this, [this]() {
    if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
      m_animationManager->verticalAnimation()->stop();
    }
  });
  connect(m_keyboardManager.get(), &KeyboardManager::repeatStepRequested, this,
          &InteractionManager::onKeyboardRepeatStep);
  connect(m_keyboardManager.get(), &KeyboardManager::stopRepeatRequested, this,
          &InteractionManager::onKeyboardStopRepeat);
}

void InteractionManager::connectGamepadManagerSignals() {
  connect(m_gamepadManager.get(), &GamepadManager::requestSelectionMove, this,
          [this](int direction, bool vertical) {
            int effectiveDirection = direction;
            bool effectiveVertical = vertical;
            ViewType vt = ViewType::Grid;
            if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
              vt = (*m_collections)[*m_currentCollectionIndex].viewType;
            }
            if (vt == ViewType::Horizontal) {
              // Kartend-dx9t: in Horizontal mode the d-pad axes swap roles —
              // vertical stick steps within the column (±1, no gridWidth
              // multiplier), horizontal stick advances columns (±gridWidth,
              // routed via the vertical-wrap path so wrap behavior matches
              // Up/Down in Grid mode). Mirrors the keyboard mapping.
              if (vertical) {
                effectiveDirection = direction;
                effectiveVertical = false;
              } else {
                const int gridWidth = getCurrentGridWidth();
                if (gridWidth > 0 && std::abs(direction) < gridWidth) {
                  effectiveDirection = direction * gridWidth;
                }
                effectiveVertical = true;
              }
            } else if (vertical) {
              // Check if we're in list mode - don't multiply by gridWidth
              bool isListMode = (vt == ViewType::List);
              if (!isListMode) {
                const int gridWidth = getCurrentGridWidth();
                if (gridWidth > 0 && std::abs(direction) < gridWidth) {
                  effectiveDirection = direction * gridWidth;
                }
              }
            }
            handleArrowKeyNavigation(effectiveDirection, effectiveVertical);
          });
  connect(m_gamepadManager.get(), &GamepadManager::requestEnterAction, this, [this]() {
    if (m_scrollManager) {
      const int totalItems = m_scrollManager->getTotalItems();
      processEnterOrReturnKey(totalItems);
    }
  });
  connect(m_gamepadManager.get(), &GamepadManager::requestEscapeAction, this,
          [this]() { (void)handleEscapeKey(); });
  connect(m_gamepadManager.get(), &GamepadManager::requestToggleSidebarAction, this, [this]() {
    if (m_sidebarManager) {
      m_sidebarManager->toggleSidebar();
    }
  });
  connect(m_gamepadManager.get(), &GamepadManager::requestScrollAnimationStop, this, [this]() {
    if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
      m_animationManager->verticalAnimation()->stop();
    }
  });
}

void InteractionManager::connectAnimationManagerSignals() {
  connect(m_animationManager.get(), &AnimationManager::requestVirtualViewUpdate, this, [this]() {
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
  });
  connect(m_animationManager.get(), &AnimationManager::requestSelectionUpdate, this, [this]() {
    if (m_scrollManager) {
      int idxDyn = m_state.isSelectionSuppressed() ? m_state.pendingSelectionIndex()
                                                   : currentSelectedIndex();
      if (idxDyn >= 0) {
        m_scrollManager->updateSelectionForIndex(idxDyn);
      }
    }
  });
  connect(m_animationManager.get(), &AnimationManager::requestSelectionOverlayRefresh, this,
          [this]() {
            if (m_scrollManager) {
              m_scrollManager->refreshSelectionOverlayState();
            }
          });
  connect(m_animationManager.get(), &AnimationManager::requestGlideAnimationStart, this, [this]() {
    if (m_gridContainer) {
      m_state.setGlideAnimating(true);
      if (m_scrollManager) {
        m_scrollManager->refreshSelectionOverlayState();
      }
    }
  });
  connect(m_animationManager.get(), &AnimationManager::horizontalAnimationFinished, this, [this]() {
    if (m_gridContainer) {
      m_state.setGlideAnimating(false);
      if (m_scrollManager) {
        m_scrollManager->refreshSelectionOverlayState();
      }
    }
  });
}

void InteractionManager::connectMouseManagerSignals() {
  connect(m_mouseManager.get(), &MouseManager::scrollStepRequested, this,
          &InteractionManager::onMouseHoldScrollStep);
  connect(m_mouseManager.get(), &MouseManager::holdScrollingStarted, this,
          [this](bool isHorizontal) {
            Q_UNUSED(isHorizontal);
            if (m_viewportManager) {
              m_viewportManager->setContinuousScrollActive(true);
              m_viewportManager->setRepeating(true);
              m_viewportManager->setPhysicalKeyDown(true);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::holdScrollingStopped, this, [this]() {
    if (m_state.isSelectionSuppressed()) {
      int pending = m_state.endSelectionSuppression();
      if (pending >= 0) {
        selectItemByIndex(pending, true);
      }
    }
    bool repeating = m_viewportManager ? m_viewportManager->isRepeating() : false;
    if (!repeating) {
      if (m_viewportManager) {
        m_viewportManager->setContinuousScrollActive(false);
        m_viewportManager->setPhysicalKeyDown(false);
      }
      m_state.artwork().suppressArtwork = false;
      m_state.artwork().allowDuringSelection = true;
    }
    if (m_viewportManager) {
      m_viewportManager->setWrapSequenceActive(false);
    }
  });
  connect(m_mouseManager.get(), &MouseManager::requestSelectionUpdate, this, [this](int index) {
    if (index >= 0) {
      QList<int> subs = getSubcollections(*m_currentCollectionIndex);
      if (m_selectionManager) {
        m_selectionManager->setSelectedIndex(index);
      }
      updateFilePathForSelection(index, subs);
      if (m_scrollManager) {
        m_scrollManager->updateSelectionForIndex(index);
      }
      selectItemByIndex(index, true);
    }
  });
  connect(m_mouseManager.get(), &MouseManager::requestOverlayVisibility, this,
          [this](bool visible) {
            if (m_scrollManager) {
              m_scrollManager->setForceSelectionOverlayVisible(visible);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::requestScrollAreaProperty, this,
          [this](const char *name, bool value) {
            if (m_itemScrollArea) {
              m_itemScrollArea->setProperty(name, value);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::requestSetProperty, this,
          [this](const char *name, const QVariant &value) { setProperty(name, value); });
}

void InteractionManager::connectViewportManagerSignals() {
  connect(m_viewportManager.get(), &ViewportManager::requestSelectionUpdate, this,
          [this](int idxDyn) {
            if (m_scrollManager) {
              int idx = (idxDyn >= 0) ? idxDyn : currentSelectedIndex();
              if (idx >= 0) {
                m_scrollManager->updateSelectionForIndex(idx);
              }
            }
          });
}

void InteractionManager::connectAttractManagerSignals() {
  // Only item selection changes should reset attract timeout / stop active
  // attract scrolling. Mouse movement/focus churn must not count as activity
  // for attract mode.
  connect(m_selectionManager.get(), &SelectionManager::selectionChanged, m_attractManager.get(),
          [this](int index) {
            Q_UNUSED(index);
            // Selection changes that AttractManager itself drives via the
            // advance-selection timer must not be treated as user activity,
            // otherwise attract mode would immediately stop on its own tick.
            if (m_attractManager->isDrivingSelection()) {
              return;
            }
            m_attractManager->onActivityDetected();
          });
  connect(m_attractManager.get(), &AttractManager::requestSelectIndex, this,
          [this](int index) {
            // Explicit viewport centering: selectItemByIndex only centers when
            // the target widget is already materialized. With random advance
            // mode the target is usually far outside the viewport, so without
            // this the virtual scroll never realises the widget and the jump
            // silently fails.
            centerItemVertically(index, false);
            selectItemByIndex(index, true);
          });
}

void InteractionManager::connectEventManagerSignals() {
  connect(m_eventManager.get(), &EventManager::widgetDoubleClicked, this,
          &InteractionManager::handleWidgetDoubleClickedWithCollection);
  connect(m_eventManager.get(), &EventManager::contextMenuRequested, this,
          &InteractionManager::showContextMenu);
  connect(m_eventManager.get(), &EventManager::mediaPreviewRequested, this,
          &InteractionManager::onMediaPreviewRequested);
  connect(m_eventManager.get(), &EventManager::artworkTypeCycleRequested, this,
          &InteractionManager::onArtworkTypeCycleRequested);
  connect(m_eventManager.get(), &EventManager::widgetClicked, this,
          [this](ItemWidget *widget, int visualIndex, const QPoint &clickPos, QMouseEvent *event) {
            Q_UNUSED(widget);
            if (m_selectionManager && visualIndex >= 0) {
              // Get previous selection BEFORE handleWidgetSelectionByIndex
              // changes it
              const int previousSelection = currentSelectedIndex();
              const int clickedIndex =
                  m_selectionManager->handleWidgetSelectionByIndex(visualIndex, clickPos, event);
              if (clickedIndex >= 0 && m_mouseManager) {
                const int gridWidth = getCurrentGridWidth();
                const int totalItems = m_scrollManager ? m_scrollManager->getTotalItems() : 0;
                m_mouseManager->updateClickHoldHorizontalCandidate(previousSelection, clickedIndex,
                                                                   gridWidth);
                m_mouseManager->startClickHoldTimer(clickPos, clickedIndex, gridWidth, totalItems);
              }
            }
          });
  connect(m_eventManager.get(), &EventManager::clearSelectionRequested, this,
          &InteractionManager::clearSelectionAndFocus);
  connect(m_eventManager.get(), &EventManager::requestStopRepeat, this,
          &InteractionManager::stopRepeat);
}
