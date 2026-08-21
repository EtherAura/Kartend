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

#include "collection/validationhelpers.h"
#include "collectiontypes.h"
#include "icollectiontreecontroller.h"
#include "idetailspanemanager.h"
#include "inavigationmanager.h"
#include "itemwidget.h"
#include "scrollmanager.h"
#include "uiconstants/selection.h"

void InteractionManager::connectSearchManagerSignals() {
  connect(m_searchManager.get(), &SearchManager::requestClearSelection, this,
          &InteractionManager::clearSelection);
  connect(m_searchManager.get(), &SearchManager::requestSelectionRestore, this, [this](int index) {
    if (navMgr()) {
      navMgr()->scheduleSelectionRestore(index, UIConstants::Selection::RESTORE_MAX_DELAY_MS);
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
    if (scrollMgr()) {
      const int totalItems = scrollMgr()->getTotalItems();
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
  connect(m_keyboardManager.get(), &KeyboardManager::requestHomeView, this, [this]() {
    if (navMgr()) {
      navMgr()->loadRootView();
    }
  });
  connect(m_keyboardManager.get(), &KeyboardManager::requestToggleCollectionTree, this, [this]() {
    if (ICollectionTreeController *tree = m_ctx ? m_ctx->collectionTreeController() : nullptr) {
      tree->toggleVisible();
    }
  });
}

bool InteractionManager::modalInputGateActive() const {
  return m_eventManager && m_eventManager->modalInputGateActive();
}

void InteractionManager::connectGamepadManagerSignals() {
  // Keyboard/mouse grid input is gated by EventManager::filterEvent while a
  // modal widget or the scrape-result dialog is up; gamepad input reaches
  // these connections without ever crossing that filter, so every
  // grid-affecting slot re-checks the shared gate (modalInputGateActive())
  // before acting. requestScrollAnimationStop stays ungated — stopping a
  // stale scroll animation when a dialog opens is harmless and desirable.
  connect(m_gamepadManager.get(), &GamepadManager::requestSelectionMove, this,
          [this](int direction, bool vertical) {
            if (modalInputGateActive()) {
              return;
            }
            // In expand mode the directions cycle ARTWORK, not the
            // selection hidden behind the overlay (field report
            // 2026-08-18).
            if (visibleArtworkOverlay()) {
              // Identical to what the keyboard does: cycle this item's
              // artwork, and only step to the neighbouring item when the
              // overlay reports it has run out (field report 2026-08-18:
              // the gamepad was jumping items instead of cycling).
              (void)sendKeyToArtworkOverlay(direction < 0 ? Qt::Key_Left : Qt::Key_Right);
              return;
            }
            // Plain directions always mean the grid (user decision
            // 2026-08-17) — pull focus home if the section chord parked it
            // elsewhere, then move as usual.
            returnGamepadFocusToGrid();
            int effectiveDirection = direction;
            bool effectiveVertical = vertical;
            ViewType vt = ViewType::Grid;
            if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
              vt = (*m_collections)[*m_currentCollectionIndex].viewType;
            }
            if (vt == ViewType::Horizontal) {
              // in Horizontal mode the d-pad axes swap roles —
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
    if (modalInputGateActive()) {
      return;
    }
    // While expand mode is up, confirm must not launch the item behind
    // it; the overlay decides what confirm means there.
    if (visibleArtworkOverlay()) {
      return;
    }
    // Confirm belongs to the focused section: activating a tree row or a
    // toolbar button must never fall through and launch the grid item that
    // happened to be selected.
    if (activateFocusedSection()) {
      return;
    }
    if (scrollMgr()) {
      const int totalItems = scrollMgr()->getTotalItems();
      processEnterOrReturnKey(totalItems);
    }
  });
  connect(m_gamepadManager.get(), &GamepadManager::requestEscapeAction, this, [this]() {
    if (modalInputGateActive()) {
      return;
    }
    // Back dismisses the expanded artwork rather than leaving the
    // collection behind it (field report 2026-08-18). Escape goes to the
    // overlay's own handler, the same one the keyboard uses.
    if (sendKeyToArtworkOverlay(Qt::Key_Escape)) {
      return;
    }
    (void)handleEscapeKey();
  });
  connect(m_gamepadManager.get(), &GamepadManager::requestToggleSidebarAction, this, [this]() {
    if (modalInputGateActive()) {
      return;
    }
    if (detailsPaneMgr()) {
      detailsPaneMgr()->toggleSidebar();
    }
  });
  connect(
      m_gamepadManager.get(), &GamepadManager::requestToggleCollectionTreeAction, this, [this]() {
        if (modalInputGateActive()) {
          return;
        }
        if (ICollectionTreeController *tree = m_ctx ? m_ctx->collectionTreeController() : nullptr) {
          tree->toggleVisible();
        }
      });
  connect(m_gamepadManager.get(), &GamepadManager::requestFocusSectionMove, this,
          [this](int dx, int dy) {
            if (modalInputGateActive()) {
              return;
            }
            moveFocusSection(dx, dy);
          });
  connect(m_gamepadManager.get(), &GamepadManager::modifierHeldChanged, this, [this](bool held) {
    if (modalInputGateActive()) {
      return;
    }
    setFocusModifierActive(held);
  });
  connect(m_gamepadManager.get(), &GamepadManager::requestPaneScroll, this, [this](int steps) {
    if (modalInputGateActive()) {
      return;
    }
    // The vertical axis belongs to the details pane unless the modifier
    // claims it for section switching (user decision 2026-08-18). Repeat
    // ticks SCROLL only: stepping to the next region happens on the flick
    // edge, so a held stick never races through the pane.
    if (visibleArtworkOverlay()) {
      return; // expand mode owns the stick
    }
    if (!m_focusModifierHeld && currentFocusSection().kind != FocusSection::Tree) {
      (void)driveDetailsPane(steps, /*allowAdvance=*/false);
    }
  });
  connect(m_gamepadManager.get(), &GamepadManager::requestRightStickFlick, this,
          [this](int dx, int dy) {
            if (modalInputGateActive()) {
              return;
            }
            (void)routeSectionInput(dx, dy);
          });
  connect(m_gamepadManager.get(), &GamepadManager::requestScrollAnimationStop, this, [this]() {
    if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
      m_animationManager->verticalAnimation()->stop();
    }
  });
}

void InteractionManager::connectAnimationManagerSignals() {
  connect(m_animationManager.get(), &AnimationManager::requestVirtualViewUpdate, this, [this]() {
    if (scrollMgr()) {
      scrollMgr()->updateVirtualView();
    }
  });
  connect(m_animationManager.get(), &AnimationManager::requestSelectionUpdate, this, [this]() {
    if (scrollMgr()) {
      int idxDyn = m_state.isSelectionSuppressed() ? m_state.pendingSelectionIndex()
                                                   : currentSelectedIndex();
      if (idxDyn >= 0) {
        scrollMgr()->updateSelectionForIndex(idxDyn);
      }
    }
  });
  connect(m_animationManager.get(), &AnimationManager::requestSelectionOverlayRefresh, this,
          [this]() {
            if (scrollMgr()) {
              scrollMgr()->refreshSelectionOverlayState();
            }
          });
  connect(m_animationManager.get(), &AnimationManager::requestGlideAnimationStart, this, [this]() {
    if (m_gridContainer) {
      m_state.setGlideAnimating(true);
      if (scrollMgr()) {
        scrollMgr()->refreshSelectionOverlayState();
      }
    }
  });
  connect(m_animationManager.get(), &AnimationManager::horizontalAnimationFinished, this, [this]() {
    if (m_gridContainer) {
      m_state.setGlideAnimating(false);
      if (scrollMgr()) {
        scrollMgr()->refreshSelectionOverlayState();
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
      if (scrollMgr()) {
        scrollMgr()->updateSelectionForIndex(index);
      }
      selectItemByIndex(index, true);
    }
  });
  connect(m_mouseManager.get(), &MouseManager::requestOverlayVisibility, this,
          [this](bool visible) {
            if (scrollMgr()) {
              scrollMgr()->setForceSelectionOverlayVisible(visible);
            }
          });
  // Kartend-8jym: the requestScrollAreaProperty + requestSetProperty
  // signal/slot connections previously sat here. Both signals were
  // declared but never emitted anywhere in src/, so the connections
  // were dead-end wiring for the dynamic-Qt-property antipattern
  // Kartend-0zhz retired. Removed wholesale; reinstate only if a real
  // emit site shows up, in which case it should route through a typed
  // accessor on InteractionStateHolder rather than QObject::setProperty.
}

void InteractionManager::connectViewportManagerSignals() {
  connect(m_viewportManager.get(), &ViewportManager::requestSelectionUpdate, this,
          [this](int idxDyn) {
            if (scrollMgr()) {
              int idx = (idxDyn >= 0) ? idxDyn : currentSelectedIndex();
              if (idx >= 0) {
                scrollMgr()->updateSelectionForIndex(idx);
              }
            }
          });
}

void InteractionManager::connectAttractManagerSignals() {
  // Only item selection changes should reset attract timeout / stop active
  // attract scrolling. Mouse movement/focus churn must not count as activity
  // for attract mode.
  //
  // NOTE (Kartend-b93at): the connections wired here, plus the
  // requestSelectIndex → selectItemByIndex → selectionChanged chain, must stay
  // synchronous (default Direct). AttractManager::advanceSelection clears its
  // isDrivingSelection() guard via a scope guard immediately after emitting, so
  // a Qt::QueuedConnection anywhere in this chain would let selectionChanged
  // fire after the guard cleared — stopping attract mode on its own first tick.
  connect(m_selectionManager.get(), &SelectionManager::selectionChanged, m_attractManager.get(),
          [this](int index) {
            Q_UNUSED(index);
            // Selection changes that AttractManager itself drives via the
            // advance-selection timer must not be treated as user activity,
            // otherwise attract mode would immediately stop on its own tick.
            if (m_attractManager->isDrivingSelection()) {
              // Record that we saw the driven change synchronously — the
              // AttractManager debug-asserts this to enforce the b93at Direct-
              // connection invariant (a queued hop would land here too late).
              m_attractManager->noteDrivenSelectionObserved();
              return;
            }
            m_attractManager->onActivityDetected();
          });
  // A wheel scroll that does NOT move the selection is still browsing, and
  // attract mode exists to yield to browsing. selectionChanged above covers
  // the wheel only when applySelectionDelta actually lands on a new index; it
  // returns early for wheelSteps == 0, which is what fine-grained trackpad
  // and high-resolution wheel deltas produce. The view then scrolls with no
  // selectionChanged, attract never hears about it, and its next advance tick
  // yanks the selection back to its own target — reported 2026-08-19 as
  // "selection reverting on mouse scroll sometimes", and the "sometimes" is
  // exactly which scrolls happened to accumulate a whole step.
  //
  // Safe to treat as activity unconditionally, unlike a scrollbar
  // valueChanged would be: this originates in a real QEvent::Wheel, so
  // attract's own centring (requestSelectIndex -> centerItemVertically) never
  // reaches it and cannot cancel the mode it is currently running.
  connect(m_eventManager.get(), &EventManager::wheelScrollStarted, m_attractManager.get(),
          &AttractManager::onActivityDetected);
  connect(m_attractManager.get(), &AttractManager::requestSelectIndex, this, [this](int index) {
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
                const int totalItems = scrollMgr() ? scrollMgr()->getTotalItems() : 0;
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
