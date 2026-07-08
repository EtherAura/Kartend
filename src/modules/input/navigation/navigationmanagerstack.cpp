// Navigation stack + back-navigation methods for NavigationManager.
// Extracted from navigationmanager.cpp during LOC-reduction refactor.
// These remain NavigationManager members; this is a translation-unit split.
#include "navigationmanager.h"

#include "applicationcontext.h"
#include "iartworkmanager.h"
#include "idetailspane.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "isettingsmanager.h"
#include "navigationhelpers.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "timerutils.h"
#include "uiconstants/selection.h"
#include "uiconstants/timing.h"

#include <QApplication>
#include <QLoggingCategory>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QtGlobal>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcNavigationManager().isDebugEnabled()) {                                                  \
      qCDebug(lcNavigationManager) << msg;                                                         \
    }                                                                                              \
  } while (0)

auto NavigationManager::performNavigationStackCleanup() -> void {
  if (interactionMgr()) {
    interactionMgr()->clearSelectionAndFocus();
  }
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }
  if (auto *art = artworkMgr()) {
    art->stopSilentLoading();
    if (art->getTimerCoordinator()) {
      art->getTimerCoordinator()->stopAllTimers();
    }
  }
}

// Finds the visual index of a subcollection within its parent
auto NavigationManager::findSubcollectionVisualIndex(int targetCollectionIndex,
                                                     int previousIndex) const -> int {
  return NavigationHelpers::findSubcollectionVisualIndex(targetCollectionIndex, previousIndex,
                                                         *m_collections);
}

// Schedules the navigation return with proper timing and selection restoration
auto NavigationManager::scheduleNavigationReturn(int targetCollectionIndex,
                                                 int subcollectionVisualIndex) -> void {
  // Delay navigation return to allow current animations to complete -
  // selection restore is handed to the tokenized coordinator after the switch
  QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this,
                     [this, targetCollectionIndex, subcollectionVisualIndex]() {
                       if (!showCollectionItems(targetCollectionIndex)) {
                         // The pop already ran cleanup synchronously, so a target that fails
                         // validation (e.g. collection removed while browsing) would strand
                         // a blank items view — route to the validated fallback instead.
                         handleNavigationFallback();
                         return;
                       }

                       if (interactionMgr()) {
                         interactionMgr()->setNavigationInProgress(false);
                       }

                       if (subcollectionVisualIndex >= 0) {
                         // Route through SelectionRestoreCoordinator rather than a bare
                         // delayed beginSelectionRestore: its token + scheduled-collection
                         // validation cancels this restore if the user navigates again (or a
                         // rescan reloads the view) before it fires, instead of applying a
                         // stale index to whatever collection is current by then.
                         scheduleSelectionRestore(subcollectionVisualIndex,
                                                  UIConstants::Selection::RESTORE_MAX_DELAY_MS);
                       }
                     });
}

// Handles navigation when the navigation stack is not empty
auto NavigationManager::handleNavigationStackPop() -> void {
  int targetCollectionIndex = m_stackManager->pop();
  int previousIndex = (*m_currentCollectionIndex);

  performNavigationStackCleanup();

  bool shared = areItemsShared(previousIndex, targetCollectionIndex);
  if (scrollMgr()) {
    if (shared) {
      scrollMgr()->cleanupActiveWidgets();
    } else {
      scrollMgr()->cleanup();
    }
  }

  int subcollectionVisualIndex = findSubcollectionVisualIndex(targetCollectionIndex, previousIndex);
  scheduleNavigationReturn(targetCollectionIndex, subcollectionVisualIndex);
}

// Handles navigation fallback when the navigation stack is empty
auto NavigationManager::handleNavigationFallback() -> void {
  m_stackManager->clear(); // Ensure depth is reset
  int previousIndex = (*m_currentCollectionIndex);

  // Home-view escape: if the user opted into the synthetic root view, an
  // empty stack from a parent==-1 collection routes back to home rather than
  // re-entering the first root collection.
  if (m_generalSettings && m_generalSettings->startup.useHomeView && previousIndex >= 0 &&
      previousIndex < (*m_collections).size() &&
      (*m_collections)[previousIndex].parentCollectionIndex == -1) {
    loadRootView();
    return;
  }

  const int fallbackIndex =
      NavigationHelpers::chooseFallbackCollectionIndex((*m_currentCollectionIndex), *m_collections);

  if (fallbackIndex >= 0) {
    bool shared = areItemsShared(previousIndex, fallbackIndex);
    if (scrollMgr()) {
      if (shared) {
        scrollMgr()->cleanupActiveWidgets();
      } else {
        scrollMgr()->cleanup();
      }
    }
    showCollectionItems(fallbackIndex);
  }

  if (interactionMgr()) {
    interactionMgr()->setNavigationInProgress(false);
  }
}

// Goes back to collections and cancels any active held-key repeats to avoid
// stray timer callbacks
void NavigationManager::goBackToCollections() {
  if (!appNotShuttingDown()) {
    return;
  }
  // Synthetic Home view is the top of the navigation hierarchy — Back is a
  // no-op here so the user isn't kicked back into a single root collection.
  if (m_inRootView) {
    return;
  }
  if (appNotShuttingDown() && interactionMgr()) {
    interactionMgr()->stopRepeat();
  }

  persistCurrentSelection();

  if (!m_stackManager->isEmpty()) {
    handleNavigationStackPop();
  } else {
    handleNavigationFallback();
  }
}
