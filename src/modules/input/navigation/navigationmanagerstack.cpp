// Navigation stack + back-navigation methods for NavigationManager.
// Extracted from navigationmanager.cpp during LOC-reduction refactor.
// These remain NavigationManager members; this is a translation-unit split.
#include "navigationmanager.h"

#include "applicationcontext.h"
#include "artworkmanager.h"
#include "detailspane.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "navigationhelpers.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "settingsmanager.h"
#include "timerutils.h"
#include "uiconstants.h"

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
  if (m_interactionManager) {
    m_interactionManager->clearSelectionAndFocus();
  }
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }
  m_artworkManager->stopSilentLoading();
  if (m_artworkManager->getTimerCoordinator()) {
    m_artworkManager->getTimerCoordinator()->stopAllTimers();
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
  // nested timer handles selection restoration after layout settles
  QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this,
                     [this, targetCollectionIndex, subcollectionVisualIndex]() {
                       showCollectionItems(targetCollectionIndex);

                       if (m_interactionManager) {
                         m_interactionManager->setNavigationInProgress(false);
                       }

                       if (subcollectionVisualIndex >= 0 && m_interactionManager) {
                         // Delay selection restore until layout is stable after navigation
                         QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this,
                                            [this, subcollectionVisualIndex]() {
                                              if (m_interactionManager) {
                                                m_interactionManager->beginSelectionRestore(
                                                    subcollectionVisualIndex);
                                              }
                                            });
                       }
                     });
}

// Handles navigation when the navigation stack is not empty
auto NavigationManager::handleNavigationStackPop() -> void {
  int targetCollectionIndex = m_stackManager->pop();
  int previousIndex = (*m_currentCollectionIndex);

  performNavigationStackCleanup();

  bool shared = areItemsShared(previousIndex, targetCollectionIndex);
  if (m_scrollManager) {
    if (shared) {
      m_scrollManager->cleanupActiveWidgets();
    } else {
      m_scrollManager->cleanup();
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
  if (m_generalSettings && m_generalSettings->useHomeView && previousIndex >= 0 &&
      previousIndex < (*m_collections).size() &&
      (*m_collections)[previousIndex].parentCollectionIndex == -1) {
    loadRootView();
    return;
  }

  const int fallbackIndex =
      NavigationHelpers::chooseFallbackCollectionIndex((*m_currentCollectionIndex), *m_collections);

  if (fallbackIndex >= 0) {
    bool shared = areItemsShared(previousIndex, fallbackIndex);
    if (m_scrollManager) {
      if (shared) {
        m_scrollManager->cleanupActiveWidgets();
      } else {
        m_scrollManager->cleanup();
      }
    }
    showCollectionItems(fallbackIndex);
  }

  if (m_interactionManager) {
    m_interactionManager->setNavigationInProgress(false);
  }
}

// Goes back to collections and cancels any active held-key repeats to avoid
// stray timer callbacks
void NavigationManager::goBackToCollections() {
  if (!parent()) {
    return;
  }
  // Synthetic Home view is the top of the navigation hierarchy — Back is a
  // no-op here so the user isn't kicked back into a single root collection.
  if (m_inRootView) {
    return;
  }
  if ((parent()) && (m_interactionManager)) {
    m_interactionManager->stopRepeat();
  }

  persistCurrentSelection();

  if (!m_stackManager->isEmpty()) {
    handleNavigationStackPop();
  } else {
    handleNavigationFallback();
  }
}
