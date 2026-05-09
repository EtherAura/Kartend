// Filter and reload methods for NavigationManager.
// Extracted from navigationmanager.cpp during LOC-reduction refactor.
// These remain NavigationManager members; this is a translation-unit split.
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants.h"

#include <algorithm>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QTimer>

#include <QLoggingCategory>

// Local mirror of the diagnostic logging macro used in navigationmanager.cpp.
// Re-defined here so this TU has the same release-safe diagnostic gate
// without leaking the macro through a header.
void NavigationManager::filterItems(const QString &searchText) {
  if (!m_currentCollectionIndex || !m_collections) {
    return;
  }

  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = getOrBuildExpandedContext(idx);

  qCDebug(lcSearchDiag) << QString(
                               "filterItems: idx=%1 query='%2' mediaDir='%3' includeSubfolders=%4 "
                               "showAllSubfolderItems=%5 currentSubfolder='%6'")
                               .arg(idx)
                               .arg(searchText)
                               .arg(context.config.mediaDirectory)
                               .arg(context.config.includeContentSubfolders)
                               .arg(context.config.showAllSubfolderItems)
                               .arg(context.config.currentSubfolder);
  requestItemCountForContext(context, searchText);
}

void NavigationManager::filterItemsCurrentAndSubcollections(const QString &searchText) {
  if (!m_currentCollectionIndex || !m_collections) {
    return;
  }

  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = getOrBuildExpandedContext(idx);
  context.queryIncludeDescendants = true;

  qCDebug(lcSearchDiag) << QString("filterItemsCurrentAndSubcollections: idx=%1 query='%2' "
                                   "includeDesc=%3")
                               .arg(idx)
                               .arg(searchText)
                               .arg(context.queryIncludeDescendants);
  requestItemCountForContext(context, searchText);
}

void NavigationManager::filterItemsAllCollections(const QString &searchText) {
  if (!m_currentCollectionIndex || !m_collections) {
    return;
  }

  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = getOrBuildExpandedContext(idx);
  context.queryIncludeAllCollections = true;
  context.hasSubcollectionOverride = true;
  context.subcollectionOverride = {};
  context.suppressVirtualFolders = true;

  qCDebug(lcSearchDiag) << QString("filterItemsAllCollections: idx=%1 query='%2' includeAll=%3")
                               .arg(idx)
                               .arg(searchText)
                               .arg(context.queryIncludeAllCollections);
  requestItemCountForContext(context, searchText);
}

// Return true if any descendant of parentIndex has a non-empty mediaDirectory
auto NavigationManager::collectionHasDescendantWithMedia(int parentIndex) const -> bool {
  QList<int> childCollections = getAllDescendantCollections(parentIndex);
  return std::ranges::any_of(childCollections, [this](int childIndex) {
    if (childIndex < 0 || childIndex >= (*m_collections).size()) {
      return false;
    }
    const QString &mediaDir = (*m_collections)[childIndex].mediaDirectory;
    return !mediaDir.trimmed().isEmpty();
  });
}

// Forces a full rescan of the collection by clearing cached data first
void NavigationManager::forceRescanCollection(int collectionIndex) {
  if (!m_collections || !m_databaseManager || collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return;
  }

  // Show loading overlay during rescan
  if (m_loadingOverlay) {
    m_loadingOverlay->show("Clearing cache...");
  }

  // Clear cached items to force a fresh scan (async operation)
  const CollectionConfig &config = (*m_collections)[collectionIndex];
  QString mediaDir = SettingsUtils::expandConfigVariables(config.mediaDirectory, config.name);
  QString uuid = CollectionUtils::computeCollectionUuid(config.name, mediaDir);

  // Mark that we're rescanning so onItemCountLoaded knows to hide the overlay
  m_isRescanInProgress = true;
  m_pendingRescanCollectionIndex = collectionIndex;

  // Disconnect any previous connection to avoid duplicate calls
  if (m_cacheInvalidatedConnection) {
    QObject::disconnect(m_cacheInvalidatedConnection);
  }

  // Connect one-shot handler to proceed after cache invalidation completes
  m_cacheInvalidatedConnection =
      QObject::connect(m_databaseManager, &DatabaseManager::cacheInvalidated, this,
                       [this](const QString & /*invalidatedUuid*/) {
                         // Disconnect immediately to ensure one-shot behavior
                         QObject::disconnect(m_cacheInvalidatedConnection);
                         m_cacheInvalidatedConnection = {};

                         if (m_pendingRescanCollectionIndex >= 0) {
                           // Update overlay message now that scan is starting
                           if (m_loadingOverlay) {
                             m_loadingOverlay->setMessage("Rescanning collection...");
                           }

                           int idx = m_pendingRescanCollectionIndex;
                           m_pendingRescanCollectionIndex = -1;

                           // Now reload - ensureCollectionScanned will scan since cache is
                           // cleared
                           safeReloadCollection(idx);
                         }
                       });

  // Initiate async cache invalidation
  m_databaseManager->invalidateCollectionCache(uuid);
}

// Safely reloads the specified collection and recenters the horizontal
// scrollbar.
//
// This method is debounced because repeated invocations (e.g. multiple signals
// firing during search clear / settings apply) can cause repeated cleanup
// calls, leaving m_totalItems at 0 and the grid empty while reloads churn.
void NavigationManager::safeReloadCollection(int collectionIndex) {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return;
  }

  qCDebug(lcSearchDiag) << "safeReloadCollection requested idx=" << collectionIndex;
  qCWarning(lcScanFlow) << "safeReloadCollection requested idx=" << collectionIndex;

  if (!m_safeReloadTimer) {
    m_safeReloadTimer = new QTimer(this);
    m_safeReloadTimer->setSingleShot(true);

    QObject::connect(m_safeReloadTimer, &QTimer::timeout, this, [this]() {
      const int pendingIndex = m_pendingSafeReloadCollectionIndex;
      m_pendingSafeReloadCollectionIndex = -1;

      qCDebug(lcSearchDiag) << "safeReloadCollection firing idx=" << pendingIndex;
      qCWarning(lcScanFlow) << "safeReloadCollection timer FIRING idx=" << pendingIndex;

      if (!m_collections || !m_databaseManager || !m_currentCollectionIndex || pendingIndex < 0 ||
          pendingIndex >= (*m_collections).size()) {
        return;
      }

      // Determine which collection to reload. If the pending index doesn't
      // match what the user is currently viewing, reload the current collection
      // instead. This handles the case where a subcollection reload is
      // requested but the user has navigated to a different collection.
      int reloadIndex = pendingIndex;
      if (pendingIndex != (*m_currentCollectionIndex)) {
        qCWarning(lcScanFlow) << "safeReloadCollection: pendingIndex" << pendingIndex
                              << "!= currentIndex" << (*m_currentCollectionIndex)
                              << "- reloading current instead";
        reloadIndex = (*m_currentCollectionIndex);

        // Validate the current collection index
        if (reloadIndex < 0 || reloadIndex >= (*m_collections).size()) {
          return;
        }
      }

      CollectionContext context;
      context.currentIndex = reloadIndex;
      context.config = (*m_collections)[reloadIndex];
      context.config.mediaDirectory =
          SettingsUtils::expandConfigVariables(context.config.mediaDirectory, context.config.name);
      context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
          context.config.artworkDirectory, context.config.name);
      context.artworkDirectory = context.config.artworkDirectory;
      if (m_generalSettings) {
        context.sortMode = m_generalSettings->sortMode;
        context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
        // mirror toolbar filters so subcollection tile visibility
        // honors the active type filter / hide-subs toggle.
        context.collectionTypeFilter = m_generalSettings->collectionTypeFilter;
        context.hideSubcollectionTiles = m_generalSettings->hideSubcollectionTiles;
      }

      // Reload after cleanup has settled to avoid use-after-free and
      // re-entrancy issues when UI events fire during widget destruction.
      requestItemCountForContext(context, QString());

      // Update toolbar title to reflect subfolder if navigated into one.
      updateItemsPageTitle(reloadIndex);

      // Delay centering until items are loaded and layout is calculated.
      QTimer::singleShot(UIConstants::Timing::VIEWPORT_DELAY_MS, this, [this]() {
        if (m_scrollManager && m_currentCollectionIndex && m_collections) {
          m_scrollManager->centerHorizontalScrollbar((*m_currentCollectionIndex), (*m_collections));
        }
      });
    });
  }

  m_pendingSafeReloadCollectionIndex = collectionIndex;

  const bool alreadyScheduled = m_safeReloadTimer->isActive();
  if (!alreadyScheduled) {
    persistCurrentSelection();
    // We're about to rebuild the items view. Clear any stale selection-restore
    // suppression so automatic restore can re-apply the selection rectangle.
    if (m_interactionManager) {
      m_interactionManager->resetSelectionRestoreState();
    }

    if (m_artworkManager) {
      if (auto *coord = m_artworkManager->getTimerCoordinator()) {
        coord->stopAllTimers();
      }
    }

    if (m_scrollManager) {
      m_scrollManager->cleanup();
    }

    if (m_artworkManager) {
      m_artworkManager->cancelAllArtworkLoading();
    }
  }

  qCDebug(lcSearchDiag) << "safeReloadCollection scheduled (debounced) idx="
                        << m_pendingSafeReloadCollectionIndex;

  // Wait for cleanup to complete and coalesce repeated reload requests.
  m_safeReloadTimer->start(UIConstants::Timing::MEDIUM_DELAY_MS);
}
