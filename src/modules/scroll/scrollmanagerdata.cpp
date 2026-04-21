// Data ingest, virtual scrolling setup, and cleanup methods extracted from
// scrollmanager.cpp (Kartend-mhf). All remain ScrollManager members and
// access existing class state (m_dataManager, m_filterManager,
// m_widgetFactory, m_widgetPool, m_artworkManager, m_metrics, etc.).
#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "artworkmanager.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "filtermanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrolleventhandler.h"
#include "scrollmanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "uiconstants.h"
#include "virtualcontainermanager.h"
#include "widgetpoolmanager.h"

#include <QHash>
#include <QLoggingCategory>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcScrollManager().isDebugEnabled()) {                                  \
      qCDebug(lcScrollManager) << msg;                                         \
    }                                                                          \
  } while (0)

// Temporary diagnostic logging (release-safe) gated by env var.
// Enable with: `KARTEND_SEARCH_DIAG=1 kartend`
static inline bool searchDiagEnabled() {
  return qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG");
}

#define diagLog(msg)                                                           \
  do {                                                                         \
    if (searchDiagEnabled()) {                                                 \
      qWarning() << "[SearchDiag][ScrollManager]" << msg;                      \
    }                                                                          \
  } while (0)

void ScrollManager::updateMediaItemCount(int mediaItemCount) {
  if (!m_dataManager || !m_virtualContainer) {
    return;
  }

  mediaItemCount = std::max(0, mediaItemCount);
  m_dataManager->resizeStorage(mediaItemCount);
  m_totalItems = m_dataManager->totalItemCount();

  if (m_totalItems == 0) {
    // Keep container and metrics consistent even when empty.
    calculateVirtualMetrics();
    positionVirtualContainer();
    updateVirtualView();
    return;
  }

  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void ScrollManager::receiveItemsRange(
    int offset, const QStringList &filePaths,
    const QHash<QString, QString> &fileNames,
    const QHash<QString, QString> &fileToArtworkDir) {
  if (m_rangeReceiveDebugBudget > 0) {
    --m_rangeReceiveDebugBudget;
    debugLog("receiveItemsRange: offset="
             << offset << "incomingPaths=" << filePaths.size()
             << "storageFileCount="
             << (m_dataManager ? m_dataManager->fileCount() : -1)
             << "fileNames=" << fileNames.size());
    if (!filePaths.isEmpty()) {
      debugLog("receiveItemsRange: firstPath=" << filePaths.first());
    }
  }

  diagLog(
      QString(
          "receiveItemsRange: offset=%1 incomingPaths=%2 storageFileCount=%3")
          .arg(offset)
          .arg(filePaths.size())
          .arg(m_dataManager ? m_dataManager->fileCount() : -1));

  if (offset < 0 || offset >= m_dataManager->fileCount()) {
    diagLog("receiveItemsRange: IGNORED (offset out of bounds)");
    return;
  }

  // Pre-warm the artwork directory cache in a background thread to avoid
  // blocking the main thread during widget creation. This is especially
  // important for showAllSubcollectionItems with many subcollections.
  //
  // OPTIMIZATION: Only prewarm directories for visible items (first ~100),
  // not the entire chunk. The prewarm also warms the OS dentry cache,
  // making subsequent accesses fast.
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    qWarning() << "[PerfTrace] receiveItemsRange: fileToArtworkDir.size="
               << fileToArtworkDir.size() << "showAllSubcollectionItems="
               << m_context.config.showAllSubcollectionItems
               << "offset=" << offset << "filePathsSize=" << filePaths.size();
  }
  if (!fileToArtworkDir.isEmpty() &&
      m_context.config.showAllSubcollectionItems) {
    // Build artwork directory list in filePaths ORDER so visible items
    // (which are at the start of filePaths) get their directories scanned
    // first. Limit to first ~100 items to avoid scanning hundreds of
    // directories.
    constexpr int maxItemsToPrewarm =
        UIConstants::Scroll::ARTWORK_PREWARM_LARGE_COLLECTION_THRESHOLD;
    QStringList artworkDirs;
    QSet<QString> seen;
    int itemCount = 0;
    for (const QString &filePath : filePaths) {
      if (itemCount >= maxItemsToPrewarm) {
        break;
      }
      QString artDir = fileToArtworkDir.value(filePath);
      if (!artDir.isEmpty() && !seen.contains(artDir)) {
        artworkDirs.append(artDir);
        seen.insert(artDir);
      }
      ++itemCount;
    }

    if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
      qWarning() << "[PerfTrace] receiveItemsRange: PREWARM artworkDirs.size="
                 << artworkDirs.size();
    }

    // Debounce artwork prewarm - skip if one was triggered recently (within
    // 200ms). Multiple chunks arrive in quick succession and we don't need to
    // prewarm each.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 prewarmDebounceMs =
        UIConstants::Scroll::ARTWORK_PREWARM_DEBOUNCE_MS;
    if (now - m_lastArtworkPrewarmTime < prewarmDebounceMs) {
      if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
        qWarning()
            << "[PerfTrace] receiveItemsRange: SKIPPING PREWARM (debounced)";
      }
    } else {
      m_lastArtworkPrewarmTime = now;

      // Pre-warm directory cache in background for FUTURE lookups.
      // This doesn't block current widgets - they use direct file lookup as
      // fallback. Only do a single callback when complete to avoid reconfigure
      // spam.
      QPointer<ScrollManager> self = this;

      QThreadPool::globalInstance()->start([artworkDirs, self]() {
        auto &cache = ArtworkUtils::DirectoryCache::instance();
        cache.prewarmDirectories(artworkDirs);
        cache.processQueuedDirectories();

        // Single callback when complete
        if (self) {
          QMetaObject::invokeMethod(self, "reconfigureArtworkForActiveWidgets",
                                    Qt::QueuedConnection);
        }
      });
    }
  }

  // Only clear the fulfilled chunk from pending requests.
  // Keeping empty-range requests pending prevents a tight request loop
  // when the DB returns no rows (e.g., filter/count mismatch).
  if (m_widgetFactory && !filePaths.isEmpty()) {
    m_widgetFactory->clearPendingRangeRequest(offset);
  }

  // Store data and get visual indices that were updated
  QList<int> updatedIndices =
      m_dataManager->receiveItemsRange(offset, filePaths, fileNames);

  diagLog(QString("receiveItemsRange: updatedVisualIndices=%1")
              .arg(updatedIndices.size()));

  // Release placeholder widgets so they get re-created with actual data
  for (int visualIndex : updatedIndices) {
    if (ItemWidget *widget = m_activeWidgets.value(visualIndex, nullptr)) {
      releaseWidget(widget);
      m_activeWidgets.remove(visualIndex);
    }
  }

  // Trigger update of visible widgets
  updateVirtualView();
}

void ScrollManager::injectCachedItems(
    int startIndex, const QStringList &filePaths,
    const QHash<QString, QString> &fileNames,
    const QHash<QString, QString> &artworkPaths) {
  if (!m_dataManager || filePaths.isEmpty()) {
    return;
  }

  // Calculate the media offset (accounting for subcollections/virtual folders)
  int subcollectionCount = m_dataManager->subcollectionCount();
  int virtualFolderCount = m_dataManager->virtualFolderCount();
  int prefixCount = subcollectionCount + virtualFolderCount;

  // Convert visual startIndex to media offset
  int mediaOffset =
      (startIndex >= prefixCount) ? (startIndex - prefixCount) : 0;

  diagLog(QString("injectCachedItems: startIndex=%1 mediaOffset=%2 "
                  "pathCount=%3 prefixCount=%4")
              .arg(startIndex)
              .arg(mediaOffset)
              .arg(filePaths.size())
              .arg(prefixCount));

  // Store cached data directly into the data manager
  QList<int> updatedIndices =
      m_dataManager->receiveItemsRange(mediaOffset, filePaths, fileNames);

  // Set cached artwork paths on the widget factory for instant artwork lookup
  if (!artworkPaths.isEmpty() && m_widgetFactory) {
    m_widgetFactory->setCachedArtworkPaths(artworkPaths);
    if (qEnvironmentVariableIsSet("KARTEND_ARTWORK_DIAG")) {
      qWarning() << "[ArtworkDiag] injectCachedItems: setting"
                 << artworkPaths.size() << "cached artwork paths";
      if (!artworkPaths.isEmpty()) {
        auto it = artworkPaths.constBegin();
        qWarning() << "[ArtworkDiag] first cached entry: key=" << it.key()
                   << "value=" << it.value();
      }
    }
  } else if (qEnvironmentVariableIsSet("KARTEND_ARTWORK_DIAG")) {
    qWarning() << "[ArtworkDiag] injectCachedItems: artworkPaths.isEmpty="
               << artworkPaths.isEmpty()
               << "m_widgetFactory=" << (m_widgetFactory != nullptr);
  }

  diagLog(QString("injectCachedItems: updatedVisualIndices=%1 artworkPaths=%2")
              .arg(updatedIndices.size())
              .arg(artworkPaths.size()));

  // Trigger immediate update of visible widgets
  updateVirtualView();
}

bool ScrollManager::getCurrentViewportForCache(
    int &startIndex, int &totalItems, QStringList &filePaths,
    QHash<QString, QString> &fileNames,
    QHash<QString, QString> &artworkPaths) const {
  if (!m_dataManager || !m_mediaScrollArea) {
    return false;
  }

  totalItems = getTotalItems();
  if (totalItems <= 0) {
    return false;
  }

  // Get the first visible row to determine start index
  int firstVisibleRow = getFirstVisibleRow();
  int lastVisibleRow = getLastVisibleRow();
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0)
    gridWidth = 1;

  // Calculate visible range with buffer (cache 2 extra rows on each side)
  int bufferRows = 2;
  int startRow = std::max(0, firstVisibleRow - bufferRows);
  int endRow = lastVisibleRow + bufferRows;

  startIndex = startRow * gridWidth;
  int endIndex = std::min((endRow + 1) * gridWidth, totalItems);

  // Get the prefix count (subcollections + virtual folders)
  int prefixCount =
      m_dataManager->subcollectionCount() + m_dataManager->virtualFolderCount();

  // We only cache media items (not subcollection tiles)
  int mediaStartIndex = std::max(0, startIndex - prefixCount);
  int mediaEndIndex = std::max(0, endIndex - prefixCount);

  if (mediaEndIndex <= mediaStartIndex) {
    return false;
  }

  // Collect file paths and names for the visible + buffered range
  filePaths.clear();
  fileNames.clear();
  artworkPaths.clear();

  const QStringList &allPaths = m_dataManager->filePaths();
  const QHash<QString, QString> &allNames = m_dataManager->fileNames();
  const QString &defaultArtworkDir = m_context.artworkDirectory;
  const bool showAllSubcollectionItems =
      m_context.config.showAllSubcollectionItems;

  static const bool diagEnabled =
      qEnvironmentVariableIntValue("KARTEND_ARTWORK_DIAG");
  int artworkMisses = 0;
  int artworkHits = 0;

  int pathsCollected = 0;
  for (int i = mediaStartIndex; i < mediaEndIndex && i < allPaths.size(); ++i) {
    const QString &path = allPaths.at(i);
    if (!path.isEmpty()) {
      filePaths.append(path);
      if (allNames.contains(path)) {
        fileNames[path] = allNames.value(path);
      }

      // Resolve artwork path for this file.
      // For showAllSubcollectionItems, each file may have a different artwork
      // directory. Use hierarchy cache for O(1) lookup instead of
      // m_fileToArtworkDir which may be empty.
      QString artworkDir = defaultArtworkDir;
      if (showAllSubcollectionItems && m_hierarchyCache) {
        QString cachedArtworkDir =
            m_hierarchyCache->artworkDirForFilePath(path);
        if (!cachedArtworkDir.isEmpty()) {
          artworkDir = cachedArtworkDir;
          ++artworkHits;
        } else {
          ++artworkMisses;
          if (diagEnabled && artworkMisses <= 3) {
            QFileInfo fi(path);
            qWarning()
                << "[ArtworkDiag] getCurrentViewportForCache: MISS for path"
                << path << "parentDir=" << fi.absolutePath();
          }
        }
      }

      if (!artworkDir.isEmpty()) {
        QString fileName = QFileInfo(path).completeBaseName();
        QString artPath =
            ArtworkUtils::findArtworkForFile(fileName, artworkDir);
        if (!artPath.isEmpty()) {
          artworkPaths[path] = artPath;
        }
      }
      ++pathsCollected;
    }
  }

  if (diagEnabled) {
    qWarning() << "[ArtworkDiag] getCurrentViewportForCache: "
                  "showAllSubcollectionItems="
               << showAllSubcollectionItems
               << "hierarchyCache=" << (m_hierarchyCache != nullptr)
               << "hits=" << artworkHits << "misses=" << artworkMisses
               << "artworkPaths=" << artworkPaths.size();
  }

  // Need at least some valid paths
  if (pathsCollected == 0) {
    return false;
  }

  // Return the visual start index (including prefixes)
  startIndex = startRow * gridWidth;

  return true;
}

void ScrollManager::initializeSubcollections() {
  m_dataManager->initializeSubcollections(m_context, m_collections,
                                          m_hierarchyCache);
}

void ScrollManager::initializeVirtualFolders() {
  m_dataManager->initializeVirtualFolders(m_context);
}

void ScrollManager::setupFilePathMappings() {
  m_dataManager->setupFilePathMappings(m_context);
}

// processRelativeFilePaths is no longer needed - DatabaseManager now handles
// relative path resolution via resolveFilePath() and resolveRelativeFilePath()

void ScrollManager::setupEmptyVirtualScrolling() {
  calculateVirtualMetrics();
  createVirtualContainer();
  if (m_virtualContainer) {
    m_virtualContainer->setVisible(true);
  }
  emit virtualScrollSetupComplete();
}

void ScrollManager::setupNormalVirtualScrolling() {
  calculateVirtualMetrics();
  createVirtualContainer();

  // Bail early if container creation failed (null gridContainer)
  if (!m_virtualContainer) {
    qWarning() << "ScrollManager::setupNormalVirtualScrolling: Failed to "
                  "create virtual container";
    return;
  }

  // Configure widget factory with current context and metrics
  if (m_widgetFactory) {
    m_widgetFactory->setDatabaseManager(m_databaseManager);
    m_widgetFactory->setParentWidget(m_virtualContainer);
    m_widgetFactory->setCollectionContext(m_context);
    m_widgetFactory->setMetrics(m_metrics.itemWidth, m_metrics.itemHeight);
    m_widgetFactory->setFileData(&m_dataManager->filePaths(),
                                 &m_dataManager->fileNames());
    m_widgetFactory->setTotalItemCount(m_dataManager->fileCount());
    m_widgetFactory->setSubcollectionNameResolver(
        [this](int idx) { return getSubcollectionName(idx); });
  }

  // Pre-position scrollbar BEFORE creating widgets to prevent visual jump
  // where list briefly shows at position 0 then jumps to remembered position
  if (m_initialScrollIndex >= 0 && m_mediaScrollArea) {
    if (QScrollBar *scrollbar = m_mediaScrollArea->verticalScrollBar()) {
      int viewportHeight = m_mediaScrollArea->viewport()->height();
      // Use clamped totalHeight for scrollbar range
      int scrollMax = qMax(0, m_metrics.totalHeight - viewportHeight);
      int itemY = GridUtils::computeItemY(
          m_initialScrollIndex, m_metrics.itemsPerRow, m_metrics.itemHeight,
          m_metrics.verticalSpacing, UIConstants::Grid::MARGINS);
      // Calculate target logical scroll Y (to center the item)
      int logicalTargetY =
          itemY + (m_metrics.itemHeight / 2) - (viewportHeight / 2);
      // Ensure max >= min for qBound (content may be smaller than viewport)
      int logicalMax = qMax(0, m_metrics.logicalHeight - viewportHeight);
      logicalTargetY = qBound(0, logicalTargetY, logicalMax);
      // Convert logical scroll target to widget scroll position
      int targetY = m_metrics.toWidgetScrollY(logicalTargetY, viewportHeight);
      targetY = qBound(0, targetY, scrollMax);
      // Set scrollbar range explicitly before setting value to ensure it takes
      // effect
      scrollbar->setRange(0, scrollMax);
      scrollbar->setValue(targetY);
    }
    m_initialScrollIndex = -1; // Reset after use
  }

  updateVirtualView();
  positionVirtualContainer();
  if (m_virtualContainer) {
    m_virtualContainer->setVisible(true);
  }

  // Pre-warm widget pool during initial setup for smoother scrolling
  if (m_widgetPool) {
    int visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1;
    m_widgetPool->setVisibleMetrics(visibleRows, m_metrics.itemsPerRow);
    m_widgetPool->prewarm();
  }

  emit virtualScrollSetupComplete();
}

void ScrollManager::cleanup() {
  if (m_destroying) {
    return;
  }
  bool hasPreSearch =
      m_preSearchStateManager && m_preSearchStateManager->hasSavedState();
  if (m_activeWidgets.isEmpty() && (!m_virtualContainer) &&
      m_dataManager->filePaths().isEmpty() &&
      m_dataManager->subcollections().isEmpty() && !hasPreSearch) {
    return;
  }

  disconnectScrollEvents();

  // Clear artwork widget references FIRST - prevents stale widget pointers
  // from causing incorrect artwork or crashes when widgets are destroyed
  if (m_artworkManager) {
    m_artworkManager->clearWidgetReferences();
  }

  // Clear cached artwork paths - no longer valid for new collection
  if (m_widgetFactory) {
    m_widgetFactory->clearCachedArtworkPaths();
  }

  // Explicitly delete active widgets before clearing the pool
  // This ensures proper cleanup order and prevents use-after-free
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      delete widget;
    }
  }
  m_activeWidgets.clear();

  // DO NOT delete pre-search widgets here - they are reparented to
  // m_gridContainer and will be restored when search is cleared. Only delete
  // them on full cleanup (when m_destroying is true, which returns early above)

  // Clear the widget pool - if we have pre-search widgets, use soft clear
  // to allow potential reuse when search is cleared
  // Otherwise use clearAndDelete for explicit cleanup
  // IMPORTANT: Pass m_gridContainer as safe parent so widgets are reparented
  // BEFORE the virtual container is deleted - prevents crash on reuse
  if (m_widgetPool) {
    if (hasPreSearch) {
      // Pre-search widgets exist - soft clear allows widget reuse
      m_widgetPool->softClear(m_gridContainer);
    } else {
      // Full cleanup - use soft clear to allow reuse during next collection
      // load The prewarm timer will prune unused stale widgets during idle
      m_widgetPool->softClear(m_gridContainer);
    }
  }

  cleanupVirtualContainer();
  if (m_filterManager) {
    m_filterManager->clearFilter();
  }
  m_dataManager->clear();
  m_totalItems = 0;
}
