// Routing/dispatch cluster split out from navigationmanager.cpp.
#include "idetailspanemanager.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "isessionmanager.h"
#include "isettingsmanager.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "selectionrestorecoordinator.h"
#include "settingsutils.h"
#include "uiconstants/navigation.h"
#include "uiconstants/timing.h"

#include <QApplication>
#include <QLineEdit>
#include <QStackedWidget>
#include <QtGlobal>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcNavigationManager().isDebugEnabled()) {                                                  \
      qCDebug(lcNavigationManager) << msg;                                                         \
    }                                                                                              \
  } while (0)

auto NavigationManager::validateAndPrepareNavigation(int collectionIndex) -> bool {
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
    if (appNotShuttingDown() && interactionMgr()) {
      // Clear navigation progress flag early when validation fails -
      // prevents navigation from being blocked indefinitely
      QTimer::singleShot(UIConstants::Navigation::PROGRESS_CLEAR_EARLY_MS, this, [this]() {
        if (appNotShuttingDown() && interactionMgr()) {
          interactionMgr()->setNavigationInProgress(false);
        }
      });
    }
    return false;
  }
  return true;
}

auto NavigationManager::handleSubcollectionNavigation(int collectionIndex, int previousIndex)
    -> void {
  Q_UNUSED(previousIndex)
  if (scrollMgr()) {
    scrollMgr()->updateContextForSubcollection(collectionIndex);
    scrollMgr()->applySubcollectionFilter(collectionIndex);
  }

  // Delegate selection restore to SelectionRestoreCoordinator
  if (m_selectionRestoreManager) {
    m_selectionRestoreManager->handleSubcollectionRestore(collectionIndex);
  }
}

auto NavigationManager::handleRegularNavigation(int collectionIndex) -> void {
  Q_UNUSED(collectionIndex)
  if (scrollMgr()) {
    scrollMgr()->clearFilter();
  }
}

auto NavigationManager::finalizeNavigation(int collectionIndex) -> void {
  applyCollectionSettingsOnly(collectionIndex);
  if ((m_searchBar) && m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }
  // Delay horizontal centering until layout has settled after navigation
  QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this, [this]() {
    if (scrollMgr() && m_currentCollectionIndex && m_collections) {
      scrollMgr()->centerHorizontalScrollbar((*m_currentCollectionIndex), (*m_collections));
    }
  });
  if (m_refreshTitleCounts) m_refreshTitleCounts();

  // Hide loading overlay after navigation completes
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }

  // Clear navigation progress flag after all animations complete -
  // allows user input to be processed again
  QTimer::singleShot(UIConstants::Navigation::PROGRESS_CLEAR_MS, this, [this]() {
    if (appNotShuttingDown() && interactionMgr()) {
      interactionMgr()->setNavigationInProgress(false);
    }
  });
}

// Delegates to SelectionRestoreCoordinator for selection restoration
auto NavigationManager::scheduleSelectionRestore(int desiredIndex, int finalEnsureDelayMs) -> void {
  if (m_selectionRestoreManager) {
    m_selectionRestoreManager->scheduleSelectionRestore(desiredIndex, finalEnsureDelayMs);
  }
}

// Validates collection index for showCollectionItems operation
auto NavigationManager::validateCollectionIndex(int collectionIndex) const -> bool {
  if (!appNotShuttingDown() || !m_collections) {
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
auto NavigationManager::handleSharedItemsNavigation(int collectionIndex) -> bool {
  bool itemsAreShared = areItemsShared((*m_currentCollectionIndex), collectionIndex);
  if (itemsAreShared) {
    navigateWithSharedItems(collectionIndex);
    if (m_refreshTitleCounts) m_refreshTitleCounts();
    return true;
  }
  return false;
}

// Prepares UI for non-shared navigation
auto NavigationManager::prepareNonSharedNavigation(int collectionIndex) -> void {
  if (!m_collections || !m_currentCollectionIndex) {
    return;
  }

  prepareForNonSharedNavigationHelper();
  suspendItemsPageRendering();

  (*m_currentCollectionIndex) = collectionIndex;

  applyUiPoliciesForCollection(collectionIndex);
  applyCollectionSettingsOnly(collectionIndex);

  if (scrollMgr()) {
    scrollMgr()->primeLayoutFor((*m_collections)[collectionIndex]);
  }
  if (interactionMgr()) {
    interactionMgr()->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);
  m_stackedWidget->setCurrentWidget(m_itemsPage);
  if (m_itemsPage && m_itemsPage->window()) {
    // Do NOT yank focus out of the collection tree (field report
    // 2026-08-18): highlighting a row now switches collection on its own,
    // and this call handed focus back to the window on every switch — so
    // the right stick stopped driving the tree mid-traversal and started
    // scrolling the details pane instead. A tree-driven switch keeps the
    // tree focused; every other route change behaves as before.
    QWidget *focused = QApplication::focusWidget();
    QWidget *tree = m_ctx ? m_ctx->ui.collectionTreeWidget : nullptr;
    const bool treeDriving = tree && focused && (focused == tree || tree->isAncestorOf(focused));
    if (!treeDriving) {
      m_itemsPage->window()->setFocus();
    }
    m_itemsPage->window()->activateWindow();
  }

  if (detailsPaneMgr()) {
    detailsPaneMgr()->applySidebarStateForCollection((*m_currentCollectionIndex));
  }

  if ((m_searchBar) && m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }

  if (scrollMgr()) {
    scrollMgr()->clearFilter();
  }
}

// Loads data for the current collection using count-based virtual scrolling
// for immediate responsiveness. Items are fetched on-demand as user scrolls.
// Shows items for a collection, prepares the view, and primes data loading
