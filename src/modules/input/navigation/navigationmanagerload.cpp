// Collection load + item-count + items-loaded slots for NavigationManager.
// Extracted from navigationmanager.cpp during LOC-reduction refactor.
// These remain NavigationManager members; this is a translation-unit split.
#include "navigationmanager.h"

#include "applicationcontext.h"
#include "artworkutils.h"
#include "emptystatewidget.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "idetailspanemanager.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "isettingsmanager.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationstackmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "sessionmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/selection.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
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

auto NavigationManager::loadCollectionData(int collectionIndex) -> void {
  Q_UNUSED(collectionIndex)
  if (!databaseMgr() || !m_collections || !m_currentCollectionIndex) {
    return;
  }

  if ((*m_currentCollectionIndex) >= 0 && (*m_currentCollectionIndex) < (*m_collections).size()) {
    CollectionContext context;
    context.currentIndex = (*m_currentCollectionIndex);
    context.config = (*m_collections)[(*m_currentCollectionIndex)];
    context.config.mediaDirectory =
        SettingsUtils::expandConfigVariables(context.config.mediaDirectory, context.config.name);
    context.config.artworkDirectory =
        SettingsUtils::expandConfigVariables(context.config.artworkDirectory, context.config.name);
    context.artworkDirectory = context.config.artworkDirectory;
    if (m_generalSettings) {
      context.sortMode = m_generalSettings->sortMode;
      context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
      // mirror toolbar filters so the scroll pipeline can drop
      // subcollection tiles whose effective type doesn't match the active
      // filter, or hide them entirely.
      context.collectionTypeFilter = m_generalSettings->collectionTypeFilter;
      context.hideSubcollectionTiles = m_generalSettings->hideSubcollectionTiles;
    }

    bool hasMediaDirectory = !context.config.mediaDirectory.trimmed().isEmpty();
    // a playlist has no mediaDirectory but its items live in
    // playlist_items rather than on disk, so it still wants the full count +
    // range fetch path. Treat it as "has data" so we don't fall through to
    // the empty-emit branch.
    if (hasMediaDirectory || context.config.showAllSubcollectionItems ||
        context.config.isPlaylist) {
      // NOTE: Don't show overlay here - it will be shown by scanStarting signal
      // if a scan is actually needed. For cached collections, no overlay
      // appears.

      // Fast startup path: use cached counts for immediate rendering at
      // startup. This allows the UI to be responsive immediately while the
      // background count query runs for verification. If counts differ,
      // onItemCountLoaded will update the view.
      if (tryUseCachedCountForStartup(context)) {
        // Still request actual count in background for verification/update
        m_backgroundCountRefreshInProgress = true;
        m_backgroundCountRefreshCollectionIndex = context.currentIndex;
        requestItemCountForContext(context, QString());
        return;
      }

      // Standard path: wait for count query before rendering
      // Items are fetched on-demand via fetchItemsRange as user scrolls
      requestItemCountForContext(context, QString());
    } else {
      QStringList emptyFilePaths;
      QHash<QString, QString> emptyFileNames;
      onItemsLoaded(emptyFilePaths, emptyFileNames);
    }
  }
}

// Attempts to use cached viewport for immediate rendering at startup.
// This provides instant item display by using cached file paths and names
// without waiting for database queries.
// Returns true if cached viewport was used; background query still runs for
// verification.
auto NavigationManager::tryUseCachedCountForStartup(const CollectionContext &context) -> bool {
  // Only use fast path on initial startup with rememberSelection enabled
  if (!m_isInitialStartupLoad) {
    qCDebug(lcSearchDiag) << "[NavigationManager] "
                             "tryUseCachedCountForStartup: SKIP - not initial startup";
    return false;
  }
  if (!sessionMgr() || !m_generalSettings || !m_generalSettings->rememberSelection) {
    qCDebug(lcSearchDiag) << "[NavigationManager] tryUseCachedCountForStartup: "
                             "SKIP - no sessionManager or rememberSelection disabled";
    return false;
  }
  if (!m_collections || context.currentIndex < 0 ||
      context.currentIndex >= (*m_collections).size()) {
    qCDebug(lcSearchDiag) << "[NavigationManager] tryUseCachedCountForStartup: "
                             "SKIP - invalid collection index";
    return false;
  }

  // Look up cached viewport with file paths for instant rendering
  const CollectionConfig &cfg = (*m_collections)[context.currentIndex];
  const QString collectionKey = CollectionUtils::hierarchicalNameFor(cfg, *m_collections);
  SessionManager::CachedViewport cachedVp = sessionMgr()->getCachedViewport(collectionKey);

  if (!cachedVp.isValid()) {
    qCDebug(lcSearchDiag) << "[NavigationManager] tryUseCachedCountForStartup: "
                             "SKIP - no valid cached viewport for"
                          << collectionKey;
    return false;
  }

  qCDebug(lcSearchDiag) << "[NavigationManager] "
                           "tryUseCachedCountForStartup: USING cached viewport for"
                        << collectionKey << "with" << cachedVp.filePaths.size() << "paths and"
                        << cachedVp.artworkPaths.size() << "artwork paths";
  // Mark that we've used the fast path - clear the initial startup flag
  m_isInitialStartupLoad = false;

  int totalItems = cachedVp.totalItems;
  if (totalItems <= 0) {
    return false;
  }

  // Use minimal context for immediate display - skip expensive UUID
  // computation. The cached viewport already has file paths resolved, so we
  // don't need precomputed UUID maps for initial rendering. Those will be built
  // lazily when the background count query completes.
  CollectionContext minimalContext;
  minimalContext.currentIndex = context.currentIndex;
  minimalContext.config = context.config;
  minimalContext.artworkDirectory = context.artworkDirectory;
  if (m_generalSettings) {
    minimalContext.sortMode = m_generalSettings->sortMode;
    minimalContext.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
    // minimal context still needs the toolbar filter so the
    // immediate cached viewport hides the same tiles the full reload would.
    minimalContext.collectionTypeFilter = m_generalSettings->collectionTypeFilter;
    minimalContext.hideSubcollectionTiles = m_generalSettings->hideSubcollectionTiles;
  }

  // Store minimal context for now - will be replaced with full context
  // when background query completes
  m_hasItemsQueryContext = true;
  m_itemsQueryContext = minimalContext;
  m_itemsQueryFilter = QString();

  // Increment generation to mark this as a fresh view
  ++m_itemsViewGeneration;
  m_pendingRangeGenerations.clear();

  // Use the cached start index as our selection position
  int selIdx = cachedVp.startIndex;
  if (selIdx < 0 || selIdx >= totalItems) {
    selIdx = 0;
  }

  // Set up virtual scrolling immediately with cached count
  if (scrollMgr()) {
    scrollMgr()->setInitialScrollIndex(selIdx);
    scrollMgr()->setupVirtualScrolling(totalItems, minimalContext);

    // Inject cached items directly into scroll manager for instant display
    // Including cached artwork paths for instant artwork resolution
    scrollMgr()->injectCachedItems(cachedVp.startIndex, cachedVp.filePaths, cachedVp.fileNames,
                                   cachedVp.artworkPaths);
  }

  // Show the items page
  resumeItemsPageRendering();
  if (m_loadingLabel) {
    m_loadingLabel->hide();
  }
  if (m_stackedWidget && m_itemsPage) {
    m_stackedWidget->setCurrentWidget(m_itemsPage);
  }

  // Update artwork for visible items - use paths directly from cache
  if (artworkMgr()) {
    artworkMgr()->updateViewportArtwork();
  }
  if (artworkMgr() && artworkMgr()->getTimerCoordinator()) {
    artworkMgr()->getTimerCoordinator()->scheduleViewportUpdate();
  }

  // Schedule selection restore
  if (selIdx >= 0 && interactionMgr()) {
    scheduleSelectionRestore(selIdx, UIConstants::Selection::RESTORE_STEPS,
                             UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                             UIConstants::Selection::RESTORE_MAX_DELAY_MS);
  }

  schedulePostLoadOperations();

  qCDebug(lcSearchDiag) << QString("tryUseCachedCountForStartup: used cached viewport "
                                   "startIdx=%1 totalItems=%2 cachedPaths=%3")
                               .arg(cachedVp.startIndex)
                               .arg(totalItems)
                               .arg(cachedVp.filePaths.size());

  return true;
}

// Returns cached expanded context if available for the same collection index,
// otherwise builds and caches a new one. This avoids recomputing precomputed
// descendants, UUIDs, and directory maps on every search keystroke.
CollectionContext NavigationManager::getOrBuildExpandedContext(int collectionIndex) {
  // Return cached context if it matches the requested collection
  if (m_cachedExpandedContextIndex == collectionIndex && m_cachedExpandedContext.isValid()) {
    return m_cachedExpandedContext;
  }

  // Build and cache new context
  m_cachedExpandedContext = buildExpandedContextForIndex(collectionIndex);
  m_cachedExpandedContextIndex = collectionIndex;
  return m_cachedExpandedContext;
}

CollectionContext NavigationManager::buildExpandedContextForIndex(int collectionIndex) const {
  CollectionContext context;
  context.currentIndex = collectionIndex;
  if (!m_collections || collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return context;
  }

  context.config = (*m_collections)[collectionIndex];

  // Use pre-computed expanded directories from hierarchy cache if available
  // This eliminates repeated path expansion (variable substitution)
  if (m_hierarchyCache && m_hierarchyCache->isValid()) {
    context.config.mediaDirectory = m_hierarchyCache->expandedMediaDir(collectionIndex);
    context.config.artworkDirectory = m_hierarchyCache->expandedArtworkDir(collectionIndex);
  } else {
    context.config.mediaDirectory =
        SettingsUtils::expandConfigVariables(context.config.mediaDirectory, context.config.name);
    context.config.artworkDirectory =
        SettingsUtils::expandConfigVariables(context.config.artworkDirectory, context.config.name);

    // Resolve artwork directory with parent fallback
    if (context.config.artworkDirectory.trimmed().isEmpty()) {
      QString resolvedArtwork =
          CollectionUtils::resolveArtworkDirectory(collectionIndex, *m_collections);
      context.config.artworkDirectory =
          SettingsUtils::expandConfigVariables(resolvedArtwork, context.config.name);
    }
  }

  context.artworkDirectory = context.config.artworkDirectory;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
    // toolbar filter mirroring (see load context above).
    context.collectionTypeFilter = m_generalSettings->collectionTypeFilter;
    context.hideSubcollectionTiles = m_generalSettings->hideSubcollectionTiles;
  }

  // Use pre-computed UUIDs and directory maps from hierarchy cache
  // for O(1) query performance. This eliminates repeated SHA1 hash
  // computations and path expansions during search queries.
  if (m_hierarchyCache && m_hierarchyCache->isValid()) {
    context.precomputedDescendants = m_hierarchyCache->allDescendants(collectionIndex);

    // Use cached UUID for current collection
    QString currentUuid = m_hierarchyCache->collectionUuid(collectionIndex);
    if (!currentUuid.isEmpty()) {
      context.precomputedDescendantUuids << currentUuid;
      context.precomputedUuidToMediaDir[currentUuid] =
          m_hierarchyCache->uuidToMediaDir(currentUuid);
      context.precomputedUuidToArtworkDir[currentUuid] =
          m_hierarchyCache->uuidToArtworkDir(currentUuid);
      context.precomputedUuidToCollectionIndex[currentUuid] = collectionIndex;
    }

    // Use cached UUIDs for all descendants - O(1) lookups
    for (int descendantIndex : context.precomputedDescendants) {
      QString uuid = m_hierarchyCache->collectionUuid(descendantIndex);
      if (!uuid.isEmpty()) {
        context.precomputedDescendantUuids << uuid;
        context.precomputedUuidToMediaDir[uuid] = m_hierarchyCache->uuidToMediaDir(uuid);
        context.precomputedUuidToArtworkDir[uuid] = m_hierarchyCache->uuidToArtworkDir(uuid);
        context.precomputedUuidToCollectionIndex[uuid] = descendantIndex;
      }
    }
  }

  return context;
}

void NavigationManager::requestItemCountForContext(const CollectionContext &context,
                                                   const QString &filter) {
  if (!databaseMgr() || !m_collections) {
    return;
  }
  m_hasItemsQueryContext = true;
  m_itemsQueryContext = context;

  // Persist the filter used to build this items view so subsequent paginated
  // range requests can use the exact same filter string.
  m_itemsQueryFilter = filter.trimmed();

  qCDebug(lcSearchDiag)
      << QString("requestItemCountForContext: collIndex=%1 filter='%2' "
                 "includeSubfolders=%3 showAllSubfolderItems=%4 "
                 "currentSubfolder='%5' includeDesc=%6 includeAll=%7")
             .arg(m_itemsQueryContext.currentIndex)
             .arg(m_itemsQueryFilter)
             .arg(m_itemsQueryContext.config.folderBrowsing.includeContentSubfolders)
             .arg(m_itemsQueryContext.config.folderBrowsing.showAllSubfolderItems)
             .arg(m_itemsQueryContext.config.folderBrowsing.currentSubfolder)
             .arg(m_itemsQueryContext.queryIncludeDescendants)
             .arg(m_itemsQueryContext.queryIncludeAllCollections);

  ++m_itemCountRequestToken;
  qCWarning(lcScanFlow) << "requestItemCountForContext: newToken=" << m_itemCountRequestToken
                        << "collIdx=" << context.currentIndex << "filter='" << filter << "'";
  databaseMgr()->fetchItemCount(m_itemsQueryContext, (*m_collections), m_itemsQueryFilter,
                                m_itemCountRequestToken);
}

void NavigationManager::onItemsLoaded(const QStringList &filePaths,
                                      const QHash<QString, QString> &fileNames) {
  if (!validateItemsLoadedContext()) {
    return;
  }

  cleanupExistingNoItemsWidgets();

  QList<int> subcollections = getSubcollections((*m_currentCollectionIndex));
  bool hasContent = determineContentAvailability(filePaths, subcollections);

  if (!hasContent) {
    handleEmptyContent();
    return;
  }

  CollectionContext context = setupCollectionContext(filePaths, fileNames);
  int totalItems = subcollections.size() + filePaths.size();
  int selIdx = calculateSelectionIndex(totalItems);

  if (scrollMgr()) {
    // Pre-set scroll position to avoid visual jump where list briefly
    // shows at position 0 then jumps to remembered position
    if (selIdx >= 0) {
      scrollMgr()->setInitialScrollIndex(selIdx);
    }
    scrollMgr()->setupVirtualScrolling(totalItems, context);
  }

  resumeItemsPageRendering();

  if (artworkMgr()) {
    artworkMgr()->updateViewportArtwork();
  }
  if (artworkMgr() && artworkMgr()->getTimerCoordinator()) {
    artworkMgr()->getTimerCoordinator()->scheduleViewportUpdate();
  }

  bool pendingRestore = state() ? state()->selectionRestore().restorePending : false;
  // Also skip if there's a pending path-based restore (from sort change)
  bool pendingPathRestore = scrollMgr() && scrollMgr()->hasPendingSelectionRestoreByPath();
  debugLog("[SelectionRestore] onItemsLoaded: selIdx="
           << selIdx << "totalItems=" << totalItems << "pendingRestore=" << pendingRestore
           << "pendingPathRestore=" << pendingPathRestore
           << "collectionIndex=" << (*m_currentCollectionIndex));
  if (selIdx >= 0 && (interactionMgr()) && !pendingRestore && !pendingPathRestore) {
    if (lcNavigationManager().isDebugEnabled()) {
      int depth = computeCollectionDepth((*m_currentCollectionIndex));
      debugLog("[SelectionRestore] depth=" << depth << "for collection"
                                           << (*m_collections)[(*m_currentCollectionIndex)].name);
    }
    // Use scheduleSelectionRestore for all depths to ensure widgets are
    // materialized before selection restore attempts sidebar update
    scheduleSelectionRestore(selIdx, UIConstants::Selection::RESTORE_STEPS,
                             UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                             UIConstants::Selection::RESTORE_MAX_DELAY_MS);
  }

  schedulePostLoadOperations();
}

void NavigationManager::onMediaLibraryError(const ErrorUtils::ErrorContext &error) {
  if (m_loadingLabel) {
    m_loadingLabel->hide();
  }

  // Hide loading overlay if visible
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }

  if (m_gridContainer) {
    QList<QWidget *> existingLabels = m_gridContainer->findChildren<QWidget *>("noItemsWidget");
    for (QWidget *widget : existingLabels) {
      widget->deleteLater();
    }
  }

  // Surface the database error to the owner, which shows the ErrorDialog —
  // NavigationManager itself stays out of the UI-chrome layer.
  emit mediaLibraryErrorRaised(error);

  // Drop the previous error widget so rapid load failures don't accumulate
  // children on m_gridContainer.
  if (m_errorWidget) {
    m_errorWidget->deleteLater();
    m_errorWidget = nullptr;
  }

  auto *errorWidget = new QWidget(m_gridContainer);
  errorWidget->setObjectName("noItemsWidget");

  auto *errorLabel = new QLabel(error.message, errorWidget);
  errorLabel->setAlignment(Qt::AlignCenter);
  errorLabel->setStyleSheet("QLabel { color: palette(text); font-size: 14px; }");

  auto *layout = new QVBoxLayout(errorWidget);
  layout->addWidget(errorLabel);
  layout->setContentsMargins(0, 0, 0, 0);

  if (m_itemScrollArea) {
    QRect viewportRect = m_itemScrollArea->viewport()->rect();
    errorWidget->setGeometry(viewportRect);
  }

  errorWidget->show();
  errorWidget->raise();
  m_errorWidget = errorWidget;

  if (databaseMgr()) {
    databaseMgr()->updateCachedCounts((*m_collections));
  }
  if (m_refreshTitleCounts) m_refreshTitleCounts();
}

void NavigationManager::onBackgroundCollectionScanCompleted(const QString &collectionUuid) {
  qCWarning(lcScanFlow) << "onBackgroundCollectionScanCompleted: uuid=" << collectionUuid;

  if (!databaseMgr() || !m_collections || !m_currentCollectionIndex) {
    qCWarning(lcScanFlow) << "Early return: missing deps";
    return;
  }

  // If the user is actively searching, don't force a background refresh.
  // A scan completion-triggered refresh rebuilds the items view with an empty
  // filter, which can invalidate search paging requests and cause a tight
  // rebuild loop (blank search results + high CPU).
  if ((m_searchBar) && !m_searchBar->text().trimmed().isEmpty()) {
    return;
  }
  if (!m_itemsQueryFilter.trimmed().isEmpty()) {
    return;
  }

  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  // Only refresh counts if the completed scan affects the currently visible
  // collection (or its descendants when showAllSubcollectionItems is active).
  CollectionConfig cur = (*m_collections)[idx];
  cur.mediaDirectory = PathUtils::validateAndExpandPath(cur.mediaDirectory, cur.name);

  if (!cur.mediaDirectory.trimmed().isEmpty()) {
    const QString curUuid = CollectionUtils::computeCollectionUuid(cur.name, cur.mediaDirectory);
    qCWarning(lcScanFlow) << "UUID compare: cur=" << curUuid << "incoming=" << collectionUuid
                          << "match=" << (curUuid == collectionUuid);
    if (curUuid == collectionUuid) {
      qCWarning(lcScanFlow) << "UUID MATCH - calling loadCollectionData for idx=" << idx;
      m_backgroundCountRefreshInProgress = true;
      m_backgroundCountRefreshCollectionIndex = idx;
      loadCollectionData(idx);
      return;
    }
  } else {
    qCWarning(lcScanFlow) << "mediaDirectory empty for" << cur.name;
  }

  // For descendant scans when showAllSubcollectionItems is enabled:
  // Don't trigger intermediate refreshes while the loading overlay is still
  // active (indicating more scans are pending). MainWindow will trigger a
  // final reload once all scans complete.
  qCWarning(lcScanFlow) << "Checking descendants: showAllSubcollectionItems="
                        << cur.showAllSubcollectionItems;
  if (cur.showAllSubcollectionItems) {
    // Skip intermediate reloads if loading overlay is active (batch scan in
    // progress)
    if (m_loadingOverlay && m_loadingOverlay->isActive()) {
      qCWarning(lcScanFlow) << "Skipping - loading overlay is active";
      return;
    }

    QList<int> descendants = CollectionUtils::collectDescendantIndices(idx, (*m_collections));
    qCWarning(lcScanFlow) << "descendant count=" << descendants.size();
    for (int descendantIndex : descendants) {
      if (descendantIndex < 0 || descendantIndex >= (*m_collections).size()) {
        continue;
      }
      CollectionConfig subCol = (*m_collections)[descendantIndex];
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      qCWarning(lcScanFlow) << "Checking descendant" << descendantIndex << "name=" << subCol.name
                            << "mediaDir=" << subCol.mediaDirectory;
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      const QString subUuid =
          CollectionUtils::computeCollectionUuid(subCol.name, subCol.mediaDirectory);
      qCWarning(lcScanFlow) << "descendant UUID=" << subUuid
                            << "match=" << (subUuid == collectionUuid);
      if (subUuid == collectionUuid) {
        qCWarning(lcScanFlow) << "DESCENDANT MATCH - calling loadCollectionData";
        m_backgroundCountRefreshInProgress = true;
        m_backgroundCountRefreshCollectionIndex = idx;
        loadCollectionData(idx);
        return;
      }
    }
  }
}

void NavigationManager::onItemsRangeLoaded(int offset, const QStringList &filePaths,
                                           const QHash<QString, QString> &fileNames,
                                           const QHash<QString, QString> &fileToArtworkDir,
                                           const QHash<QString, QString> &fileToMediaDir,
                                           const QHash<QString, int> &fileToCollectionIndex) {
  Q_UNUSED(fileToMediaDir)
  Q_UNUSED(fileToCollectionIndex)
  const auto expectedGen = m_pendingRangeGenerations.value(offset, 0);
  if (expectedGen != m_itemsViewGeneration) {
    qCDebug(lcSearchDiag) << QString("onItemsRangeLoaded: DROPPED offset=%1 paths=%2 "
                                     "expectedGen=%3 currentGen=%4")
                                 .arg(offset)
                                 .arg(filePaths.size())
                                 .arg(expectedGen)
                                 .arg(m_itemsViewGeneration);
    return;
  }
  m_pendingRangeGenerations.remove(offset);
  if (scrollMgr()) {
    qCDebug(lcSearchDiag) << QString("onItemsRangeLoaded: forward offset=%1 paths=%2")
                                 .arg(offset)
                                 .arg(filePaths.size());
    scrollMgr()->receiveItemsRange(offset, filePaths, fileNames, fileToArtworkDir);
  }
}

void NavigationManager::fetchItemsRange(int offset, int limit) {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context =
      m_hasItemsQueryContext ? m_itemsQueryContext : buildExpandedContextForIndex(idx);

  // Use the same filter that produced the current items view, if available.
  // Falls back to the search bar (legacy behavior) when no query filter is set.
  QString filter = !m_itemsQueryFilter.isEmpty() ? m_itemsQueryFilter : QString();
  if (filter.isEmpty() && m_searchBar) {
    filter = m_searchBar->text().trimmed();
  }

  m_pendingRangeGenerations.insert(offset, m_itemsViewGeneration);

  qCDebug(lcSearchDiag) << QString("fetchItemsRange: offset=%1 limit=%2 filter='%3' gen=%4")
                               .arg(offset)
                               .arg(limit)
                               .arg(filter)
                               .arg(m_itemsViewGeneration);

  databaseMgr()->fetchItemsRange(context, *m_collections, offset, limit, filter);
}
