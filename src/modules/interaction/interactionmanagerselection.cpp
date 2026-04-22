// Sibling translation unit for InteractionManager.
// Extracted from interactionmanager.cpp during LOC-reduction refactor.
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

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
#include <algorithm>

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
#include "gridutils.h"
#include "itemwidget.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcInteractionManager().isDebugEnabled()) {                             \
      qCDebug(lcInteractionManager) << msg;                                    \
    }                                                                          \
  } while (0)

void InteractionManager::toggleSearchMode() {
  if (!m_searchManager) {
    return;
  }

  // Delegate to SearchManager
  m_searchManager->toggleSearchMode();

  // Do not reload or change the grid when search text is empty.
  // Only reapply results if the user has entered text.
  if ((m_searchBar) && !m_searchBar->text().trimmed().isEmpty()) {
    m_searchManager->onSearchTextChanged(m_searchBar->text(),
                                         currentSelectedIndex());
  }
}

void InteractionManager::saveCurrentSelection() {
  const int selected = currentSelectedIndex();
  if (selected >= 0) {
    handleSuccessfulSelection(selected);
  }
}

// Updates the search mode button icon/tooltip without coercing the current mode
void InteractionManager::updateSearchModeButton() {
  if (m_searchManager) {
    m_searchManager->updateSearchModeButton();
  }
}

// Updates the search bar placeholder/text style without coercing the current
// mode
void InteractionManager::updateSearchBarPlaceholder() {
  if (m_searchManager) {
    m_searchManager->updateSearchBarPlaceholder();
  }
}

// Restores selection instantly, ensures viewport positioning, and updates
// sidebar metadata
void InteractionManager::beginSelectionRestore(int targetIndex) {
  debugLog(
      "[SelectionRestore] beginSelectionRestore: targetIndex=" << targetIndex);
  if (targetIndex < 0) {
    return;
  }

  // Check if user has made an explicit selection since navigation started -
  // if so, don't override their choice with automatic restore
  if (m_state.selectionRestore().userSelectionMade) {
    debugLog(
        "[SelectionRestore] Skipping restore - user made explicit selection");
    return;
  }

  // Use SelectionManager for preparation
  if (m_selectionManager) {
    m_selectionManager->prepareForRestore(targetIndex);
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(
          m_selectionManager->forceImmediateCenter());
    }
  }

  // Stop any running scroll animations
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
  }

  applySelectionStateForIndex(targetIndex);
  if (m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(
        targetIndex);
  }
  selectItemByIndex(targetIndex, false);

  if (currentSelectedIndex() == targetIndex) {
    finalizeRestoreFlagsAndFocus();
    emit selectionChanged(targetIndex);
  }

  // Finalize restore state
  if (m_selectionManager) {
    m_selectionManager->finalizeRestore();
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(
          m_selectionManager->forceImmediateCenter());
    }
  }

  if ((m_sidebarManager) && m_sidebarManager->isSidebarVisible()) {
    ItemWidget *widget = nullptr;
    if (m_selectionManager) {
      widget = m_selectionManager->widgetForIndex(targetIndex);
    } else if (m_scrollManager) {
      const auto &active = m_scrollManager->getActiveWidgets();
      widget = active.value(targetIndex, nullptr);
    }
    if (widget) {
      m_sidebarManager->updateSidebarMetadata(widget);
    }
    constexpr int kMetadataSidebarUpdateDelayMs = 120;
    scheduleSidebarMetadataUpdateIfVisible(targetIndex, 0,
                                           kMetadataSidebarUpdateDelayMs);
  }
}

void InteractionManager::applySelectionStateForIndex(int idx) {
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(idx);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(idx, subs);
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
    m_scrollManager->updateSelectionForIndex(idx);
  }
}

void InteractionManager::finalizeRestoreFlagsAndFocus() {
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  // Only set focus to items page if search bar doesn't currently have focus
  if ((m_itemsPage) && !m_itemsPage->hasFocus()) {
    if (!m_searchBar || !m_searchBar->hasFocus()) {
      m_itemsPage->setFocus();
    }
  }
  // Clear arrow center suppression after restore completes - ensures the
  // selection is fully visible before allowing subsequent centering operations
  QTimer::singleShot(UIConstants::Keyboard::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS,
                     this, [this]() { m_state.clearArrowCenterSuppression(); });
}

void InteractionManager::scheduleSidebarMetadataUpdateIfVisible(
    int targetIndex, int initialDelayMs, int secondaryDelayMs) {
  QPointer<InteractionManager> guard(this);
  auto schedule = [guard, targetIndex](int delay) {
    // Defer the sidebar metadata refresh so the target ItemWidget has
    // a chance to materialize and populate metadata before we read it.
    QTimer::singleShot(delay, guard, [guard, targetIndex]() {
      if (!guard) {
        return;
      }
      if (!guard->m_sidebarManager || !guard->m_scrollManager) {
        return;
      }
      if (!guard->m_sidebarManager->isSidebarVisible()) {
        return;
      }
      ItemWidget *itemWidget = guard->m_scrollManager->getActiveWidgets().value(
          targetIndex, nullptr);
      if (itemWidget) {
        guard->m_sidebarManager->updateSidebarMetadata(itemWidget);
      }
    });
  };
  schedule(initialDelayMs);
  schedule(secondaryDelayMs);
}

// Handles search text debounce; uses a state flag to avoid refocusing after
// ESC-clears
namespace {
struct ResetClearedFlag {
  InteractionStateHolder *state = nullptr;
  ~ResetClearedFlag() {
    if (state) {
      state->search().clearedByEscape = false;
    }
  }
};
} // namespace

// Schedules repeated attempts plus a layout-complete hook to restore vertical
// scrollbar visibility after clearing search
void InteractionManager::scheduleScrollbarRecovery() {
  if (!m_itemScrollArea || !m_scrollManager || !m_collections ||
      !m_currentCollectionIndex) {
    return;
  }
  int idx = *m_currentCollectionIndex;
  if (!CollectionUtils::isValidIndex(idx, m_collections)) {
    return;
  }
  if ((*m_collections)[idx].hideVerticalScrollbar) {
    return;
  }

  QPointer<InteractionManager> guard(this);

  auto attempt = [guard]() {
    if (!guard) {
      return;
    }
    if (!guard->m_scrollManager || !guard->m_itemScrollArea) {
      return;
    }
    guard->m_scrollManager->recalculateContainerMetrics();
    if (guard->m_viewportManager) {
      guard->m_viewportManager->ensureVerticalScrollbarPolicy();
    }
    QScrollBar *verticalScrollBar =
        guard->m_itemScrollArea->verticalScrollBar();
    if (verticalScrollBar && verticalScrollBar->maximum() > 0) {
      guard->m_itemScrollArea->setVerticalScrollBarPolicy(
          Qt::ScrollBarAsNeeded);
    }
  };

  attempt();
  // Retry scrollbar recovery at increasing intervals - handles race conditions
  // where the scrollbar maximum isn't set immediately after collection load
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_1_MS,
                     this, attempt);
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_2_MS,
                     this, attempt);
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_3_MS,
                     this, attempt);

  if (!m_scrollbarRecoveryConn) {
    m_scrollbarRecoveryConn = QObject::connect(
        m_scrollManager, &ScrollManager::virtualScrollSetupComplete, this,
        [guard]() {
          if (!guard) {
            return;
          }
          if (guard->m_viewportManager) {
            guard->m_viewportManager->ensureVerticalScrollbarPolicy();
          }
          if (guard->m_scrollbarRecoveryConn) {
            QObject::disconnect(guard->m_scrollbarRecoveryConn);
            guard->m_scrollbarRecoveryConn = QMetaObject::Connection();
          }
        });
  }
}

// Initialize search mode for the current collection; reset away from
// AllCollections and prefer collection defaults
void InteractionManager::initializeSearchModeForCurrentCollection() {
  if (m_searchManager) {
    m_searchManager->initializeSearchModeForCurrentCollection();
  }
}

// Launches an item using the collection's configured launcher; expands
// variables without path validation so launch works even if artworkDirectory is
// Delegates to LaunchManager for launching media items
void InteractionManager::launchItemWithCollection(const QString &filePath,
                                                  int collectionIndex) {
  if (m_launchManager) {
    m_launchManager->recordLaunch(filePath);
    m_launchManager->launchItem(filePath, collectionIndex);
  }
}


// Finalizes selection bookkeeping and persists selection; standardizes property
// key for user-free-scroll
void InteractionManager::handleSuccessfulSelection(int index) {
  m_state.scroll().userFreeScroll = false;

  bool restoringMatch = false;
  if (m_selectionManager) {
    restoringMatch = m_selectionManager->checkAndFinalizeRestore(index);
  }

  if ((m_isShuttingDown) && *m_isShuttingDown) {
    return;
  }

  int currentColl =
      ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if ((m_collections) && currentColl >= 0 && index >= 0) {
    persistSelectionForIndex(currentColl, index);
  }
  if (QApplication::closingDown()) {
    return;
  }

  bool immediate =
      (m_viewportManager && m_viewportManager->forceImmediateCenter()) ||
      restoringMatch;
  centerItemVertically(index, immediate);
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
  }
}

auto InteractionManager::titleForIndexInColl(int coll, int idx) const
    -> QString {
  // For the *current* collection consult the rendered scroll data so the
  // discriminator matches what the user sees during search (when the visible
  // sub list is filtered). Other collections fall back to the hierarchy cache.
  const bool isCurrent =
      m_currentCollectionIndex && coll == *m_currentCollectionIndex;
  if (isCurrent && m_scrollManager) {
    const int actualIdx = m_scrollManager->getFilteredIndex(idx);
    const int renderedSubCount = m_scrollManager->getSubcollectionCount();
    if (actualIdx >= 0 && actualIdx < renderedSubCount) {
      int subIdx = m_scrollManager->getDataManager()
                       ? m_scrollManager->getDataManager()
                             ->subcollectionIndexFromActual(actualIdx)
                       : -1;
      if (m_collections && subIdx >= 0 && subIdx < m_collections->size()) {
        return (*m_collections)[subIdx].name;
      }
      return {};
    }
  } else {
    QList<int> subs = getSubcollections(coll);
    if (idx >= 0 && idx < subs.size()) {
      int subIdx = subs[idx];
      if (m_collections && subIdx >= 0 && subIdx < m_collections->size()) {
        return (*m_collections)[subIdx].name;
      }
      return {};
    }
  }
  QString path =
      m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (m_scrollManager)) {
    path = m_scrollManager->filePathForVisualIndex(idx);
  }
  if (!path.isEmpty()) {
    return QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  return {};
}

void InteractionManager::persistSelectionForIndex(int coll, int idx) {
  if (!m_settingsManager ||
      !CollectionUtils::isValidIndex(coll, m_collections)) {
    return;
  }
  m_settingsManager->setLastSelectedItem(coll, idx);
  // Use a stable session key that also scopes by virtual subfolder (if active).
  QString sessionKey = CollectionUtils::selectionSessionKeyFor(
      (*m_collections)[coll], *m_collections);
  QString title;
  QString path =
      m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (m_scrollManager)) {
    path = m_scrollManager->filePathForVisualIndex(idx);
  }
  if (!path.isEmpty()) {
    title = QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  if (m_sessionManager) {
    m_sessionManager->setLastSelected(sessionKey, idx, title);
  }
  // Defer artwork update to allow UI state to settle after selection save
  QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && m_artworkManager) {
      m_artworkManager->updateViewportArtwork();
    }
  });
}

void InteractionManager::cancelPendingSelectionRestore() {
  if (m_selectionManager) {
    m_selectionManager->cancelPendingSelectionRestore();
  }
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
}

void InteractionManager::resetSelectionRestoreState() {
  if (m_selectionManager) {
    m_selectionManager->resetSelectionRestoreState();
  }
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
}

void InteractionManager::stopScrollAnimations() {
  // Prevent stale scroll animations from applying after a view rebuild
  // (e.g., entering a virtual subfolder while wheel scrolling is still
  // animating).
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    AnimationManager::stopArrowKeyAnimationIfRunning(
        m_itemScrollArea->verticalScrollBar());
  }
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
  }
}
