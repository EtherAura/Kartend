// Filter and reload methods for NavigationManager.
// Extracted from navigationmanager.cpp during LOC-reduction refactor.
// These remain NavigationManager members; this is a translation-unit split.
#include "collection/collectioncontext.h"
#include "collection/typehelpers.h"
#include "databasemanager.h"
#include "iartworkmanager.h"
#include "interactionmanager.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/timing.h"

#include <algorithm>
#include <QLineEdit>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QTimer>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)

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
                               .arg(context.config.folderBrowsing.includeContentSubfolders)
                               .arg(context.config.folderBrowsing.showAllSubfolderItems)
                               .arg(context.config.folderBrowsing.currentSubfolder);
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
  CollectionContext context;
  if (idx >= 0 && idx < (*m_collections).size()) {
    context = getOrBuildExpandedContext(idx);
  } else {
    // Root / Home view (no collection selected): build a context that has
    // no host collection but is still suitable for the cross-collection
    // query path. queryIncludeAllCollections = true tells the worker to
    // span every collection; isRootView keeps back-navigation honest so
    // clearing search returns to the Home tile view.
    context.currentIndex = -1;
    context.isRootView = true;
    if (m_generalSettings) {
      context.sortMode = m_generalSettings->view.sortMode;
      context.excludeSubfoldersFromSort = m_generalSettings->view.excludeSubfoldersFromSort;
    }
  }
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
  if (!m_collections || !databaseMgr() || collectionIndex < 0 ||
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

  // Connect one-shot handler to proceed after cache invalidation completes.
  // One-shot for OUR uuid, not for the first invalidation of anything: the
  // signal is shared, and a background launcher-enrichment refresh
  // invalidates OTHER collections' caches (Kartend-xkdxn's fix made that a
  // routine occurrence). Firing on one of those would reload before this
  // rescan's cache is actually cleared — the rescan would silently serve
  // stale rows (Kartend-1fhgz).
  m_cacheInvalidatedConnection =
      QObject::connect(databaseMgr(), &DatabaseManager::cacheInvalidated, this,
                       [this, uuid](const QString &invalidatedUuid) {
                         if (invalidatedUuid != uuid) {
                           return; // someone else's invalidation — keep waiting
                         }
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
  databaseMgr()->invalidateCollectionCache(uuid);
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

  if (!m_safeReloadTimerInited) {
    m_safeReloadTimer.setSingleShot(true);
    m_safeReloadTimerInited = true;

    QObject::connect(&m_safeReloadTimer, &QTimer::timeout, this, [this]() {
      const int pendingIndex = m_pendingSafeReloadCollectionIndex;
      m_pendingSafeReloadCollectionIndex = -1;

      qCDebug(lcSearchDiag) << "safeReloadCollection firing idx=" << pendingIndex;
      qCWarning(lcScanFlow) << "safeReloadCollection timer FIRING idx=" << pendingIndex;

      if (!m_collections || !databaseMgr() || !m_currentCollectionIndex || pendingIndex < 0 ||
          pendingIndex >= (*m_collections).size()) {
        return;
      }

      // LAST possible moment to capture where the user actually is. Persisting
      // when the reload is REQUESTED is not enough: the request is debounced,
      // so anything done between the final request and this handler running —
      // and the scroll animation settling is squarely inside that window — was
      // still being lost. The rebuild below clears the selection to -1, which
      // destroys the evidence, and the restore that follows then faithfully
      // re-applies whatever was last persisted. That is the revert: not the
      // restore misbehaving (its own trace shows it firing against current=-1,
      // exactly as intended) but the value it was handed being out of date.
      persistCurrentSelection();
      // TEMPORARY DIAGNOSTIC (2026-08-21) — pairs with RESTORETRACE ARM: this
      // is the value the upcoming restore will be handed.
      qCWarning(lcNavigationManager).nospace()
          << "RESTORETRACE PERSIST-AT-RELOAD reloadPending=" << pendingIndex;

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
        context.sortMode = m_generalSettings->view.sortMode;
        context.excludeSubfoldersFromSort = m_generalSettings->view.excludeSubfoldersFromSort;
        // mirror toolbar filters so subcollection tile visibility
        // honors the active type filter / hide-subs toggle.
        context.collectionTypeFilter = m_generalSettings->view.collectionTypeFilter;
        context.hideSubcollectionTiles = m_generalSettings->view.hideSubcollectionTiles;
      }

      // Preserve any active search across the reload. Without this, a reload
      // triggered mid-search (e.g. post-scrape artwork refresh from the
      // context menu) drops the filter and re-renders the full collection
      // while the search bar still shows the query — the visible symptom is
      // a scroll area sized for the unfiltered set with mostly empty cells.
      // Carry over the prior scope flags so a search that was scoped to
      // descendants / all collections keeps that scope after the reload.
      const QString activeFilter = m_searchBar ? m_searchBar->text().trimmed() : QString();
      if (!activeFilter.isEmpty() && m_hasItemsQueryContext) {
        context.queryIncludeDescendants = m_itemsQueryContext.queryIncludeDescendants;
        context.queryIncludeAllCollections = m_itemsQueryContext.queryIncludeAllCollections;
        if (m_itemsQueryContext.hasSubcollectionOverride) {
          context.hasSubcollectionOverride = true;
          context.subcollectionOverride = m_itemsQueryContext.subcollectionOverride;
        }
        context.suppressVirtualFolders =
            context.suppressVirtualFolders || m_itemsQueryContext.suppressVirtualFolders;
      }

      // Reload after cleanup has settled to avoid use-after-free and
      // re-entrancy issues when UI events fire during widget destruction.
      requestItemCountForContext(context, activeFilter);

      // Update toolbar title to reflect subfolder if navigated into one.
      updateItemsPageTitle(reloadIndex);

      // Delay centering until items are loaded and layout is calculated.
      QTimer::singleShot(UIConstants::Timing::VIEWPORT_DELAY_MS, this, [this]() {
        if (scrollMgr() && m_currentCollectionIndex && m_collections) {
          scrollMgr()->centerHorizontalScrollbar((*m_currentCollectionIndex), (*m_collections));
        }
      });
    });
  }

  m_pendingSafeReloadCollectionIndex = collectionIndex;

  const bool alreadyScheduled = m_safeReloadTimer.isActive();

  // Persist on EVERY request, not just the first of a burst. The reload is
  // debounced — each request restarts the timer — so with the persist inside
  // the !alreadyScheduled arm the saved index was whatever the selection
  // happened to be when the burst STARTED. Anything the user did during the
  // debounce window was then thrown away by the restore that follows the
  // reload, which is the selection jumping backwards to an older item
  // (field report 2026-08-20, reproduced as "scroll, click an item, then
  // scroll": the click opens the burst and persists, the scrolling that
  // follows is coalesced into it and never re-persists).
  //
  // This is not a behaviour change: the same restore still runs, to the same
  // collection, at the same moment. It simply carries the user's CURRENT
  // selection instead of a stale snapshot of it.
  persistCurrentSelection();

  if (!alreadyScheduled) {
    // We're about to rebuild the items view. Clear any stale selection-restore
    // suppression so automatic restore can re-apply the selection rectangle.
    if (interactionMgr()) {
      // TEMPORARY DIAGNOSTIC (2026-08-21) — names this reset site in the trace.
      qCWarning(lcNavigationManager) << "RESTORETRACE RESET-FROM NavigationManagerFilter rebuild";
      interactionMgr()->resetSelectionRestoreState();
    }

    if (artworkMgr()) {
      if (auto *coord = artworkMgr()->getTimerCoordinator()) {
        coord->stopAllTimers();
      }
    }

    if (scrollMgr()) {
      scrollMgr()->cleanup();
    }

    if (artworkMgr()) {
      artworkMgr()->cancelAllArtworkLoading();
    }
  }

  qCDebug(lcSearchDiag) << "safeReloadCollection scheduled (debounced) idx="
                        << m_pendingSafeReloadCollectionIndex;

  // Wait for cleanup to complete and coalesce repeated reload requests.
  m_safeReloadTimer.start(UIConstants::Timing::MEDIUM_DELAY_MS);
}
