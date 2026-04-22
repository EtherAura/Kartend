// Routing/dispatch cluster split out from navigationmanager.cpp.
#include "navigationmanager.h"
#include "loggingcategories.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "loadingoverlay.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QStackedWidget>
#include <QtGlobal>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcNavigationManager().isDebugEnabled()) {                              \
      qCDebug(lcNavigationManager) << msg;                                     \
    }                                                                          \
  } while (0)

auto NavigationManager::validateAndPrepareNavigation(int collectionIndex)
    -> bool {
  if (collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return false;
  }

  // Update loading overlay with collection name for better UX
  if (m_loadingOverlay && m_collections) {
    const QString &collectionName = (*m_collections)[collectionIndex].name;
    m_loadingOverlay->setMessage(QString("Loading %1...").arg(collectionName));
  }

  bool hasSub = false;
  bool hasItems = false;
  if (!getHasSubAndItems(collectionIndex, hasSub, hasItems)) {
    if ((parent()) && (m_interactionManager)) {
      // Clear navigation progress flag early when validation fails -
      // prevents navigation from being blocked indefinitely
      QTimer::singleShot(
          UIConstants::Navigation::PROGRESS_CLEAR_EARLY_MS, this, [this]() {
            if (parent() && m_interactionManager) {
              m_interactionManager->setNavigationInProgress(false);
            }
          });
    }
    return false;
  }
  return true;
}

auto NavigationManager::handleSubcollectionNavigation(int collectionIndex,
                                                      int previousIndex)
    -> void {
  Q_UNUSED(previousIndex)
  if (m_scrollManager) {
    m_scrollManager->updateContextForSubcollection(collectionIndex);
    m_scrollManager->applySubcollectionFilter(collectionIndex);
  }

  // Delegate selection restore to SelectionRestoreManager
  if (m_selectionRestoreManager) {
    m_selectionRestoreManager->handleSubcollectionRestore(collectionIndex);
  }
}

auto NavigationManager::handleRegularNavigation(int collectionIndex) -> void {
  if (m_scrollManager) {
    m_scrollManager->clearFilter();
  }
}

auto NavigationManager::finalizeNavigation(int collectionIndex) -> void {
  applyCollectionSettingsOnly(collectionIndex);
  if ((m_searchBar) && m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }
  // Delay horizontal centering until layout has settled after navigation
  QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this, [this]() {
    if (m_scrollManager && m_currentCollectionIndex && m_collections) {
      m_scrollManager->centerHorizontalScrollbar((*m_currentCollectionIndex),
                                                 (*m_collections));
    }
  });
  if (m_refreshTitleCounts)
    m_refreshTitleCounts();

  // Hide loading overlay after navigation completes
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }

  // Clear navigation progress flag after all animations complete -
  // allows user input to be processed again
  QTimer::singleShot(UIConstants::Navigation::PROGRESS_CLEAR_MS, this,
                     [this]() {
                       if (parent() && m_interactionManager) {
                         m_interactionManager->setNavigationInProgress(false);
                       }
                     });
}

// Delegates to SelectionRestoreManager for selection restoration
auto NavigationManager::scheduleSelectionRestore(int desiredIndex,
                                                 int maxAttempts,
                                                 int attemptDelayMs,
                                                 int finalEnsureDelayMs)
    -> void {
  if (m_selectionRestoreManager) {
    m_selectionRestoreManager->scheduleSelectionRestore(
        desiredIndex, maxAttempts, attemptDelayMs, finalEnsureDelayMs);
  }
}

// Validates collection index for showCollectionItems operation
auto NavigationManager::validateCollectionIndex(int collectionIndex) const
    -> bool {
  if (!parent() || !m_collections) {
    return false;
  }
  if (collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return false;
  }

  bool hasSub = false;
  bool hasItems = false;
  return getHasSubAndItems(collectionIndex, hasSub, hasItems);
}

// Handles navigation when items are shared between collections
auto NavigationManager::handleSharedItemsNavigation(int collectionIndex)
    -> bool {
  bool itemsAreShared =
      areItemsShared((*m_currentCollectionIndex), collectionIndex);
  if (itemsAreShared) {
    navigateWithSharedItems(collectionIndex);
    if (m_refreshTitleCounts)
      m_refreshTitleCounts();
    return true;
  }
  return false;
}

// Prepares UI for non-shared navigation
auto NavigationManager::prepareNonSharedNavigation(int collectionIndex)
    -> void {
  if (!m_collections || !m_currentCollectionIndex) {
    return;
  }

  prepareForNonSharedNavigationHelper();
  suspendItemsPageRendering();

  (*m_currentCollectionIndex) = collectionIndex;

  applyUiPoliciesForCollection(collectionIndex);
  applyCollectionSettingsOnly(collectionIndex);

  if (m_scrollManager) {
    m_scrollManager->primeLayoutFor((*m_collections)[collectionIndex]);
  }
  if (m_interactionManager) {
    m_interactionManager->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);
  m_stackedWidget->setCurrentWidget(m_itemsPage);
  if (m_itemsPage && m_itemsPage->window()) {
    m_itemsPage->window()->setFocus();
    m_itemsPage->window()->activateWindow();
  }

  if (m_sidebarManager) {
    m_sidebarManager->applySidebarStateForCollection(
        (*m_currentCollectionIndex));
  }

  if ((m_searchBar) && m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }

  if (m_scrollManager) {
    m_scrollManager->clearFilter();
  }
}

// Loads data for the current collection using count-based virtual scrolling
// for immediate responsiveness. Items are fetched on-demand as user scrolls.
// Shows items for a collection, prepares the view, and primes data loading
