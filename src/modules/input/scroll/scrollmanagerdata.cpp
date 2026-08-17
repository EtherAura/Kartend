// Data ingest, virtual scrolling setup, and cleanup methods extracted from
// scrollmanager.cpp. All remain ScrollManager members and
// access existing class state (m_dataManager, m_filterManager,
// m_widgetFactory, m_widgetPool, m_artworkManager, m_metrics, etc.).
#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "artworkutils.h"
#include "coverflowcontroller.h"
#include "filtermanager.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "loggingcategories.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrolleventhandler.h"
#include "scrollmanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "timerutils.h"
#include "uiconstants/grid.h"
#include "uiconstants/scroll.h"
#include "virtualcontainermanager.h"
#include "widgetpoolmanager.h"

#include <QCoreApplication>
#include <QHash>

#include <QLoggingCategory>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcScrollManager().isDebugEnabled()) {                                                      \
      qCDebug(lcScrollManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

void ScrollManager::updateMediaItemCount(int mediaItemCount) {
  if (!m_dataManager || !m_virtualContainer) {
    return;
  }

  mediaItemCount = std::max(0, mediaItemCount);
  m_dataManager->resizeStorage(mediaItemCount);
  m_totalItems = m_dataManager->totalItemCount();

  // Same sequence whether empty or not: updateVirtualView evicts every
  // active widget when the needed band is empty, keeping container and
  // metrics consistent for the zero-item case too.
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void ScrollManager::refreshManualCoverPaths(bool force) {
  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  if (!db) {
    return;
  }
  if (m_manualCoverPathsLoaded && !force) {
    return;
  }
  m_manualCoverPaths = db->loadManualCoverPaths();
  // databaseFilePath() is empty until the connection is open, which is exactly
  // the case where the empty map above means "couldn't read" rather than
  // "nothing is linked" — don't latch on it.
  m_manualCoverPathsLoaded = !db->databaseFilePath().isEmpty();

  if (m_widgetFactory) {
    m_widgetFactory->setManualCoverPaths(m_manualCoverPaths);
  }
  if (m_coverFlow) {
    m_coverFlow->setManualCoverPaths(m_manualCoverPaths);
  }
  if (m_filterManager) {
    m_filterManager->setManualCoverPaths(m_manualCoverPaths);
  }
}

void ScrollManager::onItemArtworkLinksChanged(const QString & /*collectionUuid*/,
                                              const QString &path) {
  refreshManualCoverPaths(/*force=*/true);

  // Repaint the one tile the edit is about. reconfigureArtworkForActiveWidgets
  // is the wrong tool here: it skips every widget that already has artwork, so
  // it would never REPLACE an auto-discovered cover with the link the user just
  // made, and a link that replaces nothing still needs its widget revisited
  // even though the directory cache holds an unchanged negative for that name.
  // configureArtworkForWidget now consults the manual map first, and
  // ArtworkManager::addPendingArtwork re-delivers on a changed path.
  if (m_widgetFactory && !path.isEmpty()) {
    for (auto it = m_activeWidgets.constBegin(); it != m_activeWidgets.constEnd(); ++it) {
      ItemWidget *widget = it.value();
      if (widget && widget->getFilePath() == path) {
        m_widgetFactory->configureArtworkForWidget(widget, path);
      }
    }
  }
  if (auto *art = m_ctx ? m_ctx->artworkManager() : nullptr) {
    art->scheduleViewportUpdate();
  }
  // The carousel resolves its faces at card-build time, so it needs a rebuild
  // rather than a repaint. No-op when the carousel is not the active view.
  if (m_coverFlow) {
    m_coverFlow->rebuildCardsIfActive();
  }
  // A link can make a previously-artless item visible under hideMissingArtwork
  // (or clearing one can take it away again), so re-run the baseline.
  refreshHideMissingArtworkBaseline();
}

void ScrollManager::receiveItemsRange(int offset, const QStringList &filePaths,
                                      const QHash<QString, QString> &fileNames,
                                      const QHash<QString, QString> &fileToArtworkDir) {
  if (m_rangeReceiveDebugBudget > 0) {
    --m_rangeReceiveDebugBudget;
    debugLog("receiveItemsRange: offset=" << offset << "incomingPaths=" << filePaths.size()
                                          << "storageFileCount="
                                          << (m_dataManager ? m_dataManager->fileCount() : -1)
                                          << "fileNames=" << fileNames.size());
    if (!filePaths.isEmpty()) {
      debugLog("receiveItemsRange: firstPath=" << filePaths.first());
    }
  }

  qCDebug(lcSearchDiag) << QString(
                               "receiveItemsRange: offset=%1 incomingPaths=%2 storageFileCount=%3")
                               .arg(offset)
                               .arg(filePaths.size())
                               .arg(m_dataManager ? m_dataManager->fileCount() : -1);

  if (offset < 0 || offset >= m_dataManager->fileCount()) {
    qCDebug(lcSearchDiag) << "receiveItemsRange: IGNORED (offset out of bounds)";
    return;
  }

  // Pre-warm the artwork directory cache in a background thread to avoid
  // blocking the main thread during widget creation. This is especially
  // important for showAllSubcollectionItems with many subcollections.
  //
  // OPTIMIZATION: Only prewarm directories for visible items (first ~100),
  // not the entire chunk. The prewarm also warms the OS dentry cache,
  // making subsequent accesses fast.
  qCDebug(lcPerfTrace) << "receiveItemsRange: fileToArtworkDir.size=" << fileToArtworkDir.size()
                       << "showAllSubcollectionItems=" << m_context.config.showAllSubcollectionItems
                       << "offset=" << offset << "filePathsSize=" << filePaths.size();
  if (!fileToArtworkDir.isEmpty() && m_context.config.showAllSubcollectionItems) {
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

    qCDebug(lcPerfTrace) << "receiveItemsRange: PREWARM artworkDirs.size=" << artworkDirs.size();
    // Debounce artwork prewarm - skip if one was triggered recently (within
    // 200ms). Multiple chunks arrive in quick succession and we don't need to
    // prewarm each.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 prewarmDebounceMs = UIConstants::Scroll::ARTWORK_PREWARM_DEBOUNCE_MS;
    if (now - m_lastArtworkPrewarmTime < prewarmDebounceMs) {
      qCDebug(lcPerfTrace) << "receiveItemsRange: SKIPPING PREWARM (debounced)";
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

        // Single callback when complete. Kartend-t4e00: the QPointer is
        // resolved ON THE GUI THREAD inside the qApp hop — reading it here
        // on the pool thread while ScrollManager is destroyed on the GUI
        // thread is a data race, and check-then-invoke was TOCTOU even when
        // the read happened to be safe. The worker only copies the pointer
        // value. qApp can be gone if this abandoned task outlives the app —
        // skip the callback in that case (nothing left to reconfigure).
        if (QCoreApplication *app = QCoreApplication::instance()) {
          QMetaObject::invokeMethod(
              app,
              [self]() {
                if (self) {
                  // String-based: the target is a private slot.
                  QMetaObject::invokeMethod(self, "reconfigureArtworkForActiveWidgets",
                                            Qt::DirectConnection);
                }
              },
              Qt::QueuedConnection);
        }
      });
    }
  }

  // Clear the fulfilled chunk from pending requests. An empty (zero-row)
  // response goes through onEmptyRangeResponse instead, which permits a bounded
  // number of re-requests then stops — keeping a legitimately-empty chunk from
  // either looping tightly or staying stuck on "Loading..." forever
  // (Kartend-ejsf).
  if (m_widgetFactory) {
    if (!filePaths.isEmpty()) {
      m_widgetFactory->clearPendingRangeRequest(offset);
    } else {
      m_widgetFactory->onEmptyRangeResponse(offset);
    }
  }

  // Store data and get visual indices that were updated
  QList<int> updatedIndices = m_dataManager->receiveItemsRange(offset, filePaths, fileNames);

  qCDebug(lcSearchDiag)
      << QString("receiveItemsRange: updatedVisualIndices=%1").arg(updatedIndices.size());

  // Release placeholder widgets so they get re-created with actual data.
  // Mirrors removeUnneededWidgets: the visible-row range is constant across
  // this loop, so compute it once — lazily, only if something is actually
  // released — instead of letting every one-arg releaseWidget call recompute
  // it.
  int visibleRows = -1;
  for (int visualIndex : updatedIndices) {
    if (ItemWidget *widget = m_activeWidgets.value(visualIndex, nullptr)) {
      if (visibleRows < 0) {
        visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1;
      }
      releaseWidget(widget, visibleRows);
      removeActiveWidget(visualIndex);
    }
  }

  // Trigger update of visible widgets
  updateVirtualView();

  // cover-flow keeps a flat card list, refresh it whenever new file data
  // lands. Skip when not in cover flow — building a card descriptor for
  // every visual index is fast per-item but pointless work when the widget
  // is hidden, and 30+ chunk arrivals × 29k items adds up. When it IS
  // active, patch only the chunk's own indices (Kartend-x7bn8): the old
  // full rebuildCardsIfActive() redid per-item DB resolution for all N
  // cards on every chunk — O(N) work × O(N/chunk) arrivals on the GUI
  // thread. updateCardsIfActive falls back to the full rebuild itself when
  // a filter is active or the card count changed.
  // Kartend-l66sn: when the hideMissingArtwork baseline is the active filter,
  // it was computed while these rows were still empty pre-sized entries,
  // which pass it as "artwork unknown". Re-evaluate now that real paths
  // landed — debounced to the trailing edge so a chunk burst re-filters
  // once, not per arrival. The refresh ends in rebuildCardsIfActive(), so
  // the per-arrival cover-flow update is skipped here: with a filter active
  // it can only fall back to a full rebuildCards() anyway, and doing that
  // per chunk wipes the widget's pixmap caches over and over — the visible
  // "cards load in and out until the load settles" failure Kartend-i3mmq
  // documents.
  if (m_filterManager && m_filterManager->isFiltered() &&
      m_filterManager->currentFilter().isEmpty() && m_baselineRefilterTimer) {
    m_baselineRefilterTimer->trigger();
  } else {
    m_coverFlow->updateCardsIfActive(updatedIndices);
  }
}

void ScrollManager::injectCachedItems(int startIndex, const QStringList &filePaths,
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
  int mediaOffset = (startIndex >= prefixCount) ? (startIndex - prefixCount) : 0;

  qCDebug(lcSearchDiag) << QString("injectCachedItems: startIndex=%1 mediaOffset=%2 "
                                   "pathCount=%3 prefixCount=%4")
                               .arg(startIndex)
                               .arg(mediaOffset)
                               .arg(filePaths.size())
                               .arg(prefixCount);

  // Store cached data directly into the data manager
  QList<int> updatedIndices = m_dataManager->receiveItemsRange(mediaOffset, filePaths, fileNames);

  // Set cached artwork paths on the widget factory for instant artwork lookup.
  // Diag toggle snapshotted once per process — same pattern as
  // getCurrentViewportForCache below; the env var can't change mid-run.
  static const bool artworkDiag = qEnvironmentVariableIsSet("KARTEND_ARTWORK_DIAG");
  if (!artworkPaths.isEmpty() && m_widgetFactory) {
    m_widgetFactory->setCachedArtworkPaths(artworkPaths);
    if (artworkDiag) {
      qCDebug(lcSearchDiag) << "[ArtworkDiag] injectCachedItems: setting" << artworkPaths.size()
                            << "cached artwork paths";
      if (!artworkPaths.isEmpty()) {
        auto it = artworkPaths.constBegin();
        qCDebug(lcSearchDiag) << "[ArtworkDiag] first cached entry: key=" << it.key()
                              << "value=" << it.value();
      }
    }
  } else if (artworkDiag) {
    qCDebug(lcSearchDiag) << "[ArtworkDiag] injectCachedItems: artworkPaths.isEmpty="
                          << artworkPaths.isEmpty()
                          << "m_widgetFactory=" << static_cast<bool>(m_widgetFactory);
  }

  qCDebug(lcSearchDiag) << QString("injectCachedItems: updatedVisualIndices=%1 artworkPaths=%2")
                               .arg(updatedIndices.size())
                               .arg(artworkPaths.size());

  // Trigger immediate update of visible widgets
  updateVirtualView();

  // Kartend-l66sn: cached injection fills real paths without going through
  // receiveItemsRange, so it needs the same baseline re-arm — rows that just
  // gained a path may now be provably artless.
  if (m_filterManager && m_filterManager->isFiltered() &&
      m_filterManager->currentFilter().isEmpty() && m_baselineRefilterTimer) {
    m_baselineRefilterTimer->trigger();
  }
}

bool ScrollManager::getCurrentViewportForCache(int &startIndex, int &totalItems,
                                               QStringList &filePaths,
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
  if (gridWidth <= 0) gridWidth = 1;

  // Calculate visible range with buffer (cache 2 extra rows on each side)
  int bufferRows = 2;
  int startRow = std::max(0, firstVisibleRow - bufferRows);
  int endRow = lastVisibleRow + bufferRows;

  startIndex = startRow * gridWidth;
  int endIndex = std::min((endRow + 1) * gridWidth, totalItems);

  // Get the prefix count (subcollections + virtual folders)
  int prefixCount = m_dataManager->subcollectionCount() + m_dataManager->virtualFolderCount();

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
  const bool showAllSubcollectionItems = m_context.config.showAllSubcollectionItems;

  static const bool diagEnabled = qEnvironmentVariableIntValue("KARTEND_ARTWORK_DIAG");
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
        QString cachedArtworkDir = m_hierarchyCache->artworkDirForFilePath(path);
        if (!cachedArtworkDir.isEmpty()) {
          artworkDir = cachedArtworkDir;
          ++artworkHits;
        } else {
          ++artworkMisses;
          if (diagEnabled && artworkMisses <= 3) {
            QFileInfo fi(path);
            qCDebug(lcSearchDiag) << "[ArtworkDiag] getCurrentViewportForCache: MISS for path"
                                  << path << "parentDir=" << fi.absolutePath();
          }
        }
      }

      if (!artworkDir.isEmpty()) {
        QString fileName = QFileInfo(path).completeBaseName();
        QString artPath = ArtworkUtils::findArtworkForFile(fileName, artworkDir);
        if (!artPath.isEmpty()) {
          artworkPaths[path] = artPath;
        }
      }
      ++pathsCollected;
    }
  }

  if (diagEnabled) {
    qCDebug(lcSearchDiag) << "[ArtworkDiag] getCurrentViewportForCache: "
                             "showAllSubcollectionItems="
                          << showAllSubcollectionItems
                          << "hierarchyCache=" << static_cast<bool>(m_hierarchyCache)
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
  m_dataManager->initializeSubcollections(m_context, m_collections, m_hierarchyCache);
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
    qCWarning(lcScrollManager) << "ScrollManager::setupNormalVirtualScrolling: Failed to "
                                  "create virtual container";
    return;
  }

  // Configure widget factory with current context and metrics. Kartend-davi:
  // ctx is bound once in setup; the factory's setDatabaseManager has been
  // retired in favour of ctx-routed access.
  if (m_widgetFactory) {
    m_widgetFactory->setParentWidget(m_virtualContainer);
    m_widgetFactory->setCollectionContext(m_context);
    m_widgetFactory->setMetrics(m_metrics.itemWidth, m_metrics.itemHeight);
    m_widgetFactory->setFileData(&m_dataManager->filePaths(), &m_dataManager->fileNames());
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
      int itemY =
          GridUtils::computeItemY(m_initialScrollIndex, m_metrics.itemsPerRow, m_metrics.itemHeight,
                                  m_metrics.verticalSpacing, UIConstants::Grid::MARGINS);
      // Calculate target logical scroll Y (to center the item)
      int logicalTargetY = itemY + (m_metrics.itemHeight / 2) - (viewportHeight / 2);
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

  // Kartend-l66sn (second leg of the startup deadlock): the artwork-only
  // baseline is typically computed BEFORE the store pre-sizes its rows, over
  // a zero-length path list — leaving filteredIndices empty, which
  // materializes zero widgets, which requests zero ranges, so the
  // arrival-driven refresh below never gets its first arrival. Now that the
  // store is sized, re-evaluate the baseline (no-op unless it is the active
  // filter); under the empty-path-is-unknown rule every pre-sized row
  // becomes visible, widgets materialize, and the load can start.
  if (m_filterManager && m_filterManager->isFiltered() &&
      m_filterManager->currentFilter().isEmpty() && m_baselineRefilterTimer) {
    m_baselineRefilterTimer->trigger();
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
  bool hasPreSearch = m_preSearchStateManager && m_preSearchStateManager->hasSavedState();
  if (m_activeWidgets.isEmpty() && (!m_virtualContainer) && m_dataManager->filePaths().isEmpty() &&
      m_dataManager->subcollections().isEmpty() && !hasPreSearch) {
    return;
  }

  disconnectScrollEvents();

  // Clear artwork widget references FIRST - prevents stale widget pointers
  // from causing incorrect artwork or crashes when widgets are destroyed
  if (auto *art = m_ctx ? m_ctx->artworkManager() : nullptr) {
    art->clearWidgetReferences();
  }

  // Clear cached artwork paths - no longer valid for new collection
  if (m_widgetFactory) {
    m_widgetFactory->clearCachedArtworkPaths();
    // also drop the per-chunk "request already in flight" set.
    // Without this, chunk indices left over from the previous view (in-flight
    // requests, or empty-result chunks that the receiveItemsRange path
    // intentionally keeps pending) suppress every new chunk request after a
    // reload — widgets stay stuck on the "Loading..." placeholder until
    // restart, which is exactly the symptom users see after adding a new
    // subcollection.
    m_widgetFactory->clearPendingRangeRequests();
  }

  // Explicitly delete active widgets before clearing the pool
  // This ensures proper cleanup order and prevents use-after-free
  for (ItemWidget *widget : std::as_const(m_activeWidgets)) {
    delete widget;
  }
  clearActiveWidgets();

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
