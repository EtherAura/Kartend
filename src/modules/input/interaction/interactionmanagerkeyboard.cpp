// Sibling translation unit for InteractionManager.
// Extracted from interactionmanager.cpp during LOC-reduction refactor.
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include "alphabeticnavigationhandler.h"
#include "animationmanager.h"
#include "arrownavigationhandler.h"
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "mousemanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailpagemanager.h"
#include "detailpageoverlay.h"
#include "detailspane.h"
#include "detailspanemanager.h"
#include "gridutils.h"
#include "itemwidget.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcInteractionManager().isDebugEnabled()) {                                                 \
      qCDebug(lcInteractionManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

void InteractionManager::handleArrowKeyNavigation(int direction, bool vertical) {
  bool restoringSelection = m_selectionManager && m_selectionManager->isRestoringSelection();
  if (restoringSelection || m_navigationInProgress) {
    return;
  }
  if (m_arrowHandler) {
    m_arrowHandler->handleArrowKeyNavigation(direction, vertical);
  }
}

// KeyboardManager callback: handles alphabetic navigation via PageUp/PageDown
void InteractionManager::handleAlphabeticNavigation(bool forward) {
  bool restoringSelection = m_selectionManager && m_selectionManager->isRestoringSelection();
  if (restoringSelection || m_navigationInProgress) {
    return;
  }
  if (m_alphabeticHandler) {
    (void)m_alphabeticHandler->navigateToNextLetter(forward);
  }
}

// KeyboardManager callback: handles Home/End key to jump to first/last item
void InteractionManager::handleJumpToEdge(bool toEnd) {
  bool restoringSelection = m_selectionManager && m_selectionManager->isRestoringSelection();
  if (restoringSelection || m_navigationInProgress) {
    return;
  }
  if (!scrollMgr()) {
    return;
  }

  const int totalItems = scrollMgr()->getTotalItems();
  if (totalItems <= 0) {
    return;
  }

  // Stop any ongoing scroll animation
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
  }

  // Clear stale state that could interfere with immediate jump
  m_state.click().deferCenterOnClick = false;
  m_state.click().deferredCenterIndex = -1;
  m_state.click().selectionSuppressed = false;
  m_state.click().pendingSelectionIndex = -1;
  m_state.scroll().userScrollActive = false;
  if (m_viewportManager) {
    m_viewportManager->setWrapSequenceActive(false);
    m_viewportManager->setContinuousScrollActive(false);
    m_viewportManager->setForceImmediateCenter(true);
  }

  // Calculate target index: 0 for Home, last item for End
  const int targetIndex = toEnd ? (totalItems - 1) : 0;
  const int currentIndex = currentSelectedIndex();

  if (targetIndex == currentIndex) {
    // Already at edge - just ensure it's centered
    if (m_viewportManager) {
      m_viewportManager->centerItemVertically(targetIndex, true);
      m_viewportManager->setForceImmediateCenter(false);
    }
    return;
  }

  // Set selection directly without triggering additional centering logic
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(targetIndex);
  }

  // Center immediately on the target
  if (m_viewportManager) {
    m_viewportManager->centerItemVertically(targetIndex, true);
    m_viewportManager->setForceImmediateCenter(false);
  }

  // Update virtual view and selection overlay
  if (scrollMgr()) {
    scrollMgr()->updateVirtualView();
    scrollMgr()->updateSelectionForIndex(targetIndex);
  }

  emit selectionChanged(targetIndex);
}

// KeyboardManager callback: handles repeat step during key hold
void InteractionManager::onKeyboardRepeatStep() {
  if (m_arrowHandler) {
    m_arrowHandler->handleRepeatStep();
  }
}

// KeyboardManager callback: handles cleanup when key hold stops
void InteractionManager::onKeyboardStopRepeat(bool suppressRecentering) {
  if (m_destroying) {
    return;
  }
  if (m_arrowHandler) {
    m_arrowHandler->handleStopRepeat(suppressRecentering);
  }
}

// Handles global key presses; ESC clears search, navigates to parent only if
// subcollection, otherwise does nothing
auto InteractionManager::handleGlobalKeyPress(QKeyEvent *event) -> bool {
  if (!event) {
    return false;
  }

  const int searchKey =
      m_generalSettings ? m_generalSettings->keySearch : static_cast<int>(Qt::Key_Slash);
  const int backKey =
      m_generalSettings ? m_generalSettings->keyBack : static_cast<int>(Qt::Key_Escape);

  if (event->key() == searchKey) {
    return handleSlashKey();
  }
  if (event->key() == backKey) {
    return handleEscapeKey();
  }

  if (m_generalSettings && m_generalSettings->useHomeView && m_generalSettings->keyHomeView != 0 &&
      event->key() == m_generalSettings->keyHomeView) {
    if (m_searchBar && m_searchBar->hasFocus()) {
      return false;
    }
    if (navMgr()) {
      navMgr()->loadRootView();
      return true;
    }
  }
  return false;
}

// Focuses the search bar and selects all text when '/' is pressed.
// If the search bar already has focus and is empty, toggles search mode
// instead.
auto InteractionManager::handleSlashKey() -> bool {
  if (!m_searchBar || !m_searchBar->isVisible()) {
    return false;
  }

  if (m_searchBar->hasFocus() && m_searchBar->text().trimmed().isEmpty()) {
    toggleSearchMode();
    return true;
  }

  m_searchBar->setFocus(Qt::ShortcutFocusReason);
  m_searchBar->selectAll();
  return true;
}

// Clears search if non-empty (keeping focus), otherwise navigates to parent
// collection when possible. If search bar is focused and empty, returns focus
// to the grid.
auto InteractionManager::handleEscapeKey() -> bool {
  // First check if artwork preview overlay is open - close it and return
  if (scrollMgr() && scrollMgr()->hideArtworkPreview()) {
    // Also clear expand-mode state so the next activation expands again.
    m_state.clearExpandedItem();
    return true;
  }
  // Then the detail-page overlay (info mode). Its own keyPressEvent would
  // handle Escape, but the app-wide event filter consumes the key first via
  // KeyboardManager → requestEscapeAction; route the dismiss here so the
  // overlay closes regardless of which path the key takes.
  if (detailPageMgr()) {
    if (auto *overlay = detailPageMgr()->overlay()) {
      if (overlay->isActive()) {
        detailPageMgr()->hideOverlay();
        return true;
      }
    }
  }

  const QString current = (m_searchBar ? m_searchBar->text().trimmed() : QString());
  const bool searchBarFocused = (m_searchBar && m_searchBar->hasFocus());

  if (!current.isEmpty()) {
    if (m_searchBar) {
      m_state.search().clearedByEscape = true;
      m_searchBar->clear();
      // Keep focus on search bar when clearing text
    }
    return true;
  }

  // If search bar is focused but empty, return focus to grid
  if (searchBarFocused) {
    if (m_gridContainer) {
      m_gridContainer->setFocus(Qt::OtherFocusReason);
    }
    return true;
  }

  const int collIndex = (m_currentCollectionIndex ? *m_currentCollectionIndex : -1);
  if (m_collections && collIndex >= 0 && collIndex < m_collections->size()) {
    const CollectionConfig &cfg = (*m_collections)[collIndex];

    // First check if we're in a virtual subfolder - go back one level
    if (!cfg.currentSubfolder.isEmpty()) {
      if (navMgr()) {
        navMgr()->goBackFromVirtualFolder();
      }
      return true;
    }

    // Then check if we're in a subcollection - go to parent
    if (cfg.isSubcollection && cfg.parentCollectionIndex >= 0 &&
        cfg.parentCollectionIndex < m_collections->size()) {
      if (navMgr()) {
        constexpr int kRestoreAttempts = UIConstants::Selection::RESTORE_STEPS;
        constexpr int kRestoreIntervalMs = UIConstants::Selection::RESTORE_STEP_DELAY_MS;
        constexpr int kRestoreTimeoutMs = UIConstants::Selection::RESTORE_MAX_DELAY_MS;
        const int parent = cfg.parentCollectionIndex;
        navMgr()->showCollectionItems(parent);
        int sel = -1;
        if (settingsMgr()) {
          sel = settingsMgr()->getLastSelectedItem(parent);
        }
        if (sel >= 0) {
          navMgr()->scheduleSelectionRestore(sel, kRestoreAttempts, kRestoreIntervalMs,
                                                        kRestoreTimeoutMs);
        }
      }
    } else {
      return true;
    }
    return true;
  }
  return true;
}

void InteractionManager::handleImmediateSearchTextChanged(const QString &text) {
  updateSearchBarPlaceholder();
  if (m_searchManager) {
    m_searchManager->onSearchTextChanged(text, currentSelectedIndex());
  }
}
