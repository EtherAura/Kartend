// Sibling translation unit for InteractionManager.
// Extracted from interactionmanager.cpp during LOC-reduction refactor.
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

#include "collection/launcherconfig.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include "alphabeticnavigationhandler.h"
#include "animationmanager.h"
#include "arrownavigationhandler.h"
#include "attractmanager.h"
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "ikeyboardmanager.h"
#include "imousemanager.h"
#include "launchmanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "collection/hierarchyhelpers.h"
#include "collection/validationhelpers.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "idetailspane.h"
#include "idetailspanemanager.h"
#include "inavigationmanager.h"
#include "isessionmanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/keyboard.h"
#include "uiconstants/navigation.h"
#include "uiconstants/timing.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcInteractionManager().isDebugEnabled()) {                                                 \
      qCDebug(lcInteractionManager) << msg;                                                        \
    }                                                                                              \
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
    m_searchManager->onSearchTextChanged(m_searchBar->text(), currentSelectedIndex());
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
  debugLog("[SelectionRestore] beginSelectionRestore: targetIndex=" << targetIndex);
  if (targetIndex < 0) {
    return;
  }

  // Check if user has made an explicit selection since navigation started -
  // if so, don't override their choice with automatic restore
  if (m_state.selectionRestore().userSelectionMade) {
    debugLog("[SelectionRestore] Skipping restore - user made explicit selection");
    return;
  }

  // Use SelectionManager for preparation
  if (m_selectionManager) {
    m_selectionManager->prepareForRestore(targetIndex);
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(m_selectionManager->forceImmediateCenter());
    }
  }

  // Stop any running scroll animations
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
  }

  applySelectionStateForIndex(targetIndex);
  if (m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(targetIndex);
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
      m_viewportManager->setForceImmediateCenter(m_selectionManager->forceImmediateCenter());
    }
  }

  if ((detailsPaneMgr()) && detailsPaneMgr()->isSidebarVisible()) {
    ItemWidget *widget = nullptr;
    if (m_selectionManager) {
      widget = m_selectionManager->widgetForIndex(targetIndex);
    } else if (scrollMgr()) {
      const auto &active = scrollMgr()->getActiveWidgets();
      widget = active.value(targetIndex, nullptr);
    }
    if (widget) {
      detailsPaneMgr()->updateSidebarMetadata(widget);
    }
    constexpr int kMetadataSidebarUpdateDelayMs = 120;
    scheduleSidebarMetadataUpdateIfVisible(targetIndex, 0, kMetadataSidebarUpdateDelayMs);
  }
}

void InteractionManager::applySelectionStateForIndex(int idx) {
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(idx);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(idx, subs);
  if (scrollMgr()) {
    scrollMgr()->updateVirtualView();
    scrollMgr()->updateSelectionForIndex(idx);
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
  QTimer::singleShot(UIConstants::Keyboard::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS, this,
                     [this]() { m_state.clearArrowCenterSuppression(); });
}

void InteractionManager::scheduleSidebarMetadataUpdateIfVisible(int targetIndex, int initialDelayMs,
                                                                int secondaryDelayMs) {
  QPointer<InteractionManager> guard(this);
  auto schedule = [guard, targetIndex](int delay) {
    // Defer the sidebar metadata refresh so the target ItemWidget has
    // a chance to materialize and populate metadata before we read it.
    QTimer::singleShot(delay, guard, [guard, targetIndex]() {
      if (!guard) {
        return;
      }
      if (!guard->detailsPaneMgr() || !guard->scrollMgr()) {
        return;
      }
      if (!guard->detailsPaneMgr()->isSidebarVisible()) {
        return;
      }
      ItemWidget *itemWidget = guard->scrollMgr()->getActiveWidgets().value(targetIndex, nullptr);
      if (itemWidget) {
        guard->detailsPaneMgr()->updateSidebarMetadata(itemWidget);
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
  if (!m_itemScrollArea || !scrollMgr() || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  int idx = *m_currentCollectionIndex;
  if (!CollectionUtils::isValidIndex(idx, m_collections)) {
    return;
  }
  if ((*m_collections)[idx].gridLayout.hideVerticalScrollbar) {
    return;
  }

  QPointer<InteractionManager> guard(this);

  auto attempt = [guard]() {
    if (!guard) {
      return;
    }
    if (!guard->scrollMgr() || !guard->m_itemScrollArea) {
      return;
    }
    guard->scrollMgr()->recalculateContainerMetrics();
    if (guard->m_viewportManager) {
      guard->m_viewportManager->ensureVerticalScrollbarPolicy();
    }
    QScrollBar *verticalScrollBar = guard->m_itemScrollArea->verticalScrollBar();
    if (verticalScrollBar && verticalScrollBar->maximum() > 0) {
      guard->m_itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
  };

  attempt();
  // Three staggered retries handle a race: QScrollArea sometimes finishes
  // virtual setup AFTER the synchronous call above. ATTEMPT_1 catches the
  // common case; ATTEMPT_2 / ATTEMPT_3 cover progressively slower collection
  // loads (populous libraries on cold caches). The virtualScrollSetupComplete
  // signal below is the deterministic path — these timers are a belt for the
  // case where the signal fires while we're still mid-construction.
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_1_MS, this, attempt);
  // Mid-delay retry — see above.
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_2_MS, this, attempt);
  // Longest-delay retry — see above.
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_3_MS, this, attempt);

  if (!m_scrollbarRecoveryConn) {
    m_scrollbarRecoveryConn =
        QObject::connect(scrollMgr(), &ScrollManager::virtualScrollSetupComplete, this, [guard]() {
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
void InteractionManager::launchItemWithCollection(const QString &filePath, int collectionIndex) {
  if (!m_launchManager) {
    return;
  }
  // Kartend-l06g6: debounce EVERY launch surface here, not just mouse
  // double-click — keyboard Enter, gamepad Confirm, context-menu Launch,
  // cover-flow activation, and the detail page all funnel through this
  // method, and two quick presses (bouncy Enter key, gamepad chatter)
  // previously spawned two child processes.
  if (!filePath.isEmpty() && !m_launchManager->canLaunch(filePath)) {
    return;
  }
  // Kartend-w2n0: gate Enter/launch when the collection's launcher profile
  // has unresolvable paths. Replaces the silent "launch fails 200ms later
  // with a QMessageBox from validateLauncherPath" surprise with an upfront
  // explanation pointing the user to Settings → Launchers. Hard gate per
  // bd choice; users who hit a false-negative from checkLauncherPath can
  // still fix the path or temporarily clear it.
  if (m_collections && collectionIndex >= 0 && collectionIndex < m_collections->size()) {
    // Launch-time overload: the gate must judge exactly what launchItem will
    // execute. Raw inline fields hard-blocked a %collection%-placeholder path
    // that expands fine at build time, and skipped preset-backed entries
    // (inline path empty) whose stored preset path is broken — letting those
    // launches fail moments later anyway.
    const CollectionConfig &collection = (*m_collections)[collectionIndex];
    const QStringList issues = LauncherUtils::launcherPathIssues(
        collection.launcher,
        m_generalSettings ? m_generalSettings->launchers.launcherPresets : QList<LauncherPreset>{},
        collection.name);
    if (!issues.isEmpty()) {
      QString body = tr("This collection's launcher paths are unresolvable on this host:");
      for (const QString &issue : issues) {
        body += QLatin1String("\n• ") + issue;
      }
      body += QLatin1Char('\n');
      body += tr("Fix the paths in Settings → Launchers, then try again.");
      // Kartend-sqoq0: owner-supplied runner when wired; stock QMessageBox
      // fallback otherwise (headless tests stub the runner instead).
      if (m_dialogs.warn) {
        m_dialogs.warn(tr("Launcher unavailable"), body);
      } else {
        QMessageBox::warning(QApplication::activeWindow(), tr("Launcher unavailable"), body);
      }
      return;
    }
  }
  // Treat the launch itself as user activity so attract mode stops
  // immediately. Without this, attract keeps advancing the selection during
  // the gap between Enter-press and runtimeStarted (which only fires once
  // QProcess::started arrives — a noticeable lag for video players), and
  // never stops at all for detached launches.
  if (m_attractManager) {
    m_attractManager->onActivityDetected();
  }
  // Stamp the debounce window only once we actually proceed — stamping on a
  // validation-rejected launch used to block a corrective retry for 500ms.
  m_launchManager->recordLaunch(filePath);
  m_launchManager->launchItem(filePath, collectionIndex);
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

  int currentColl = ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if ((m_collections) && currentColl >= 0 && index >= 0) {
    persistSelectionForIndex(currentColl, index);
  }
  if (QApplication::closingDown()) {
    return;
  }

  bool immediate =
      (m_viewportManager && m_viewportManager->forceImmediateCenter()) || restoringMatch;
  centerItemVertically(index, immediate);
  if (scrollMgr()) {
    scrollMgr()->updateVirtualView();
  }
}

auto InteractionManager::titleForIndexInColl(int coll, int idx) const -> QString {
  // For the *current* collection consult the rendered scroll data so the
  // discriminator matches what the user sees during search (when the visible
  // sub list is filtered). Other collections fall back to the hierarchy cache.
  const bool isCurrent = m_currentCollectionIndex && coll == *m_currentCollectionIndex;
  if (isCurrent && scrollMgr()) {
    const int actualIdx = scrollMgr()->getFilteredIndex(idx);
    const int renderedSubCount = scrollMgr()->getSubcollectionCount();
    if (actualIdx >= 0 && actualIdx < renderedSubCount) {
      int subIdx = scrollMgr()->subcollectionIndexFromActual(actualIdx);
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
  QString path = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (scrollMgr())) {
    path = scrollMgr()->filePathForVisualIndex(idx);
  }
  if (!path.isEmpty()) {
    return QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  return {};
}

void InteractionManager::persistSelectionForIndex(int coll, int idx) {
  if (!settingsMgr() || !CollectionUtils::isValidIndex(coll, m_collections)) {
    return;
  }
  settingsMgr()->setLastSelectedItem(coll, idx);
  // Use a stable session key that also scopes by virtual subfolder (if active).
  QString sessionKey =
      CollectionUtils::selectionSessionKeyFor((*m_collections)[coll], *m_collections);
  QString title;
  QString path = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (scrollMgr())) {
    path = scrollMgr()->filePathForVisualIndex(idx);
  }
  if (!path.isEmpty()) {
    title = QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  if (sessionMgr()) {
    sessionMgr()->setLastSelected(sessionKey, idx, title);
  }
  // Defer artwork update to allow UI state to settle after selection save
  QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && artworkMgr()) {
      artworkMgr()->updateViewportArtwork();
    }
  });
}

void InteractionManager::cancelPendingSelectionRestore() {
  if (m_selectionManager) {
    // SelectionManager owns the canonical write path and updates
    // m_state.selectionRestore() directly.
    m_selectionManager->cancelPendingSelectionRestore();
    return;
  }
  // Fallback: SelectionManager not wired (e.g. shutdown teardown). Still
  // advance the canonical token so any in-flight restore observer sees
  // cancellation.
  auto &restoreState = m_state.selectionRestore();
  ++restoreState.restoreToken;
  restoreState.restorePending = false;
  restoreState.userSelectionMade = true;
}

void InteractionManager::resetSelectionRestoreState() {
  if (m_selectionManager) {
    m_selectionManager->resetSelectionRestoreState();
    return;
  }
  // Fallback: see cancelPendingSelectionRestore() above. resetSelectionRestoreState
  // does NOT set userSelectionMade — the new navigation should be free to
  // auto-restore.
  auto &restoreState = m_state.selectionRestore();
  ++restoreState.restoreToken;
  restoreState.restorePending = false;
  restoreState.userSelectionMade = false;
}

void InteractionManager::stopScrollAnimations() {
  // Prevent stale scroll animations from applying after a view rebuild
  // (e.g., entering a virtual subfolder while wheel scrolling is still
  // animating).
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    AnimationManager::stopArrowKeyAnimationIfRunning(m_itemScrollArea->verticalScrollBar());
  }
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
  }
}
